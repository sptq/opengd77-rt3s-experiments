# RT3S idea backlog

Feature ideas for running this firmware on the Retevis RT3S (`PLATFORM=MDUV380`).
They are based on the R20260131 code and on the RT3S hardware:

* STM32F405 + HR-C6000 (DMR) + AT1846S
* 160×128 colour LCD
* GPS
* 16 MB SPI flash (W25Q128, part id `0x4018`)
* PTT, SK1, SK2, channel knob, keypad. **No top/orange button**: `BUTTON_ORANGE`
  is emulated with SK1 + GREEN.

Status legend: **done**, **in progress**, *idea*.

## Already in the firmware (upstream + fork)

* DMR Tier II (CC/TS, TG lists, private and all calls, Talker Alias, filters), FM
  CTCSS/DCS, tone scan, 1750 Hz, DTMF send, VOX, dual watch, scan.
* GPS screen, Maidenhead locator, APRS beaconing (TX only, SmartBeaconing),
  roaming (zone sorted by repeater distance), satellite predictions with Doppler.
* Colour themes (day/night), voice prompts, 20+ languages (Polish included),
  hotspot (MMDVMHost/BlueDV), CPS over USB.
* Fork: VFO band scope + waterfall, custom-data area auto-recovery/reset.

## Backlog

| # | Idea | Status | Notes |
|---|---|---|---|
| 1 | Waterfall: peak hold, strongest-signal marker, tune to peak | **done** | see `rt3s_quickkeys_and_waterfall.md` |
| 1b | Waterfall: band presets (`SK1+3`) | **done** | free cursor still an idea; knob is used for retune |
| 1c | Fix: `SK1+0` in sweep was swallowed by the "any key stops the scan" check | **done** | handled before that check now |
| 8c | GPS trip computer page (distance, moving time, avg/max speed, altitude gain/loss) | **done** | `functions/gpsTrip.c`, RAM only, long GREEN resets |
| 3 | Quickkeys menu + quickkey actions | **done** | see `rt3s_quickkeys_and_waterfall.md` |
| 2 | Fox-hunt mode on the RSSI screen (pitch ∝ RSSI, max-hold with GPS position and bearing) | **done** | `menuRSSIScreen.c`, `#` toggles; untested on hardware |
| 4 | GPS track log to SPI flash (16 MB), GPX export over USB/CPS, "back to start" screen | **done** | upstream already logs NMEA (GPS mode On+Log, last 2 MB); `tools/opengd77_gps_log_to_gpx.py` reads it over the CPS link; "back to start" = waypoint page |
| 5 | Replay the last DMR transmission locally (store AMBE frames in flash) | *idea* | ~27 B / 60 ms, so minutes fit easily |
| 6 | FM DTMF **decoding** (show received digits, simple selective call) | **done** | RSSI monitor DTMF view + background notification; selective call still an idea |
| 6b | CW decoder (repeater IDs on FM) | **done** | `toneRxDecoders.c`, host tests |
| 7 | Roaming++: auto-switch to the nearest repeater on low RSSI | *idea* | upstream already has auto re-sort on distance and a bearing notification, so little is left |
| 8 | GPS navigation: waypoint page (GREEN marks, arrow/distance/bearing back) | **done** | `menuGPS.c`; speed/course/alt already on the Direction page; waypoint is RAM-only; untested on hardware |
| 8b | Waypoint: persist in settings, pick a contact/channel location as target | *idea* | |
| 9 | Satellite: automatic Doppler retune during a pass, cross-band hints | *idea* | `menuSatelliteScreen.c` |
| 10 | APRS RX decode (AFSK 1200): station list with distance/bearing, radar view | **done** (2026-11-19) | AFSK/AX.25 decoder on I2S RX audio; RSSI `9` monitor: frames / raw (TNC2+hex) / stations (dist, bearing) / radar views (`#`); positions: uncompressed, compressed, Mic-E, objects, items; background RX on `SK1+4` / quickkey with notifications. Background setting persisted (bit 29); APRS messages with auto ACK and canned replies; station -> GPS waypoint |
| 16 | Radiosonde hunting: 401/403/405 MHz scope presets + keypad waypoint entry + fox-hunt | **done** | no RS41/M20 decode: only 8 kHz voice-band audio reaches the MCU, the sondes send 4800/9600 bd GFSK |
| 17 | SSTV receive (Robot, Martin, Scottie, PD) on the RSSI monitor | **done** | `sstvRx.c`, encoder-based host test; untested on air |
| 11 | DMR SMS send/receive | *idea* | needs data-burst handling in `HR-C6000.c` |
| 12 | DMR GPS position (LRRP / Brandmeister) | *idea* | |
| 13 | Unify MD2017 / MD9600 trees | *idea* | `unify_sourcecodes.md` |
| 14 | Host-side unit tests (AX.25 encoder, Maidenhead, satellite maths, roaming sort, quickkey encoding) in CI | *idea* | |
| 15 | Ship a binary named for the RT3S in releases + flashing notes in the README | *idea* | |
