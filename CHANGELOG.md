# Changelog — SP7TQ fork of OpenGD77 for the RT3S / MDUV380

This is an independent fork maintained by **Marcin SP7TQ**, based on the
[EA4IPW fork](https://github.com/mangelajo/opengd77-rt3s-experiments) of the
upstream [OpenGD77](https://www.opengd77.com/) R20260131 source. Features
marked *(untested)* build and pass host tests but still need on-air checking.

## Unreleased

* **Volume indicator in the header** (VFO / channel screens): 4 bars left of the GPS label following the volume knob, `x` when muted. The pop-up bar on knob turns is still *Options → Display → Visual volume*.

## R20260131-SP7TQ.1 — 2026-11-19

First release under the SP7TQ tag. `.gla` language packs changed (new menu strings): flash the ones from this release.

* **Main menu: APRS RX and SSTV entries** (also quickkey targets); the APRS monitor moved out of the RSSI screen, `9` there still opens it.
* **DTMF decoder**: shows received digits on the RSSI monitor.
* **CW (Morse) decoder**: decodes repeater IDs and CW tones on FM, with WPM and tone shown.
* **SSTV receive** (main menu → SSTV, also a quickkey): Robot 36/72, Martin M1/M2, Scottie S1/S2, PD 90/120/180 (ISS: PD 120), VIS auto-detect, picture 160x120 on screen, line by line. Host tests with an encoder.
* **APRS messages**: messages addressed to you are shown and acknowledged automatically (rate limited: 6 acks / 10 min, alphanumeric ids only), and you can reply with canned texts.
* **APRS background RX** setting is kept after power off; DTMF sequences are notified in the background.
* **Station → GPS waypoint** (`*` in the stations / radar view), waypoint drawn on the radar.
* **GPS log → GPX**: `tools/opengd77_gps_log_to_gpx.py` downloads the upstream NMEA log (GPS mode On+Log) over USB.
* `make build-all` no longer deletes the bins of the variants it has already built.
* DM-1701 / RT-84 (120-line screen): the SSTV status line covers the top of the picture.
* README and changelog: the fork is now developed independently, maintained by SP7TQ.

## R20260131-EA4IPW.3 — 2026-11-19

The first release from SP7TQ.

* **APRS receive (AFSK 1200)**, on the RSSI screen with key `9`:
  * own decoder on the HR-C6000 I2S RX audio: 4 slicers for FM twist, DPLL, HDLC, CRC;
  * views: frames, raw view (TNC2 text + hex dump), stations (distance, bearing, age) and radar (north up);
  * positions: uncompressed, compressed, Mic-E, objects and items;
  * background RX on `SK1+4` or a quickkey, with notifications.
  * Frame decoding is confirmed on air. The stations, radar and background RX *(untested)*.
* **Radiosonde hunting**: 401/403/405 MHz band-scope presets, waypoint entry from the keypad, and fox-hunt.
* **VFO sweep**:
  * `SK1+3` band presets;
  * fix: `SK1+0` was swallowed by the "any key stops the scan" check.
* **GPS**:
  * waypoint page (arrow, distance, bearing back);
  * trip computer (distance, moving time, average and maximum speed, altitude gain and loss).
* **RSSI screen**: fox-hunt mode with a pitch beep that follows the signal, max-hold and the GPS bearing to the maximum.
* **Quickkeys menu** and new quickkey actions; waterfall peak hold, strongest-signal marker and tune to peak.
* Polish how-to (`PLANS/rt3s_instrukcja_pl.md`), credits in the firmware info screen.

## R20260131-EA4IPW.1 / .2 — by EA4IPW

* VFO band scope and scrolling waterfall (toggled with `*`).
* Custom-data area auto-recovery and reset tool.
* Command-line Makefile and containerised build.
* GitHub Actions build matrix and releases.
* Release bins named after the hardware (RT3S, RT3S 10W, RT-84/DM-1701).
