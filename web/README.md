# Project website and web flasher

A static site: project description, screen pictures, and a firmware flasher that runs in the
browser (WebUSB, Chrome / Edge / Brave on a desktop). There is no server-side code.

## Publish a release on the site

```sh
python3 web/build_site.py --release R20260131-SP7TQ.3   # needs gh, downloads the release assets
# or: python3 web/build_site.py --assets DIR --tag NAME  (DIR holds the .bin files and OpenGD77_Languages.zip)
```

This copies `docs/screens/*.png` to `web/img/` and writes `web/firmware/` (firmware files,
language packs and `manifest.json`, read by the flasher). `web/firmware/` is not committed.
Then upload the whole `web/` directory to the server, for example:

```sh
rsync -av --delete --exclude test/ --exclude README.md --exclude package.json web/ user@server:/var/www/emcom/
```

The site has to be served over **HTTPS** (browsers only allow WebUSB on secure pages;
`http://localhost` works for testing: `python3 -m http.server -d web 8000`).

## How the flasher works

`js/flasher-core.js` is a port of `MDUV380_firmware/tools/opengd77_stm32_firmware_loader.py`:
it merges the `.gla` language pack, copies the AMBE codec from the user's MD-9600 V26.45 donor
firmware (checked by SHA-256, kept only in the browser's IndexedDB, never uploaded), encrypts
the image for the model and splits it into the same DFU blocks. `js/dfu.js` sends them with the
same DFU command sequence as the Python loader. `js/ciphers.js` is generated from the loader.

`test/compare.sh` checks that the JavaScript sends exactly the same bytes as the Python loader:

```sh
web/test/compare.sh MDUV380_firmware/dist LANG_DIR [MD9600-CSV(2571V5)-V26.45.bin]
```

The "Own files" section of the flasher takes a `.bin` from a local build and a matching `.gla`,
for testing builds without a release.

Fonts: Barlow and Barlow Condensed (SIL Open Font License), self-hosted in `fonts/`.
