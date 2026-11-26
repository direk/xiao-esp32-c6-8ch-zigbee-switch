"""Build BOTH antenna variants. Run in the activated ESP-IDF environment."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

VARIANTS = ("external", "internal")


def build_all(root, idf_path):
    firmware = root / "firmware"
    idf = idf_path / "tools/idf.py"
    if not idf.is_file():
        raise SystemExit("ESP-IDF not found; activate export.sh or pass --idf-path")
    for antenna in VARIANTS:
        build = firmware / "build" / antenna
        print(f"Building antenna={antenna}", flush=True)
        subprocess.run([
            sys.executable, str(idf), "-C", str(firmware), "-B", str(build),
            "-D", "IDF_TARGET=esp32c6",
            "-D", f"SDKCONFIG={build / 'sdkconfig'}",
            "-D", f"XIAO_ANTENNA={antenna}", "build",
        ], check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--idf-path", type=Path, default=os.environ.get("IDF_PATH"))
    args = parser.parse_args()
    if args.idf_path is None:
        parser.error("Activate ESP-IDF export.sh or pass --idf-path")
    build_all(Path(__file__).resolve().parents[1], args.idf_path.resolve())
