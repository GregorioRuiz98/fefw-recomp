"""Export an original PNG to a multi-size Windows icon and textual icon source.

Authoring tool only: requires Pillow. Normal builds decode the checked-in JSON
directly and need neither Pillow nor the source PNG.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('png', type=Path)
    parser.add_argument('--ico', type=Path, required=True)
    parser.add_argument('--json', type=Path, required=True)
    args = parser.parse_args()
    from PIL import Image
    with Image.open(args.png) as source:
        source.convert('RGBA').save(args.ico, format='ICO',
            sizes=[(n, n) for n in (16, 20, 24, 32, 48, 64, 128, 256)])
    icon = args.ico.read_bytes()
    payload = {'name': 'Woven Shield', 'license': 'GPL-3.0-or-later',
        'provenance': 'Original project artwork; see APP-ICON.md. No game assets.',
        'source_png_sha256': hashlib.sha256(args.png.read_bytes()).hexdigest(),
        'ico_sha256': hashlib.sha256(icon).hexdigest(),
        'sizes': [16, 20, 24, 32, 48, 64, 128, 256],
        'ico_base64': base64.b64encode(icon).decode('ascii')}
    args.json.write_text(json.dumps(payload, indent=2) + '\n', encoding='ascii')
    print(f'Exported 8 sizes; ICO {len(icon)} bytes')


if __name__ == '__main__':
    main()
