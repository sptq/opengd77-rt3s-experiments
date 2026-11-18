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
| 1b | Waterfall: free cursor on the knob, band presets (2 m / 70 cm / PMR446) | *idea* | knob is already used for retune in sweep |
| 3 | Quickkeys menu + quickkey actions | **done** | see `rt3s_quickkeys_and_waterfall.md` |
| 2 | Fox-hunt mode on the RSSI screen (pitch ∝ RSSI, max-hold with GPS position and bearing) | **done** | `menuRSSIScreen.c`, `#` toggles; untested on hardware |
| 4 | GPS track log to SPI flash (16 MB), GPX export over USB/CPS, "back to start" screen | *idea* | needs a free flash region and a CPS-side command |
| 5 | Replay the last DMR transmission locally (store AMBE frames in flash) | *idea* | ~27 B / 60 ms, so minutes fit easily |
| 6 | FM DTMF **decoding** (show received digits, simple selective call) | *idea* | only DTMF TX exists today |
| 7 | Roaming++: auto-switch to the nearest repeater on low RSSI, bearing to the current repeater | *idea* | `SK1`-free; uses existing distance sort |
| 8 | GPS navigation screen (speed, course, altitude, bearing to a waypoint/contact) | *idea* | `menuGPS.c` |
| 9 | Satellite: automatic Doppler retune during a pass, cross-band hints | *idea* | `menuSatelliteScreen.c` |
| 10 | APRS RX decode (AFSK 1200): station list with distance/bearing, radar view | *idea* | CPU budget with the codec running is the main risk |
| 11 | DMR SMS send/receive | *idea* | needs data-burst handling in `HR-C6000.c` |
| 12 | DMR GPS position (LRRP / Brandmeister) | *idea* | |
| 13 | Unify MD2017 / MD9600 trees | *idea* | `unify_sourcecodes.md` |
| 14 | Host-side unit tests (AX.25 encoder, Maidenhead, satellite maths, roaming sort, quickkey encoding) in CI | *idea* | |
| 15 | Ship a binary named for the RT3S in releases + flashing notes in the README | *idea* | |
