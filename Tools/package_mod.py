"""Build InfoScreens for Windows clients and Linux dedicated servers.

Activate ~/satisfactory-modding/activate.sh first. The default project is the
isolated CL513885 SDK, not the older shared starter. --preflight changes nothing.
"""

from __future__ import annotations

import argparse
import copy
from contextlib import contextmanager
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
MOD = "InfoScreens"
SDK_COMMIT = "8eebc9e659f0b2ff4392d2f8cf6e2cff60c8ea49"
GAME_CL = "513885"
WWISE_VERSION = "2025.1.6.9117.4248"
ARTIFACTS = ("Windows", "LinuxServer")


def metadata(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def preflight(engine: Path, project: Path) -> None:
    failures: list[str] = []
    if not project.is_file():
        failures.append(f"Missing configured FactoryGame project: {project}")
    elif metadata(project).get("EngineAssociation") != "5.6.1-CSS":
        failures.append("Project must use custom engine association 5.6.1-CSS")
    version = project.parent / "Source/FactoryGame/currentVersion.txt"
    if not version.is_file() or version.read_text().strip() != GAME_CL:
        failures.append(f"SDK headers must target CL{GAME_CL}; use upstream {SDK_COMMIT}")
    engine_version = engine / "Engine/Build/Build.version"
    if not engine_version.is_file():
        failures.append(f"Missing custom engine: {engine}")
    else:
        data = metadata(engine_version)
        if (data.get("MajorVersion"), data.get("MinorVersion"), data.get("PatchVersion"), data.get("Changelist")) != (5, 6, 1, 83):
            failures.append("Expected installed UE5.6.1-CSS changelist83")
    wwise = project.parent / "Plugins/Wwise/Wwise.uplugin"
    if not wwise.is_file() or metadata(wwise).get("VersionName") != WWISE_VERSION:
        failures.append(f"Integrate Wwise {WWISE_VERSION} into this isolated project (CLI integration version2025.1.6.4248)")
    include = project.parent / "Plugins/WwiseSoundEngine/ThirdParty/include/AK/AkWwiseSDKVersion.h"
    if not include.is_file():
        failures.append("Missing integrated Wwise SDK headers")
    msvc = os.environ.get("UE_WINE_MSVC")
    if not msvc or not Path(msvc).is_dir():
        failures.append("Activate the installed build environment: source ~/satisfactory-modding/activate.sh")
    if not os.environ.get("WINEPREFIX"):
        failures.append("WINEPREFIX must identify the isolated build/MSVC prefix, never a game prefix")
    if failures:
        raise RuntimeError("Preflight failed:\n- " + "\n- ".join(failures))


@contextmanager
def exclusive(path: Path):
    try:
        descriptor = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    except FileExistsError as error:
        raise RuntimeError(f"Build lock already exists; do not steal it: {path}") from error
    try:
        with os.fdopen(descriptor, "w") as handle:
            handle.write(f"InfoScreens build PID {os.getpid()}\n")
        yield
    finally:
        path.unlink()


def run(command: list[str], log: Path) -> None:
    print(f"Running {Path(command[0]).name}; full output: {log}", flush=True)
    with log.open("w", encoding="utf-8") as output:
        result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, check=False)
    if result.returncode:
        lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
        errors = [line for line in lines if any(marker in line.lower() for marker in ("error:", "fatal", "exception", "failed"))]
        print("\n".join(errors[-25:] or lines[-20:]), file=sys.stderr)
        raise RuntimeError(f"{Path(command[0]).name} exited {result.returncode}; see {log}")


def validate_archive(path: Path, platform: str) -> None:
    with zipfile.ZipFile(path) as archive:
        descriptor = json.loads(archive.read(f"{MOD}.uplugin"))
        if descriptor.get("GameVersion") != GAME_CL or not descriptor.get("RequiredOnRemote"):
            raise RuntimeError(f"Wrong game/remote compatibility in {path}")
        manifests = [name for name in archive.namelist() if name.endswith(".modules")]
        targets = {"FactoryGameEGS", "FactoryGameSteam"} if platform == "Windows" else {"FactoryServer"}
        found: set[str] = set()
        for name in manifests:
            manifest = json.loads(archive.read(name))
            library = manifest.get("Modules", {}).get(MOD)
            if manifest.get("BuildId") != "SML" or not library:
                raise RuntimeError(f"Invalid Alpakit native manifest: {path}:{name}")
            if str(Path(name).parent / library) not in archive.namelist():
                raise RuntimeError(f"Missing native binary for {name}")
            found.add(Path(name).name.split("-")[0])
        if not targets.issubset(found):
            raise RuntimeError(f"Missing native targets {targets - found} in {path}")
        if not any(name.endswith(".pak") for name in archive.namelist()):
            raise RuntimeError(f"Missing cooked InfoScreens assets in {path}")


