"""Run with the ESP-IDF Python environment (esptool 4.x required)."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

p = argparse.ArgumentParser()
p.add_argument("--idf-path", type=Path, required=True)
p.add_argument("--hardware-boot-tested", action="store_true", help="Mark this exact firmware as boot-tested on hardware")
args = p.parse_args()
root = Path(__file__).resolve().parents[1]
firmware_version = re.search(r'set\(PROJECT_VER "([^"]+)"\)', (root / "firmware/CMakeLists.txt").read_text()).group(1)
release_name = f"xiao-8ch-v{firmware_version}"
staging = tempfile.TemporaryDirectory(prefix="xiao-release-")
out = Path(staging.name) / release_name
out.mkdir(parents=True)
build = root / "firmware/build"
subprocess.run([
    sys.executable, "-m", "esptool", "--chip", "esp32c6", "merge_bin",
    "-o", str(out / "firmware-factory.bin"), "--flash_mode", "dio",
    "--flash_size", "4MB", "--flash_freq", "80m",
    "0x0", str(build / "bootloader/bootloader.bin"),
    "0x8000", str(build / "partition_table/partition-table.bin"),
    "0x10000", str(build / "xiao_zigbee_8ch.bin"),
], check=True)
shutil.copy2(build / "xiao_zigbee_8ch.bin", out / "application.bin")
for directory in ["zigbee2mqtt", "docs", "tests", "scripts", "esphome"]:
    shutil.copytree(root / directory, out / directory, dirs_exist_ok=True)
shutil.copytree(root / "firmware", out / "firmware", dirs_exist_ok=True,
                ignore=shutil.ignore_patterns("build", "managed_components", "sdkconfig", "sdkconfig.old"))
for name in [".gitignore", "README.md", "CHANGELOG.md", "LICENSE", "package.json", "package-lock.json"]:
    shutil.copy2(root / name, out / name)
notices = out / "third-party-licenses"
notices.mkdir(exist_ok=True)
shutil.copy2(args.idf_path / "LICENSE", notices / "ESP-IDF-LICENSE")
for component in ["esp-zboss-lib", "esp-zigbee-lib"]:
    shutil.copy2(root / "firmware/managed_components" / f"espressif__{component}" / "LICENSE", notices / f"{component}-LICENSE")
(notices / "README.txt").write_text(
    "Firmware contains ESP-IDF 5.4.2 and Espressif esp-zigbee-lib / esp-zboss-lib 1.6.9.\n"
    "Third-party components remain under their respective licenses.\n"
    "ESP-IDF source and per-component license notices: https://github.com/espressif/esp-idf/tree/v5.4.2\n"
    "Zigbee SDK: https://github.com/espressif/esp-zigbee-sdk/tree/release/v1.0\n"
)
manifest = {
    "version": firmware_version, "target": "esp32c6", "board": "Seeed Studio XIAO ESP32-C6",
    "esp_idf": "5.4.2", "esp_zigbee_lib": "1.6.9", "esp_zboss_lib": "1.6.9",
    "converter_tested_with": "zigbee-herdsman-converters 26.135.0",
    "converter_revision": "0.0.6",
    "hardware_tested": False,
    "hardware_boot_tested": args.hardware_boot_tested,
    "antenna": "external",
    "routing": "per_cluster_binding_otherwise_action",
    "binding_priority": {"per_channel": True, "default": "ON", "persistent": True,
                         "off_behavior": "direct_control_and_action"},
    "channels": 8,
    "channel_gpio": [0, 1, 2, 21, 22, 23, 19, 20],
    "uart": {"tx_gpio": 16, "rx_gpio": 17, "baud": 115200, "format": "8N1",
             "protocol": "ascii_buttons_v2", "hold_timeout_ms": 10000,
             "dedicated_task": True, "queue_depth": 8, "commands_per_tick": 1,
             "get_configuration": True, "reply_max_bytes": 2063,
             "info_status": True, "request_ids": "increasing_uint32", "dedup_depth": 16,
             "receive_error_recovery": True, "reset": "zigbee_network_and_bindings",
             "usb_commands": True, "sessions": "per_port", "logs_default": False,
             "log_control": "per_port_LOG_0_1", "log_prefix": "#LOG "},
    "modes": 13,
    "images": [{"file": name, "offset": offset, "bytes": (out / name).stat().st_size,
                "sha256": hashlib.sha256((out / name).read_bytes()).hexdigest()}
               for name, offset in [("firmware-factory.bin", "0x0"), ("application.bin", "0x10000")]],
}
(out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
files = sorted(f for f in out.rglob("*") if f.is_file() and f.name != "SHA256SUMS")
(out / "SHA256SUMS").write_text("".join(f"{hashlib.sha256(f.read_bytes()).hexdigest()}  {f.relative_to(out).as_posix()}\n" for f in files))
archive = root / "dist" / f"{release_name}.zip"
archive.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
    for f in sorted(out.rglob("*")):
        if f.is_file():
            z.write(f, Path(out.name) / f.relative_to(out))
staging.cleanup()
print(f"Packaged {archive}")
