"""Map Arduino's copied sketch sources back to the files edited in Zed."""

import json
import os
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/editor-arduino"
DATABASE = ROOT / "build/clangd/compile_commands.json"
SETTINGS = ROOT / ".zed/settings.json"


def map_commands(entries: list, build: Path, sketch: Path) -> list:
    commands = []
    for entry in entries:
        source = Path(entry["file"])
        if not source.is_absolute():
            source = Path(entry["directory"]) / source
        try:
            relative = source.relative_to(build / "sketch")
        except ValueError:
            continue  # Dependencies keep their headers, but are not project source files.
        if relative.name.endswith(".ino.cpp"):
            relative = relative.with_suffix("")
        original = sketch / relative
        if not original.is_file():
            raise ValueError(f"Missing original source: {original}")
        arguments = [str(original) if arg == entry["file"] else arg for arg in entry["arguments"]]
        if str(original) not in arguments:
            raise ValueError(f"Source argument missing: {source}")
        if original.suffix == ".ino":
            arguments[1:1] = ["-x", "c++", "-include", "Arduino.h"]
        commands.append({"directory": entry["directory"], "file": str(original), "arguments": arguments})
    if not commands:
        raise ValueError("No sketch compile commands found")
    return commands


def main() -> None:
    clangd = Path(os.environ["ESP_CLANGD"])
    if not clangd.is_file():
        raise ValueError("Espressif clangd is missing; enter the Nix environment first")
    commands = map_commands(json.loads((BUILD / "compile_commands.json").read_text()),
                            BUILD, ROOT / "firmware/cores3")
    drivers = sorted({entry["arguments"][0] for entry in commands})
    for driver in drivers:
        if not Path(driver).is_file() or Path(driver).name != "xtensa-esp32s3-elf-g++":
            raise ValueError(f"Unexpected compiler; not enabling query-driver: {driver}")
    host_compiler = shutil.which("clang++")
    if not host_compiler:
        raise ValueError("Host clang++ is missing; enter the Nix environment first")
    drivers.append(host_compiler)
    for source in sorted((ROOT / "tests").glob("*.cpp")):
        commands.append({"directory": str(ROOT), "file": str(source), "arguments": [
            host_compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=undefined", "-fno-sanitize-recover=all", "-c", str(source)]})
    # Preserve other settings. Refuse unsupported JSONC instead of silently replacing it.
    settings = json.loads(SETTINGS.read_text()) if SETTINGS.exists() else {}
    binary = settings.setdefault("lsp", {}).setdefault("clangd", {}).setdefault("binary", {})
    arguments = [arg for arg in binary.get("arguments", [])
                 if not arg.startswith(("--query-driver=", "--compile-commands-dir="))]
    arguments.append("--query-driver=" + ",".join(drivers))
    binary.update({"path": str(clangd), "arguments": arguments})
    DATABASE.parent.mkdir(parents=True, exist_ok=True)
    SETTINGS.parent.mkdir(parents=True, exist_ok=True)
    DATABASE.write_text(json.dumps(commands, indent=2) + "\n")
    SETTINGS.write_text(json.dumps(settings, indent=2) + "\n")
    print(f"Configured Zed for {len(commands)} source files. Restart the language server.")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError) as error:
        raise SystemExit(f"Editor setup failed: {error}") from error
