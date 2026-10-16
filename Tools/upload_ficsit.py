"""Upload the merged InfoScreens package to an existing ficsit.app listing.

Sign in at https://ficsit.app through OAuth, create an InfoScreens listing if
needed, wait for its approval, and complete its AI-use and network-use disclosures.
Use the site's SMR session token (the signed-in site's `token` cookie), NOT a
GitHub personal access token, password, or OAuth provider access token. Supply
FICSIT_TOKEN in the environment or enter it at the hidden terminal prompt.

Example: python3 Tools/upload_ficsit.py --mod-reference InfoScreens \
    --changelog "Initial release" --stability release

The default archive is dist/InfoScreens.zip, with Windows/ and LinuxServer/
roots. Install wrappers and individual-platform ZIPs cannot be uploaded here.
This command publishes a real version; it has no implicit retries or dry run.

Protocol sources: satisfactorymodding/smr-api (staging) schemas/version.graphql,
api/api.go, gql/resolver_versions.go, tests/version_test.go; smr-frontend
(staging) src/lib/core/graphql.ts and src/lib/utils/chunked-upload.ts.
"""

from __future__ import annotations

import argparse
import getpass
import json
import os
from pathlib import Path, PurePosixPath
import re
import sys
import time
from typing import TypeAlias
from urllib import error, request
import uuid
import warnings
import zipfile

ROOT = Path(__file__).resolve().parents[1]
MOD = "InfoScreens"
GAME_CL = "513885"
ENDPOINT = "https://api.ficsit.app/v2/query"
CHUNK_SIZE = 10_000_000  # Matches the official frontend; S3 parts are 1-based.
MAX_PARTS = 100  # Resolver's actual limit (its older error text says 41).
POLL_ATTEMPTS = 61  # Immediate check, then 60 ten-second intervals.
POLL_INTERVAL = 10
# ficsit.app's dependency schema accepts one comparator, not compound ranges.
DEPENDENCY_VERSION = re.compile(
    r"(<=|<|>|>=|\^)?(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(?:-((?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)"
    r"(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?"
    r"(?:\+([0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?",
    re.ASCII,
)
JSONValue: TypeAlias = None | bool | int | float | str | list["JSONValue"] | dict[str, "JSONValue"]

AUTH_QUERY = "query UploadIdentity { getMe { id } }"
MOD_QUERY = """query UploadListing($listing: String!) {
  getModByIdOrReference(modIdOrReference: $listing) {
    id mod_reference approved network_use_disclosure
    ai_use_disclosure { disclosure_type message }
  }
}"""
CREATE_MUTATION = """mutation StartUpload($modId: ModID!) {
  createVersion(modId: $modId)
}"""
PART_MUTATION = """mutation UploadPart($modId: ModID!, $versionId: VersionID!, $part: Int!, $file: Upload!) {
  uploadVersionPart(modId: $modId, versionId: $versionId, part: $part, file: $file)
}"""
FINALIZE_MUTATION = """mutation FinalizeUpload($modId: ModID!, $versionId: VersionID!, $version: NewVersion!) {
  finalizeCreateVersion(modId: $modId, versionId: $versionId, version: $version)
}"""
STATE_QUERY = """query UploadState($modId: ModID!, $versionId: VersionID!) {
  checkVersionUploadState(modId: $modId, versionId: $versionId) {
    auto_approved version { id version }
  }
}"""


class UploadError(RuntimeError):
    """An actionable failure safe to display without a traceback."""


def object_value(value: JSONValue, context: str) -> dict[str, JSONValue]:
    if not isinstance(value, dict):
        raise UploadError(f"Invalid API response: expected {context}")
    return value


def string_value(value: JSONValue, context: str) -> str:
    if not isinstance(value, str) or not value:
        raise UploadError(f"Invalid API response: missing {context}")
    return value


