"""Local HTTP protocol tests; never contact ficsit.app or publish a version."""

from __future__ import annotations

from contextlib import contextmanager, redirect_stderr, redirect_stdout
from email import policy
from email.parser import BytesParser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch
import zipfile

from Tools import upload_ficsit as uploader


DESCRIPTOR = {
    "Version": 0,
    "VersionName": "0.0.1",
    "SemVersion": "0.0.1",
    "GameVersion": "513885",
    "RequiredOnRemote": True,
    "Modules": [{"Name": "InfoScreens", "Type": "Runtime"}],
}
MOD = {
    "id": "listing-id",
    "mod_reference": "InfoScreens",
    "approved": True,
    "ai_use_disclosure": {"disclosure_type": "ai_usage", "message": "Assisted development"},
    "network_use_disclosure": "No external network requests",
}


def write_archive(path: Path, prefix: str = "", descriptor: dict | None = None) -> None:
    with zipfile.ZipFile(path, "w") as archive:
        for target, library in (("Windows", "InfoScreens.dll"), ("LinuxServer", "InfoScreens.so")):
            root = f"{prefix}{target}/"
            archive.writestr(root + "InfoScreens.uplugin", json.dumps(descriptor or DESCRIPTOR))
            archive.writestr(root + "Content/Paks/InfoScreens.pak", b"cooked fixture")
            archive.writestr(root + "Binaries/" + library, b"native fixture")


@contextmanager
def api_fixture(responses: list[dict | tuple[int, dict]]):
    requests: list[dict] = []

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_POST(self):
            body = self.rfile.read(int(self.headers["Content-Length"]))
            content_type = self.headers["Content-Type"]
            request = {"authorization": self.headers.get("Authorization"), "body": body}
            if content_type.startswith("multipart/form-data"):
                message = BytesParser(policy=policy.default).parsebytes(
                    f"Content-Type: {content_type}\r\nMIME-Version: 1.0\r\n\r\n".encode() + body
                )
                fields = {part.get_param("name", header="content-disposition"): part.get_payload(decode=True)
                          for part in message.iter_parts()}
                request["operations"] = json.loads(fields["operations"])
                request["map"] = json.loads(fields["map"])
                request["chunk"] = fields["0"]
                request["field_order"] = list(fields)
            else:
                request["operations"] = json.loads(body)
            requests.append(request)
            if responses:
                response = responses.pop(0)
                status, payload = response if isinstance(response, tuple) else (200, response)
            else:
                status, payload = 500, {"errors": [{"message": "Unexpected extra request"}]}
            encoded = json.dumps(payload).encode()
            self.send_response(status)
            if status in (301, 302, 307, 308):
                self.send_header("Location", "http://127.0.0.1:1/credential-leak")
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(encoded)))
            self.end_headers()
            self.wfile.write(encoded)

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        yield uploader.Client("secret-session", endpoint=f"http://127.0.0.1:{server.server_port}/v2/query"), requests
    finally:
        server.shutdown()
        server.server_close()
        worker.join()


class ArchiveTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.archive = Path(self.directory.name) / "InfoScreens.zip"

    def test_accepts_merged_archive_without_repacking(self):
        write_archive(self.archive, descriptor={**DESCRIPTOR, "SemVersion": "0.0.2", "VersionName": "0.0.2"})
        self.assertEqual(uploader.validate_archive(self.archive), "0.0.2")

    def test_rejects_install_wrapper_before_network(self):
        write_archive(self.archive, "FactoryGame/Mods/InfoScreens/")
        with self.assertRaisesRegex(uploader.UploadError, "merged|Windows/|wrapper"):
            uploader.validate_archive(self.archive)

    def test_rejects_mismatched_version_fields(self):
        write_archive(self.archive, descriptor={**DESCRIPTOR, "SemVersion": "0.0.2"})
        with self.assertRaisesRegex(uploader.UploadError, "matching release"):
            uploader.validate_archive(self.archive)

    def test_rejects_platform_descriptor_mismatch(self):
        write_archive(self.archive)
        with zipfile.ZipFile(self.archive, "a") as archive:
            archive.writestr("LinuxServer/Other.uplugin", json.dumps(DESCRIPTOR))
        with self.assertRaisesRegex(uploader.UploadError, "descriptor"):
            uploader.validate_archive(self.archive)

    def test_rejects_different_descriptor_bytes(self):
        with zipfile.ZipFile(self.archive, "w") as archive:
            archive.writestr("Windows/InfoScreens.uplugin", json.dumps(DESCRIPTOR))
            archive.writestr("LinuxServer/InfoScreens.uplugin", json.dumps(DESCRIPTOR, indent=2))
        with self.assertRaisesRegex(uploader.UploadError, "identical"):
            uploader.validate_archive(self.archive)

    def test_rejects_missing_cooked_or_native_content(self):
        with zipfile.ZipFile(self.archive, "w") as archive:
            for target in ("Windows", "LinuxServer"):
                archive.writestr(f"{target}/InfoScreens.uplugin", json.dumps(DESCRIPTOR))
        with self.assertRaisesRegex(uploader.UploadError, "cooked|native"):
            uploader.validate_archive(self.archive)


