"""Package only the application image and its OTA manifest, never a flash dump."""

import hashlib
import json
import re
import sys
from pathlib import Path


def package(binary_path: Path, output: Path) -> None:
    root = Path(__file__).resolve().parents[1]
    header = (root / "firmware/cores3/firmware_version.h").read_text()

    def constant(name: str) -> str:
        match = re.search(rf'{name}\[\] = "([^"]+)";', header)
        if not match:
            raise ValueError(f"Missing {name}")
        return match[1]

    version = constant("kFirmwareVersion")
    if not re.fullmatch(r"(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})", version):
        raise ValueError("Use a version such as 0.1.0 (no v prefix)")
    binary = binary_path.read_bytes()
    if not 288 <= len(binary) <= 0x300000 or binary[0] != 0xE9:
        raise ValueError("Not a valid-sized ESP application image")
    # esp_image_header_t.chip_id: ESP32-S3 is 9, little endian.
    if int.from_bytes(binary[12:14], "little") != 9:
        raise ValueError("The application image must target ESP32-S3")
    manifest = {
        "schema": 1,
        "board": constant("kFirmwareBoard"),
        "partition": constant("kFirmwarePartition"),
        "version": version,
        "file": "firmware.bin",
        "size": len(binary),
        "sha256": hashlib.sha256(binary).hexdigest(),
    }
    output.mkdir(parents=True, exist_ok=True)
    (output / "firmware.bin").write_bytes(binary)
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Release v{version}: {output / 'firmware.bin'} and {output / 'manifest.json'}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: package_release.py APP_BIN OUTPUT_DIR")
    package(Path(sys.argv[1]), Path(sys.argv[2]))