def validate_archive(path: Path) -> str:
    """Reject install layouts and mismatched multi-target plugin metadata locally."""
    if path.stat().st_size > CHUNK_SIZE * MAX_PARTS:
        raise UploadError("Archive exceeds the API's 100-part upload limit (1,000,000,000 bytes)")
    try:
        with zipfile.ZipFile(path) as archive:
            names = archive.namelist()
            if len(names) != len(set(names)):
                raise UploadError("Archive contains duplicate entries")
            for name in names:
                parts = PurePosixPath(name).parts
                if (not parts or parts[0] not in ("Windows", "LinuxServer")
                        or ".." in parts or "\\" in name or name.startswith("/")):
                    raise UploadError("Use the merged ZIP with Windows/ and LinuxServer/ roots, not an install wrapper")
            descriptors = {name for name in names if name.endswith(".uplugin")}
            expected = {f"{target}/{MOD}.uplugin" for target in ("Windows", "LinuxServer")}
            if descriptors != expected:
                raise UploadError("Merged archive must contain exactly Windows/InfoScreens.uplugin and LinuxServer/InfoScreens.uplugin descriptors")
            windows = archive.read(f"Windows/{MOD}.uplugin")
            linux = archive.read(f"LinuxServer/{MOD}.uplugin")
            if windows != linux:
                raise UploadError("Platform descriptors must be byte-identical, as required by ficsit.app")
            descriptor = object_value(json.loads(windows.decode("utf-8-sig")), "plugin descriptor")
            version = descriptor.get("SemVersion")
            if not isinstance(version, str):
                raise UploadError("Plugin descriptor must contain a release SemVersion")
            match = re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", version)
            if not match or descriptor.get("VersionName") != version or descriptor.get("Version") != int(match[1]):
                raise UploadError("Plugin descriptor must have matching release SemVersion, VersionName and integer major Version")
            if descriptor.get("GameVersion") != GAME_CL or descriptor.get("RequiredOnRemote") is not True:
                raise UploadError(f"Plugin descriptor must target CL{GAME_CL} and require the mod on remote peers")
            dependencies = descriptor.get("Plugins", [])
            if not isinstance(dependencies, list):
                raise UploadError("Plugin dependencies must be a list")
            for dependency in dependencies:
                if not isinstance(dependency, dict):
                    raise UploadError("Plugin dependency must be an object")
                if "SemVersion" in dependency:
                    constraint = dependency["SemVersion"]
                    if not isinstance(constraint, str) or not DEPENDENCY_VERSION.fullmatch(constraint):
                        raise UploadError(
                            "Plugin dependency SemVersion must use one ficsit.app-supported "
                            "comparator and semantic version (for example ^3.12.0), not a compound range"
                        )
            modules = descriptor.get("Modules")
            if not isinstance(modules, list) or not any(
                isinstance(module, dict) and module.get("Name") == MOD and module.get("Type") == "Runtime"
                for module in modules
            ):
                raise UploadError("Plugin descriptor is missing the InfoScreens Runtime module")
            for target, suffix in (("Windows", ".dll"), ("LinuxServer", ".so")):
                if not any(name.startswith(f"{target}/Content/") and name.endswith(".pak") for name in names):
                    raise UploadError(f"Missing cooked .pak content for {target}")
                if not any(name.startswith(f"{target}/Binaries/") and name.endswith(suffix) for name in names):
                    raise UploadError(f"Missing native {suffix} content for {target}")
            corrupt = archive.testzip()
            if corrupt:
                raise UploadError(f"Archive CRC check failed for {corrupt}")
            return version
    except (zipfile.BadZipFile, UnicodeError, json.JSONDecodeError, RuntimeError) as exc:
        if isinstance(exc, UploadError):
            raise
        raise UploadError("Invalid or unreadable merged ZIP/plugin descriptor") from exc


