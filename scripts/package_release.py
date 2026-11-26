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
from build_firmware import build_all, VARIANTS
from ota_image import make_image, index_entry, file_version

p = argparse.ArgumentParser()
p.add_argument("--idf-path", type=Path, required=True)
p.add_argument("--hardware-boot-tested", action="append", choices=VARIANTS, default=[], help="Antenna variant boot-tested on hardware; repeat only for tested variants")
p.add_argument("--sync-repository-ota", action="store_true", help="Update tracked ota/ images and index before publishing")
args = p.parse_args()
root = Path(__file__).resolve().parents[1]
firmware_version = re.search(r'set\(PROJECT_VER "([^"]+)"\)', (root / "firmware/CMakeLists.txt").read_text()).group(1)
build_all(root, args.idf_path.resolve())
release_name = f"xiao-8ch-v{firmware_version}"
staging = tempfile.TemporaryDirectory(prefix="xiao-release-")
out = Path(staging.name) / release_name
out.mkdir(parents=True)
images = []
ota_entries = []
(out / "ota").mkdir()
common = [("bootloader.bin", "bootloader/bootloader.bin", "0x0"),
          ("partition-table.bin", "partition_table/partition-table.bin", "0x8000"),
          ("ota-data-initial.bin", "ota_data_initial.bin", "0x196000")]
for antenna in VARIANTS:
    build = root / "firmware/build" / antenna
    description = json.loads((build / "project_description.json").read_text())
    if description["project_version"] != firmware_version:
        raise SystemExit(f"Wrong firmware version in {build}")
    cache = (build / "CMakeCache.txt").read_text()
    if f"XIAO_ANTENNA:STRING={antenna}\n" not in cache:
        raise SystemExit(f"Wrong antenna selection in {build}")
    commands = json.loads((build / "compile_commands.json").read_text())
    command = next(c["command"] for c in commands if c["file"].endswith("/main/main.c"))
    if f"-DXIAO_OTA_FILE_VERSION={file_version(firmware_version)}U" not in command:
        raise SystemExit(f"Wrong OTA version in {build}")
    for name, source, offset in common:
        data = (build / source).read_bytes()
        if antenna == VARIANTS[0]:
            (out / name).write_bytes(data)
        elif name != "bootloader.bin" and data != (out / name).read_bytes():
            raise SystemExit(f"Antenna variants have inconsistent {name}")
    factory = f"firmware-factory-{antenna}.bin"
    application = f"application-{antenna}.bin"
    subprocess.run([
        sys.executable, "-m", "esptool", "--chip", "esp32c6", "merge_bin",
        "-o", str(out / factory), "--flash_mode", "dio",
        "--flash_size", "4MB", "--flash_freq", "80m",
        "0x0", str(out / "bootloader.bin"),
        "0x8000", str(out / "partition-table.bin"),
        "0x10000", str(build / "xiao_zigbee_8ch.bin"),
        "0x196000", str(out / "ota-data-initial.bin"),
    ], check=True)
    shutil.copy2(build / "xiao_zigbee_8ch.bin", out / application)
    ota_name = f"xiao-8ch-v{firmware_version}-{antenna}.zigbee"
    ota_data = make_image((out / application).read_bytes(), firmware_version, antenna)
    (out / "ota" / ota_name).write_bytes(ota_data)
    ota_entries.append(index_entry(ota_data, firmware_version, antenna, ota_name))
    for name, offset in [(factory, "0x0"), (application, "0x10000")]:
        images.append({"file": name, "antenna": antenna, "offset": offset,
                       "hardware_boot_tested": antenna in args.hardware_boot_tested,
                       "bytes": (out / name).stat().st_size,
                       "sha256": hashlib.sha256((out / name).read_bytes()).hexdigest()})
(out / "ota/index.json").write_text(json.dumps(ota_entries, indent=2) + "\n")
if args.sync_repository_ota:
    shutil.copytree(out / "ota", root / "ota", dirs_exist_ok=True)
for directory in ["zigbee2mqtt", "docs", "tests", "scripts", ".github"]:
    shutil.copytree(root / directory, out / directory, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
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
    "converter_revision": "0.0.7",
    "ota": {"endpoint": 1, "file_version": file_version(firmware_version), "rollback": True,
            "index": "ota/index.json", "images": ota_entries,
            "usb_migration_required_from": "2.1.x", "slots": ["0x10000", "0x1a0000"],
            "preserved_partitions": ["nvs", "zb_storage", "zb_fct"]},
    "hardware_tested": False,
    "hardware_boot_tested": all(v in args.hardware_boot_tested for v in VARIANTS),
    "antenna_variants": list(VARIANTS),
    "routing": "per_cluster_binding_otherwise_action",
    "binding_table_decoder": "raw_zboss_variable_records",
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
    "images": images,
    "usb_components": [{"file": name, "offset": offset, "sha256": hashlib.sha256((out / name).read_bytes()).hexdigest()} for name, _, offset in common],
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
# Standalone release attachments use the same bytes as the complete archive.
for name in [i["file"] for i in images] + ["manifest.json"] + [name for name, _, _ in common]:
    shutil.copy2(out / name, archive.parent / name)
shutil.copy2(root / "zigbee2mqtt/xiao-c6-8ch.mjs", archive.parent / "xiao-c6-8ch.mjs")
for entry in ota_entries:
    shutil.copy2(out / "ota" / entry["fileName"], archive.parent / entry["fileName"])
shutil.copy2(out / "ota/index.json", archive.parent / "ota-index.json")
attachments = [archive.name, "manifest.json", "xiao-c6-8ch.mjs", "ota-index.json"] + [i["file"] for i in images] + [name for name, _, _ in common] + [e["fileName"] for e in ota_entries]
(archive.parent / "SHA256SUMS").write_text("".join(
    f"{hashlib.sha256((archive.parent / name).read_bytes()).hexdigest()}  {name}\n"
    for name in sorted(attachments)))
print(f"Packaged {archive} and both antenna variants")

staging.cleanup()
