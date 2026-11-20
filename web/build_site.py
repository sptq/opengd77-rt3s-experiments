#!/usr/bin/env python3
"""Fills web/ with the screen pictures and the firmware files of one release, ready to upload.

    python3 web/build_site.py --release R20260131-SP7TQ.3          # downloads the assets with `gh`
    python3 web/build_site.py --assets DIR                          # DIR holds the release assets

Writes web/img/*.png (from docs/screens) and web/firmware/ (firmware .bin files, language
packs, OpenGD77_Languages.zip, manifest.json read by the flasher). Then upload the whole web/
directory to the server (HTTPS is required for WebUSB).
"""
import argparse
import datetime
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(ROOT, 'web')
REPO = 'sptq/opengd77-rt3s-experiments'

# Release asset -> what the flasher shows. platform is the loader's -m model.
MODELS = [
    {'id': 'rt3s', 'file': 'OpenGD77_MDUV380_RT3S.bin', 'platform': 'MD-UV380',
     'name': 'Retevis RT3S / TYT MD-UV380', 'tested': True},
    {'id': 'rt3s10w', 'file': 'OpenGD77_MDUV380_RT3S_10W.bin', 'platform': 'MD-UV380',
     'name': 'RT3S 10 W / MD-UV380 10 W (UV380 Plus)', 'tested': False},
    {'id': 'rt84', 'file': 'OpenGD77_RT84_DM1701.bin', 'platform': 'DM-1701',
     'name': 'Retevis RT-84 / Baofeng DM-1701', 'tested': False},
]

LANGUAGE_NAMES = {
    'Catalan': 'Català', 'Croatian': 'Hrvatski', 'Czech': 'Čeština', 'Danish': 'Dansk',
    'Dutch': 'Nederlands', 'Finnish': 'Suomi', 'French': 'Français', 'German': 'Deutsch',
    'Hungarian': 'Magyar', 'Italian': 'Italiano', 'Polish': 'Polski', 'PortuguesBrazil': 'Português (Brasil)',
    'Portuguese': 'Português', 'Romanian': 'Română', 'Slovenian': 'Slovenščina', 'Spanish': 'Español',
    'Swedish': 'Svenska', 'Turkish': 'Türkçe', 'Japanese': '日本語',
}


def sha256(path):
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


def copy_screens():
    src = os.path.join(ROOT, 'docs', 'screens')
    dst = os.path.join(WEB, 'img')
    os.makedirs(dst, exist_ok=True)
    n = 0
    for name in sorted(os.listdir(src)):
        if name.endswith('.png'):
            shutil.copy2(os.path.join(src, name), os.path.join(dst, name))
            n += 1
    print(f'{n} screen pictures -> web/img')


def release_info(tag):
    try:
        out = subprocess.run(['gh', 'release', 'view', tag, '-R', REPO, '--json', 'publishedAt,url'],
                             check=True, capture_output=True, text=True).stdout
        info = json.loads(out)
        return info['publishedAt'][:10], info['url']
    except (OSError, subprocess.CalledProcessError, KeyError, ValueError):
        return datetime.date.today().isoformat(), f'https://github.com/{REPO}/releases/tag/{tag}'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument('--release', help='release tag to download with gh')
    src.add_argument('--assets', help='directory with the release assets')
    ap.add_argument('--tag', help='release name shown on the page (with --assets)')
    args = ap.parse_args()

    copy_screens()

    tmp = None
    if args.release:
        tmp = tempfile.mkdtemp()
        subprocess.run(['gh', 'release', 'download', args.release, '-R', REPO, '-D', tmp,
                        '-p', '*.bin', '-p', 'OpenGD77_Languages.zip', '-p', 'SHA256SUMS'], check=True)
        assets, tag = tmp, args.release
    else:
        assets, tag = args.assets, (args.tag or os.path.basename(os.path.abspath(args.assets)))

    fw_dir = os.path.join(WEB, 'firmware')
    shutil.rmtree(fw_dir, ignore_errors=True)
    os.makedirs(os.path.join(fw_dir, 'languages'))

    models = []
    for m in MODELS:
        path = os.path.join(assets, m['file'])
        if not os.path.isfile(path):
            print(f'missing {m["file"]}, skipped', file=sys.stderr)
            continue
        shutil.copy2(path, fw_dir)
        models.append(dict(m, size=os.path.getsize(path), sha256=sha256(path)))
    if not models:
        sys.exit('no firmware files found')

    languages = []
    zpath = os.path.join(assets, 'OpenGD77_Languages.zip')
    if os.path.isfile(zpath):
        shutil.copy2(zpath, fw_dir)
        with zipfile.ZipFile(zpath) as z:
            for name in sorted(z.namelist()):
                base = os.path.basename(name)
                if not base.endswith('.gla'):
                    continue
                data = z.read(name)
                with open(os.path.join(fw_dir, 'languages', base), 'wb') as f:
                    f.write(data)
                key = base[:-4]
                languages.append({'id': key, 'name': LANGUAGE_NAMES.get(key, key), 'file': f'languages/{base}'})
    languages.sort(key=lambda l: l['name'])

    date, url = release_info(tag) if args.release else (datetime.date.today().isoformat(), '')
    manifest = {
        'release': tag, 'date': date, 'notes': url,
        'models': models, 'languages': languages,
        'languagesZip': 'OpenGD77_Languages.zip' if languages else None,
    }
    with open(os.path.join(fw_dir, 'manifest.json'), 'w', encoding='utf-8') as f:
        json.dump(manifest, f, ensure_ascii=False, indent=1)
    print(f'{tag}: {len(models)} firmware files, {len(languages)} language packs -> web/firmware')

    if tmp:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    main()