class NoRedirect(request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        # urllib otherwise copies Authorization to the redirect destination.
        raise UploadError("API redirect refused to protect the session token")


class Client:
    def __init__(self, token: str, *, endpoint: str = ENDPOINT):
        self._token = token
        self._endpoint = endpoint
        self._opener = request.build_opener(NoRedirect())

    def safe_message(self, message: str) -> str:
        return " ".join(message.replace(self._token, "[redacted]").split())

    def execute(self, query: str, variables: dict[str, JSONValue], chunk: bytes | None = None) -> dict[str, JSONValue]:
        operations = json.dumps({"query": query, "variables": variables}).encode()
        if chunk is None:
            body = operations
            content_type = "application/json"
        else:
            boundary = "InfoScreens" + uuid.uuid4().hex
            body = b"".join((
                f'--{boundary}\r\nContent-Disposition: form-data; name="operations"\r\n\r\n'.encode(),
                operations,
                f'\r\n--{boundary}\r\nContent-Disposition: form-data; name="map"\r\n\r\n'.encode(),
                b'{"0":["variables.file"]}',
                f'\r\n--{boundary}\r\nContent-Disposition: form-data; name="0"; filename="InfoScreens.zip"\r\nContent-Type: application/octet-stream\r\n\r\n'.encode(),
                chunk,
                f"\r\n--{boundary}--\r\n".encode(),
            ))
            content_type = f"multipart/form-data; boundary={boundary}"
        req = request.Request(self._endpoint, data=body, headers={
            "Authorization": self._token,  # Raw SMR session, not 'Bearer <token>'.
            "Content-Type": content_type,
            "Accept": "application/json",
            "User-Agent": "InfoScreens-uploader/0.0.1",
        }, method="POST")
        try:
            with self._opener.open(req, timeout=120) as response:
                raw = response.read(1_048_577)
        except error.HTTPError as exc:
            exc.close()
            if exc.code in (401, 403):
                raise UploadError("API authentication/permission denied; use a current SMR session token from an authorized listing owner") from None
            # Never echo HTTP bodies, headers, or a Request object's representation.
            raise UploadError(f"API request failed with HTTP {exc.code}; no automatic retry was attempted") from None
        except (error.URLError, TimeoutError, OSError):
            raise UploadError("API network request failed or timed out; no automatic retry was attempted") from None
        if len(raw) > 1_048_576:
            raise UploadError("API response exceeded the expected size")
        try:
            payload = object_value(json.loads(raw), "GraphQL response")
        except (ValueError, UnicodeError):
            raise UploadError("API returned an invalid JSON response") from None
        errors = payload.get("errors")
        if errors:
            messages = []
            if isinstance(errors, list):
                for item in errors:
                    if isinstance(item, dict) and isinstance(item.get("message"), str):
                        messages.append(self.safe_message(item["message"]))
            detail = "; ".join(messages) or "unspecified GraphQL error"
            if any(word in detail.lower() for word in ("logged in", "authorized", "token", "session")):
                detail += "; use a current SMR OAuth session token (not a GitHub PAT) with listing upload permission"
            raise UploadError("API: " + detail)
        return object_value(payload.get("data"), "GraphQL data")


def credentials() -> str:
    token = os.environ.get("FICSIT_TOKEN", "").strip()
    if not token:
        if not sys.stdin.isatty():
            raise UploadError("Set FICSIT_TOKEN or run from a terminal for the hidden SMR session prompt")
        try:
            with warnings.catch_warnings():
                warnings.simplefilter("error", getpass.GetPassWarning)
                token = getpass.getpass("ficsit.app SMR session token (hidden): ").strip()
        except (getpass.GetPassWarning, EOFError):
            raise UploadError("A hidden terminal prompt is unavailable; set FICSIT_TOKEN instead") from None
    if not token or any(character.isspace() or ord(character) < 33 or ord(character) > 126 for character in token):
        raise UploadError("Supply a nonempty raw SMR session token, without a Bearer prefix or whitespace")
    return token


def upload(client: Client, archive: Path, listing: str, changelog: str, stability: str,
           expected_version: str) -> dict[str, JSONValue]:
    identity = client.execute(AUTH_QUERY, {}).get("getMe")
    string_value(object_value(identity, "authenticated user").get("id"), "authenticated user ID")
    try:
        mod = client.execute(MOD_QUERY, {"listing": listing}).get("getModByIdOrReference")
    except UploadError as exc:
        raise UploadError(
            f"{exc}. Confirm the listing ID/reference on https://ficsit.app; create an InfoScreens "
            "listing there if needed, complete disclosures, and wait for approval."
        ) from None
    if mod is None:
        raise UploadError("No existing listing found; create one at https://ficsit.app with mod reference InfoScreens, complete disclosures, and wait for approval")
    mod = object_value(mod, "mod listing")
    if mod.get("mod_reference") != MOD:
        raise UploadError("Selected listing's mod reference must match the InfoScreens plugin descriptor")
    if mod.get("approved") is not True:
        raise UploadError("Listing must be approved on ficsit.app before uploading versions")
    disclosure = mod.get("ai_use_disclosure")
    if not isinstance(disclosure, dict) or (
        disclosure.get("disclosure_type") != "no_ai_usage" and not disclosure.get("message")
    ) or mod.get("network_use_disclosure") is None:
        raise UploadError("Complete the listing's AI-use and network-use disclosures on ficsit.app before uploading")
    mod_id = string_value(mod.get("id"), "listing ID")
    size = archive.stat().st_size
    if not 0 < size <= CHUNK_SIZE * MAX_PARTS:
        raise UploadError("Archive must be nonempty and fit the API's 100-part upload limit")
    upload_id = string_value(client.execute(CREATE_MUTATION, {"modId": mod_id}).get("createVersion"), "upload session ID")
    variables: dict[str, JSONValue] = {"modId": mod_id, "versionId": upload_id}
    print(client.safe_message(f"Upload session {upload_id}; sending {size:,} bytes unchanged"), flush=True)
    try:
        with archive.open("rb") as source:
            part = 0
            while chunk := source.read(CHUNK_SIZE):
                part += 1
                if part > MAX_PARTS:
                    raise UploadError("Archive changed during upload and exceeded the part limit")
                result = client.execute(PART_MUTATION, {**variables, "part": part, "file": None}, chunk)
                if result.get("uploadVersionPart") is not True:
                    raise UploadError(f"API rejected upload part {part}")
                print(f"Uploaded part {part}/{(size + CHUNK_SIZE - 1) // CHUNK_SIZE}", flush=True)
            if source.tell() != size:
                raise UploadError("Archive size changed during upload; refusing to finalize")
        result = client.execute(FINALIZE_MUTATION, {
            **variables, "version": {"changelog": changelog, "stability": stability},
        })
        if result.get("finalizeCreateVersion") is not True:
            raise UploadError("API rejected upload finalization")
        print("Finalization submitted; waiting for server validation/processing", flush=True)
        for attempt in range(POLL_ATTEMPTS):
            if attempt:
                time.sleep(POLL_INTERVAL)
            state = client.execute(STATE_QUERY, variables).get("checkVersionUploadState")
            if state is None:
                continue
            state = object_value(state, "upload state")
            version = state.get("version")
            if version is None:
                continue
            version = object_value(version, "processed version")
            string_value(version.get("id"), "processed version ID")
            if version.get("version") != expected_version or not isinstance(state.get("auto_approved"), bool):
                raise UploadError("Server returned unexpected version metadata after processing")
            return state
        raise UploadError("Timed out waiting for server processing; the version may still be processing")
    except (UploadError, OSError, KeyboardInterrupt) as exc:
        detail = str(exc) if not isinstance(exc, KeyboardInterrupt) else "Interrupted"
        raise UploadError(client.safe_message(
            f"{detail}. Upload session {upload_id} stopped. The API exposes no abort-upload mutation; "
            "deleteVersion is not an upload cleanup API. If finalization was submitted, check the listing "
            "before retrying because processing may still finish. Server-side finalization failures trigger backend cleanup."
        )) from None


class SafeParser(argparse.ArgumentParser):
    def error(self, message):
        # argparse normally echoes unknown arguments, which could be pasted secrets.
        self.exit(2, "Invalid command line; use --help. Credentials belong in FICSIT_TOKEN or the hidden prompt, never command-line arguments.\n")


def main(argv: list[str] | None = None) -> int:
    parser = SafeParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--archive", type=Path, default=ROOT / "dist/InfoScreens.zip")
    listing = parser.add_mutually_exclusive_group()
    listing.add_argument("--mod-id", help="Existing ficsit.app listing ID (must reference InfoScreens)")
    listing.add_argument("--mod-reference", default=MOD, help="Existing listing reference (default: InfoScreens)")
    changelog = parser.add_mutually_exclusive_group(required=True)
    changelog.add_argument("--changelog", help="Release notes, not credentials")
    changelog.add_argument("--changelog-file", type=Path, help="UTF-8 release notes file")
    parser.add_argument("--stability", choices=("alpha", "beta", "release"), default="release")
    try:
        args = parser.parse_args(argv)
    except SystemExit as exc:
        return int(exc.code or 0)
    try:
        archive = args.archive.expanduser().resolve()
        expected_version = validate_archive(archive)
        notes = args.changelog_file.expanduser().read_text(encoding="utf-8") if args.changelog_file else args.changelog
        if not notes.strip():
            raise UploadError("Changelog must not be empty")
        client = Client(credentials())
        state = upload(client, archive, args.mod_id or args.mod_reference, notes, args.stability, expected_version)
        version = object_value(state["version"], "processed version")
        status = "auto-approved" if state["auto_approved"] else "uploaded; awaiting scanning/moderation approval"
        print(client.safe_message(f"InfoScreens {expected_version}: {status}. Version ID: {version['id']}"))
        return 0
    except (OSError, UploadError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("Interrupted before an upload session was established", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
