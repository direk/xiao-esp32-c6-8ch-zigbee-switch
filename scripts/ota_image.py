"""Create standard Zigbee OTA Upgrade images containing one ESP-IDF application."""
import argparse
import hashlib
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
MAGIC = 0x0BEEF11E
HEADER_LENGTH = 60


def identity():
    text = (ROOT / 'firmware/main/ota_client.h').read_text()
    def value(name):
        return int(re.search(r'^#define ' + name + r' (0x[0-9a-fA-F]+|[0-9]+)$', text, re.M).group(1), 0)
    return value('XIAO_OTA_MANUFACTURER'), value('XIAO_OTA_IMAGE_BASE'), value('XIAO_OTA_HARDWARE_VERSION')


def file_version(version):
    match = re.fullmatch(r'(\d+)\.(\d+)\.(\d+)', version)
    if not match or any(int(x)>255 for x in match.groups()):
        raise ValueError('OTA release must be a stable major.minor.patch version with byte-sized components')
    major, minor, patch = map(int, match.groups())
    return major << 24 | minor << 16 | patch << 8


def make_image(application, version, antenna):
    if antenna not in ('external', 'internal'):
        raise ValueError('Unknown antenna')
    if len(application)<256 or application[0]!=0xe9:
        raise ValueError('Not an ESP-IDF application image')
    if application[48:80].split(b'\0',1)[0].decode('ascii')!=version:
        raise ValueError('Application version differs from OTA version')
    manufacturer, base, hardware = identity()
    image_type = base + (antenna=='external')
    label = f'XIAO8 {version} {antenna}'.encode('ascii')
    if len(label)>32:
        raise ValueError('OTA header label too long')
    total = HEADER_LENGTH+6+len(application)
    header = struct.pack('<IHHHHHIH32sIHH', MAGIC, 0x0100, HEADER_LENGTH, 4,
                         manufacturer, image_type, file_version(version), 2,
                         label.ljust(32,b'\0'), total, hardware, hardware)
    return header + struct.pack('<HI', 0, len(application)) + application


def index_entry(data, version, antenna, name):
    manufacturer, base, hardware = identity()
    return {
        'manufacturerCode': manufacturer, 'imageType': base+(antenna=='external'),
        'fileVersion': file_version(version), 'fileName': name, 'fileSize': len(data),
        'modelId': 'XIAO-C6-8CH', 'manufacturerName': ['DIY-XIAO'],
        'hardwareVersionMin': hardware, 'hardwareVersionMax': hardware,
        'sha512': hashlib.sha512(data).hexdigest(),
        'url': f'https://github.com/direk/xiao-esp32-c6-8ch-zigbee-switch/releases/download/v{version}/{name}',
        'releaseNotes': f'https://github.com/direk/xiao-esp32-c6-8ch-zigbee-switch/releases/tag/v{version}',
    }


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--application',type=Path,required=True)
    parser.add_argument('--version',required=True)
    parser.add_argument('--antenna',choices=['external','internal'],required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    args.output.write_bytes(make_image(args.application.read_bytes(),args.version,args.antenna))