class ProtocolTests(unittest.TestCase):
    def test_chunked_upload_and_async_polling_use_official_protocol(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "InfoScreens.zip"
            # An incompressible payload spanning the official 10,000,000-byte boundary.
            payload = b"a" * 10_000_000 + b"last chunk"
            archive.write_bytes(payload)
            responses = [
                {"data": {"getMe": {"id": "user-id"}}},
                {"data": {"getModByIdOrReference": MOD}},
                {"data": {"createVersion": "upload-id"}},
                {"data": {"uploadVersionPart": True}},
                {"data": {"uploadVersionPart": True}},
                {"data": {"finalizeCreateVersion": True}},
                {"data": {"checkVersionUploadState": None}},
                {"data": {"checkVersionUploadState": {"auto_approved": False,
                    "version": {"id": "version-id", "version": "0.0.1"}}}},
            ]
            with api_fixture(responses) as (client, requests), patch.object(uploader.time, "sleep"):
                result = uploader.upload(client, archive, "InfoScreens", "Release notes", "beta", "0.0.1")
            self.assertEqual(result["version"], {"id": "version-id", "version": "0.0.1"})
            self.assertFalse(result["auto_approved"])
            self.assertTrue(all(request["authorization"] == "secret-session" for request in requests))
            chunks = [request for request in requests if "chunk" in request]
            self.assertEqual([len(request["chunk"]) for request in chunks], [10_000_000, 10])
            self.assertEqual(b"".join(request["chunk"] for request in chunks), payload)
            for number, request in enumerate(chunks, start=1):
                self.assertEqual(request["map"], {"0": ["variables.file"]})
                self.assertEqual(request["field_order"], ["operations", "map", "0"])
                self.assertEqual(request["operations"]["variables"], {
                    "modId": "listing-id", "versionId": "upload-id", "part": number, "file": None})
            self.assertEqual(requests[5]["operations"]["variables"]["version"],
                             {"changelog": "Release notes", "stability": "beta"})
            self.assertEqual(responses, [])

    def test_auth_failure_is_clear_and_redacts_token(self):
        with api_fixture([{"errors": [{"message": "user not logged in secret-session"}]}]) as (client, requests):
            with self.assertRaises(uploader.UploadError) as caught:
                uploader.upload(client, Path("unused.zip"), "InfoScreens", "Notes", "release", "0.0.1")
        self.assertNotIn("secret-session", str(caught.exception))
        self.assertIn("session", str(caught.exception))
        self.assertEqual(len(requests), 1)

    def test_http_auth_failure_is_clear(self):
        with api_fixture([(401, {"errors": [{"message": "expired"}]})]) as (client, requests):
            with self.assertRaisesRegex(uploader.UploadError, "session|authentication"):
                client.execute("query { getMe { id } }", {})
        self.assertEqual(len(requests), 1)

    def test_redirect_is_refused_before_forwarding_authorization(self):
        with api_fixture([(302, {})]) as (client, requests):
            with self.assertRaisesRegex(uploader.UploadError, "redirect refused"):
                client.execute("query { getMe { id } }", {})
        self.assertEqual(len(requests), 1)

    def test_invalid_response_fails_without_dumping_server_payload(self):
        with api_fixture([{"data": ["secret-session"]}]) as (client, requests):
            with self.assertRaisesRegex(uploader.UploadError, "Invalid API response") as caught:
                client.execute("query { getMe { id } }", {})
        self.assertNotIn("secret-session", str(caught.exception))
        self.assertEqual(len(requests), 1)

    def test_missing_listing_backend_error_explains_website_setup(self):
        with api_fixture([
            {"data": {"getMe": {"id": "user-id"}}},
            {"errors": [{"message": "ent: mod not found"}], "data": {"getModByIdOrReference": None}},
        ]) as (client, requests):
            with self.assertRaisesRegex(uploader.UploadError, "create|ficsit.app|disclosures"):
                uploader.upload(client, Path("unused.zip"), "InfoScreens", "Notes", "release", "0.0.1")
        self.assertEqual(len(requests), 2)

    def test_wrong_listing_never_starts_upload(self):
        cases = [
            ({**MOD, "mod_reference": "Other"}, "InfoScreens"),
            (None, "listing"),
            ({**MOD, "approved": False}, "approved"),
            ({**MOD, "ai_use_disclosure": None}, "disclosure"),
            ({**MOD, "network_use_disclosure": None}, "disclosure"),
        ]
        for listing, message in cases:
            with self.subTest(listing=listing), api_fixture([
                {"data": {"getMe": {"id": "user-id"}}},
                {"data": {"getModByIdOrReference": listing}},
            ]) as (client, requests):
                with self.assertRaisesRegex(uploader.UploadError, message):
                    uploader.upload(client, Path("unused.zip"), "InfoScreens", "Notes", "release", "0.0.1")
                self.assertEqual(len(requests), 2)

    def test_failed_part_is_not_finalized_or_deleted(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "InfoScreens.zip"
            archive.write_bytes(b"fixture")
            with api_fixture([
                {"data": {"getMe": {"id": "user-id"}}},
                {"data": {"getModByIdOrReference": MOD}},
                {"data": {"createVersion": "upload-id"}},
                {"data": {"uploadVersionPart": False}},
            ]) as (client, requests):
                with self.assertRaisesRegex(uploader.UploadError, "upload-id"):
                    uploader.upload(client, archive, "InfoScreens", "Notes", "release", "0.0.1")
            self.assertEqual(len(requests), 4)

    def test_processing_error_is_reported_not_success(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "InfoScreens.zip"
            archive.write_bytes(b"fixture")
            with api_fixture([
                {"data": {"getMe": {"id": "user-id"}}},
                {"data": {"getModByIdOrReference": MOD}},
                {"data": {"createVersion": "upload-id"}},
                {"data": {"uploadVersionPart": True}},
                {"data": {"finalizeCreateVersion": True}},
                {"errors": [{"message": "multi-target plugin contains different .uplugin files"}],
                 "data": {"checkVersionUploadState": None}},
            ]) as (client, requests):
                with self.assertRaisesRegex(uploader.UploadError, "different .uplugin"):
                    uploader.upload(client, archive, "InfoScreens", "Notes", "release", "0.0.1")
            self.assertEqual(len(requests), 6)

    def test_polling_timeout_does_not_delete_version(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "InfoScreens.zip"
            archive.write_bytes(b"fixture")
            with api_fixture([
                {"data": {"getMe": {"id": "user-id"}}},
                {"data": {"getModByIdOrReference": MOD}},
                {"data": {"createVersion": "upload-id"}},
                {"data": {"uploadVersionPart": True}},
                {"data": {"finalizeCreateVersion": True}},
                {"data": {"checkVersionUploadState": None}},
            ]) as (client, requests), patch.object(uploader, "POLL_ATTEMPTS", 1):
                with self.assertRaisesRegex(uploader.UploadError, "Timed out|timed out"):
                    uploader.upload(client, archive, "InfoScreens", "Notes", "release", "0.0.1")
            self.assertEqual(len(requests), 6)


class CredentialTests(unittest.TestCase):
    def test_env_session_is_not_echoed(self):
        with patch.dict(uploader.os.environ, {"FICSIT_TOKEN": "secret-session"}):
            self.assertEqual(uploader.credentials(), "secret-session")

    def test_no_hidden_prompt_in_noninteractive_session(self):
        with patch.dict(uploader.os.environ, {}, clear=True), patch.object(uploader.sys.stdin, "isatty", return_value=False):
            with self.assertRaisesRegex(uploader.UploadError, "FICSIT_TOKEN|terminal"):
                uploader.credentials()

    def test_hidden_prompt_returns_session_without_echoing_it(self):
        output = io.StringIO()
        with patch.dict(uploader.os.environ, {}, clear=True), \
                patch.object(uploader.sys.stdin, "isatty", return_value=True), \
                patch.object(uploader.getpass, "getpass", return_value="secret-session"), \
                redirect_stdout(output), redirect_stderr(output):
            self.assertEqual(uploader.credentials(), "secret-session")
        self.assertNotIn("secret-session", output.getvalue())

    def test_rejects_bearer_prefix_and_header_injection(self):
        for token in ("Bearer secret-session", "secret\nsession"):
            with self.subTest(token=token), patch.dict(uploader.os.environ, {"FICSIT_TOKEN": token}):
                with self.assertRaisesRegex(uploader.UploadError, "raw SMR session"):
                    uploader.credentials()

    def test_token_cannot_be_passed_as_cli_argument_or_echoed_by_argparse(self):
        output = io.StringIO()
        with redirect_stderr(output), redirect_stdout(output):
            self.assertEqual(uploader.main(["--token", "never-print-this"]), 2)
        self.assertNotIn("never-print-this", output.getvalue())


if __name__ == "__main__":
    unittest.main()