def build_shipping(engine: Path, project: Path, log: Path) -> None:
    # UAT puts -ubtargs inside each -Target; WaitMutex must be a top-level UBT option.
    targets = (("FactoryGameEGS", "Win64"), ("FactoryGameSteam", "Win64"), ("FactoryServer", "Linux"))
    run([str(engine / "Engine/Build/BatchFiles/Linux/Build.sh"), "-WaitMutex",
         *[f'-Target={target} {platform} Shipping -Project="{project}"' for target, platform in targets]], log)


def package(engine: Path, project: Path, output: Path) -> None:
    preflight(engine, project)
    source = ROOT / "Mods" / MOD
    stage = project.parent / "Mods" / MOD
    if stage.exists() or stage.is_symlink():
        if not stage.is_symlink() or stage.resolve() != source.resolve():
            raise RuntimeError(f"Refusing to replace unrelated SDK plugin: {stage}")
    build = ROOT / "build"
    build.mkdir(exist_ok=True)
    # This also serializes use of the shared MSVC Wine prefix by this workflow.
    prefix = Path(os.environ["WINEPREFIX"]).resolve()
    with exclusive(prefix / ".InfoScreens-build.lock"), exclusive(project.parent / ".InfoScreens-build.lock"):
        for other in (project.parent / ".FluidSloshFix-native-probe.lock",):
            if other.exists():
                raise RuntimeError(f"Another native probe owns this project: {other}")
        created_stage = not stage.is_symlink()
        if created_stage:
            stage.symlink_to(source, target_is_directory=True)
        try:
            run([str(engine / "Engine/Build/BatchFiles/Linux/Build.sh"), "FactoryEditor", "Linux", "Development",
                 f"-Project={project}", "-WaitMutex"], build / "editor.log")
            run([str(engine / "Engine/Binaries/Linux/UnrealEditor-Cmd"), str(project),
                 "-run=pythonscript", f"-script={ROOT / 'Tools/create_screen_assets.py'}",
                 "-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities", "-unattended", "-NullRHI", "-nosound"],
                build / "assets.log")
            for asset in ("Meshes/SM_InfoScreenFrame.uasset", "UI/T_InfoScreenIcon.uasset", "Materials/M_InfoScreenFrame.uasset"):
                if not (source / "Content" / asset).is_file():
                    raise RuntimeError(f"Asset generation did not save {asset}")
            build_shipping(engine, project, build / "shipping.log")
            run([str(engine / "Engine/Build/BatchFiles/RunUAT.sh"), f"-ScriptsForProject={project}",
                 "PackagePlugin", f"-Project={project}", f"-dlcname={MOD}", "-merge", "-skipbuild", "-server",
                 "-clientconfig=Shipping", "-serverconfig=Shipping", "-platform=Win64", "-serverplatform=Linux",
                 "-nocompileeditor", "-installed"], build / "package.log")
            archived = project.parent / "Saved/ArchivedPlugins" / MOD
            for platform in ARTIFACTS:
                validate_archive(archived / f"{MOD}-{platform}.zip", platform)
            output.mkdir(parents=True, exist_ok=True)
            for name in (f"{MOD}-Windows.zip", f"{MOD}-LinuxServer.zip", f"{MOD}.zip"):
                shutil.copy2(archived / name, output / name)
            for platform in ARTIFACTS:
                install_archive = output / f"{MOD}-{platform}-Install.zip"
                with zipfile.ZipFile(output / f"{MOD}-{platform}.zip") as source_archive:
                    with zipfile.ZipFile(install_archive, "w") as destination:
                        for entry in source_archive.infolist():
                            installed_entry = copy.copy(entry)
                            installed_entry.filename = f"FactoryGame/Mods/{MOD}/{entry.filename}"
                            destination.writestr(installed_entry, source_archive.read(entry))
            print(f"Validated packages in {output}; extract the matching -Install.zip into the game/server folder.")
        finally:
            if created_stage:
                stage.unlink()


def main() -> int:
    root = Path.home() / "satisfactory-modding"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, default=root / "UnrealEngineCSS")
    parser.add_argument("--project", type=Path, default=root / "info-screens-workspace/sdk/FactoryGame.uproject")
    parser.add_argument("--output", type=Path, help="Output directory (default: dist/<SemVersion>)")
    parser.add_argument("--preflight", action="store_true")
    args = parser.parse_args()
    try:
        engine, project = args.engine.expanduser().resolve(), args.project.expanduser().resolve()
        if args.preflight:
            preflight(engine, project)
            print(f"Ready for CL{GAME_CL} build")
        else:
            version = metadata(ROOT / "Mods" / MOD / f"{MOD}.uplugin")["SemVersion"]
            output = args.output if args.output is not None else ROOT / "dist" / version
            package(engine, project, output.expanduser().resolve())
        return 0
    except (OSError, RuntimeError, ValueError, zipfile.BadZipFile, KeyError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
