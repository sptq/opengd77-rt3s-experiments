# opengd77-rt3s-experiments — SP7TQ fork

**Independent fork** of the [OpenGD77](https://www.opengd77.com/) firmware
for the **Retevis RT3S** (GPS) and the rest of the MDUV380 / DM1701 family
(TYT MD-UV380, Baofeng DM-1701, Retevis RT-84, MD-380UV).

Maintainer: **Marcin SP7TQ** — <https://github.com/sptq/opengd77-rt3s-experiments>

This repository started as a fork of
[mangelajo/opengd77-rt3s-experiments](https://github.com/mangelajo/opengd77-rt3s-experiments)
by EA4IPW (OpenGD77 R20260131 source mirror, command-line build, VFO band
scope / waterfall, release tooling). From R20260131-EA4IPW.3 on it is
**developed independently**: new features are added here first, and they are
not guaranteed to be merged back into that repository or into upstream
OpenGD77. It is **not affiliated with the OpenGD77 project**.

What this fork adds on the radio (details in
[Fork features](#fork-features-rt3s--mduv380), history in
[`CHANGELOG.md`](CHANGELOG.md)):

* **APRS receive** — on-radio AFSK 1200 decoder: frame list, raw TNC2/hex
  view, station list with distance and bearing, radar, background RX with
  notifications.
* **Band scope / waterfall** in VFO mode, peak hold, tune to peak, band presets.
* **Fox hunting** on the RSSI screen (pitch beep, max-hold with GPS position).
* **Radiosonde hunting** helpers (401–405 MHz presets, keypad waypoint).
* **GPS** waypoint navigation page and trip computer.
* **Quickkeys** menu and new quickkey actions.
* Polish translation and a Polish how-to
  ([`PLANS/rt3s_instrukcja_pl.md`](PLANS/rt3s_instrukcja_pl.md)).

Testers are welcome: download a release, try it and open an issue.
**Experimental firmware — flash at your own risk** and back up your codeplug
first.

## What's here

| Path | Contents | Origin |
|---|---|---|
| `MDUV380_firmware/` | OpenGD77 R20260131 source release, verbatim | upstream |
| `MDUV380_firmware/Makefile` | Reconstructed from `.cproject` for command-line builds outside STM32CubeIDE | local addition |

The build wrapper scripts (Containerfile, `build.sh`) that drive a
[podman](https://podman.io/)-based Ubuntu container build live **outside this
repository** in the maintainer's working directory.

## License — **non-commercial only**

The OpenGD77 source code is distributed under a **modified BSD-3-Clause
license with an explicit non-commercial restriction** (see
[`license.txt`](license.txt) and
[`MDUV380_firmware/tools/license.txt`](MDUV380_firmware/tools/license.txt)).
Relevant clause:

> 4. Use of this source code or binary releases for commercial purposes is
> strictly forbidden. This includes, without limitation, incorporation in a
> commercial product or incorporation into a product or project which allows
> commercial use.

This restriction inherits to any fork or derivative work, including this one.

Bundled third-party components retain their own licenses:

- **CMSIS** — Apache-2.0
- **STM32F4xx HAL Driver** — BSD-3-Clause
- **ST USB Device Library** — ST SLA0044
- **FreeRTOS** — MIT (Amazon)
- **SEGGER RTT** — SEGGER custom BSD-like

See the `LICENSE`/`LICENSE.txt` files inside each subdirectory.

## Upstream and credits

- Upstream release zips: <https://www.opengd77.com/downloads/releases/>
- Community-maintained user guide:
  <https://github.com/LibreDMR/OpenGD77_UserGuide>
- Original authors and contributors are listed in
  the upstream README ([`UPSTREAM_README.md`](UPSTREAM_README.md))
  (Kai DG4KLU, Roger VK3KYY, Daniel F1RMB, Alex DL4LEX, Colin G4EML and
  many others).
- Fork maintainer: **Marcin SP7TQ** (RT3S features: APRS RX, GPS pages,
  VFO sweep improvements, fox and radiosonde hunting, quickkeys, Polish docs).
- Original fork and build tooling: EA4IPW, on which this
  repository is based.

If you are looking for the canonical firmware: **use
[opengd77.com](https://www.opengd77.com/)**, not this repo.

## Building

The firmware was historically built with STM32CubeIDE on Linux/Windows.
The added `Makefile` lets you build with a stock `arm-none-eabi-gcc` outside
the IDE.

```sh
# inside an Ubuntu / Debian environment with the ARM toolchain installed
cd MDUV380_firmware
make            # produces build/OpenGD77_MDUV380.bin
```

Other variants:

```sh
make build PLATFORM=MDUV380 VARIANT=UV380_PLUS_10W   # 10W mod
make build PLATFORM=RT84_DM1701 VARIANT=DM1701        # Retevis RT-84 / Baofeng DM-1701
make build-all                                        # all three; bins land in dist/
```

The build runs natively on macOS as well — `codec_cleaner` is now a portable
Python script (`MDUV380_firmware/tools/codec_cleaner.py`), so no podman
container is needed for this step.

> ⚠️ The build relies on the AMBE codec being patched in at **flash time**
> from a separate "donor" firmware. This repository contains zero AMBE
> code or data. See the OpenGD77 docs for the codec/flashing workflow.

## Cutting a release

Releases are tagged `R<upstream-date>-SP7TQ.<n>`, which anchors each binary
to the upstream OpenGD77 snapshot it was built from.

**Tag format:** `R<YYYYMMDD>-SP7TQ.<N>`
- `<YYYYMMDD>` — the date of the upstream OpenGD77 release the fork is based
  on (matches the upstream `R<date>` directories on opengd77.eu).
- `<N>` — incrementing build number against that upstream base, starting
  at `1`.

Older tags from before the fork went independent keep their
`R20260131-EA4IPW.<N>` names (the last one is `R20260131-EA4IPW.3`).

GitHub Actions do not run on this fork yet, so releases are built locally
with `make build-all CONTAINER_ENGINE=docker` (bins and `SHA256SUMS` in
`MDUV380_firmware/dist/`) and uploaded with `gh release create`. The CI
workflow accepts both tag schemes if Actions get enabled.

**How to cut a release:**

```sh
# on the commit you want to ship
git switch main
git pull sptq main

# tag, annotated, with a short note
git tag -a R20260131-SP7TQ.1 -m "Short release note"
git push sptq R20260131-SP7TQ.1
```

CI then:

1. Runs the full build matrix (MDUV380, MDUV380 +10W, RT84/DM-1701).
2. Downloads all `.bin` artifacts and generates a `SHA256SUMS` file.
3. Creates a public GitHub Release at the tag, with auto-generated
   release notes (commit list since the previous tag) and the bins
   attached.

To remove a bad release: delete the tag both locally and on the
remote (`git tag -d <tag>; git push origin :refs/tags/<tag>`) and
delete the corresponding Release in the GitHub UI.

## Fork features (RT3S / MDUV380)

The RT3S has PTT, SK1, SK2, the channel knob and the keypad. There is no
top/orange button. Firmware "ORANGE" events are emulated with **SK1 + GREEN**.

### Quickkeys menu — *Options → Quickkeys*

Upstream already supports ten **SK2 + digit** quickkeys, but there is no screen
that shows what each slot does. This menu lists slots `0`–`9` with their
current assignment and lets you change them:

| Key | Action |
|---|---|
| ▲ / ▼ or a digit | select slot |
| channel knob | cycle the assignment |
| GREEN | save changed slots (marked `*`) and exit |
| RED | discard changes and exit |

On top of the menu shortcuts (RSSI, Last heard, Zones, Contacts, Radio info,
Satellite, GPS, APRS), a slot can run a fork **action**:

| Action | Equivalent built-in combo |
|---|---|
| APRS beacon | `SK1 + 2` |
| APRS on/off | `SK1 + 1` |
| Day/Night | `SK1 + RED` |
| Band scope | long `#` in VFO (also works from the Channel screen) |
| Scope: to peak | `SK1 + 0` while sweeping |

Actions are stored as out-of-range contact indices. Upstream firmware ignores
these, so going back to upstream with the same codeplug is harmless.

### VFO band scope / waterfall

Start it with long `#` in VFO mode. `*` toggles between waterfall and bars.

* **Peak hold**: a dot above each bar marks the recent maximum. It decays a
  little every sweep.
* **Strongest-signal marker**: a small triangle at the top of the scope marks
  the strongest carrier of the last sweep.
* **`SK1 + 0`**: re-centre the VFO on that carrier, rounded to the VFO step.
  The waterfall history slides along instead of being cleared.
* **`SK1 + 3`**: jump to the next band preset. The presets are 144.800 APRS,
  145.500 2 m, 401/403/405 MHz radiosondes, 433.925 LPD, 438.800 70 cm
  repeaters and 446.100 PMR446. Each
  press goes to the next preset above the current frequency and wraps around
  after the last one.

### Fox-hunt mode — *RSSI screen*

Press `#` on the RSSI screen to toggle it.

* A short beep every 0.6 s. Its pitch rises with the signal: 300 Hz at
  −130 dBm, 2.4 kHz at −40 dBm.
* **Max-hold**: the strongest reading so far is shown as `Max: … dBm` and as a
  marker on the S-meter.
* With a GPS fix, the position of that maximum is stored. The screen then shows
  the distance and bearing from where you are now (`Max @ 120m 045 deg`).
* `0` resets the max-hold.

### Waypoint page — *GPS menu, second-to-last page*

* Press **GREEN** to mark the current position (needs a GPS fix). The waypoint
  is kept in RAM until power-off.
* The page shows the distance and bearing back to the waypoint, plus an arrow:
  * when moving, the arrow is relative to your direction of travel (ahead = up);
  * when standing still, it is north-up.
* Within 5 m of the waypoint the arrow turns into a dot.
* **`#`** opens a keypad editor for the waypoint, for example to enter a
  radiosonde landing prediction from SondeHub:
  * coordinates are in decimal degrees;
  * `#` types the decimal point and `*` toggles the minus sign (S / W);
  * ▲ / ▼ switch between the lat and lon fields, RED deletes a character;
  * GREEN sets the waypoint.

### Radiosonde hunting

The radio cannot decode RS41 / M20 telemetry. Its only digital audio path to
the MCU runs at 8 kHz through a voice-band filter, while the sondes send
4800 / 9600 bd GFSK. You can still find a sonde:

1. Find the carrier: VFO → long `#` → `SK1 + 3` to the 401/403/405 MHz presets.
   Use `SK2 + ▲` to widen the span to 2 MHz.
2. Tune to it with `SK1 + 0`.
3. Enter the landing prediction as a waypoint: GPS → waypoint page → `#`.
4. Close in with fox-hunt mode on the RSSI screen.

### APRS receive — *RSSI screen, key `9`; background: `SK1+4`*

A software AFSK 1200 demodulator runs on the FM RX audio the HR-C6000
sends over I2S (8 kHz): mark/space correlators, four slicers covering
de-/pre-emphasis tone twist, DPLL bit clock, NRZI, HDLC, AX.25 FCS check.
It only owns the I2S while nothing else needs it (beeps, voice prompts,
DMR audio and TX take over and the capture restarts by itself).

The monitor (`9` on the RSSI screen) has six views, cycled with `#`:

* **frames** — last 8 frames; `*` shows the selected one raw (full TNC2
  line + hex dump),
* **stations** — last heard stations / objects with distance, bearing, age,
  speed and course,
* **radar** — north-up plot around your position, auto range; the GPS
  waypoint is drawn as a square, `*` makes the selected station the waypoint,
* **messages** — APRS messages to you (acknowledged automatically, about 2 s
  later), bulletins and your own messages with their state (`...` waiting,
  `ACK`, `REJ`, `FAIL`, `noTX`). `GREEN` replies to the selected message, or
  starts a message to the station selected in the stations / radar view,
  with a canned text (`QSL`, `73`, `Roger`...). Sent messages are retried
  every 30 s, 3 times at most. Your call is the radio name + the SSID of the
  channel APRS configuration, TX goes to its QSY frequency when set,
* **DTMF** — decoded DTMF digits (Goertzel, twist / energy checks),
* **CW** — Morse decoder for FM (repeater IDs), with WPM and tone.

Positions are decoded from uncompressed, compressed and Mic-E reports,
objects and items. Own position: GPS fix, else the location from the
settings. `SK1+4` on the VFO / channel screen (or the *APRS RX on/off*
quickkey action) keeps decoding in the background and pops a short
notification (`SP7ABC-9 12.3km`) per frame, DTMF sequences and APRS
messages; the setting survives a power cycle. The decoders and the parser
are hardware independent and have host tests
(`make -C MDUV380_firmware/tests/aprs_rx`).

### GPS log to GPX

With the GPS mode set to *On+Log* the firmware records NMEA into the last
2 MB of the SPI flash. `MDUV380_firmware/tools/opengd77_gps_log_to_gpx.py
-o track.gpx` downloads it over USB (pyserial) and writes one GPX track
segment per logging session.

### Trip computer — *GPS menu, last page*

The radio collects these in the background, whatever screen is shown. They
are kept in RAM only:

* distance travelled;
* moving time;
* average and maximum speed;
* altitude gain and loss.

Only movement above about 2 km/h is counted, so GPS jitter while you stand
still is ignored. Altitude changes under 5 m are ignored as noise. A
**long GREEN** press resets the trip.

Plans and the on-radio test checklist are in [`PLANS/`](PLANS/). A Polish how-to is in [`PLANS/rt3s_instrukcja_pl.md`](PLANS/rt3s_instrukcja_pl.md).

## Local experiments

Branches under `feature/*` may contain in-progress experiments (for
example, converting the existing VFO sweep mode in
`MDUV380_firmware/application/source/user_interface/uiVFOMode.c` into a
proper scrolling waterfall display). These are not endorsed by upstream
and may be broken at any commit.

— 73, Marcin SP7TQ
