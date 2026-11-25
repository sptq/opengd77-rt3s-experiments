# Changelog — SP7TQ fork of OpenGD77 for the RT3S / MDUV380

This is an independent fork maintained by **Marcin SP7TQ**, based on the
[EA4IPW fork](https://github.com/mangelajo/opengd77-rt3s-experiments) of the
upstream [OpenGD77](https://www.opengd77.com/) R20260131 source. Features
marked *(untested)* build and pass host tests but still need on-air checking.

## R20260131-SP7TQ.7 — 2026-11-25

* **Fix: the radio froze with voice prompts on** (issue #21). With *Options → Sound → Prompt* set to *Voice* / *Voice L2* / *Voice L3*, highlighting a fork menu entry (PACE, Position, Coverage, Radio check, APRS RX, Event log, SSTV, and *Reset* / *Quickkeys* at the end of *Options*) froze the radio until the battery was pulled. These names have no recording in the voice pack; the firmware read past the voice pack table of contents. They are now spelled letter by letter, and playback refuses prompt numbers outside the table. Radios with *Beep* or *Silent* were not affected.

## R20260131-SP7TQ.6 — 2026-11-25

* **DMR SMS** (new main menu tile *DMR SMS*, envelope icon): cleartext text messages over DMR, receive and send. Send and receive work through a BrandMeister hotspot (service numbers 262993 / 262994 answer); radio to radio with a stock TYT is not tested yet *(untested)*. The data path is ported from [dondch/OpenGD77-AES256](https://github.com/dondch/OpenGD77-AES256), without any encryption (encrypted traffic is not allowed on the amateur bands; encrypted messages are ignored).
  * **Receive**: stock TYT / Motorola IP format (UDP 4007, TMS, UTF-16), the TYT "DMR format" text and Defined / Raw Short Data from the network (BrandMeister sends its replies as confirmed rate 3/4 short data). Polish and other accented letters are shown without diacritics. A confirmed private message is acknowledged on a simplex (DMO) channel; through a hotspot BrandMeister repeats its reply 3 times and the copies are dropped.
  * **Inbox** in the SPI flash (kept over a power cycle, the newest 16..32 messages): list newest first, `GREEN` reads, `0` deletes, long `0` then `GREEN` deletes all. New message: card, beep, `MSG IN` event log entry and `SMS n` (or `MSG a+b` with APRS) on the EMCOM main screen.
  * **Send**: `#` new message, `GREEN` on a message replies. Recipient: DMR ID on the keypad, `*` private / group, `#` the channel contact. Text: quick texts `WX` / `RPT` / `INBOX` (BrandMeister services), the APRS canned replies or typed text (no diacritics yet). Sent as an unconfirmed data call (IP / UDP / TMS, rate 1/2); refused like PTT on FM, RX only, TX inhibit or out of band channels. Sent messages show `>` in the list (`!` if not sent) and go to the event log as `MSG OUT`.
  * `*` on the list: receive diagnostics (data bursts, blocks, completed packets, drops), for reports.
* **Decoders** (new main menu tile): the DTMF and CW decoders moved out of APRS RX to their own screen, with a third view:
  * **STQC selcall** (issue #18) *(untested on a real signal)*: receive only decoder for the Digitex / Platan five-tone selcall (fire brigade sirens, SW-xx CB selcall): address and command as numbers and the tones as sent, 5 last calls, event log entry and a background notification like DTMF. Tested on synthetic calls (5 dB SNR, +-2 % tone error, 70..130 ms tones). No encoder or transmit path.
* **Clock**: *Options → Display → Clock* Zulu / Local / Loc+Zulu (the EMCOM footer shows `14:05Z`, `15:05` or `15:05/14:05Z`); *Set time* opens *Radio infos → Time*, where the EMCOM style now shows how to type the time (`HHMMSS`) and date (`YYYYMMDD`).
* **RSSI screen**: `UP` / `DOWN` set the FM squelch (same card as on the channel screen); the EMCOM strip shows `SQL n`.
* **DMR RX AGC and minimum volume** (PR #23, desertblade, issue #22): the hardware volume no longer changes with the AGC; AGC levels -9..+12 dB with learned gain per station and smooth changes. Users who had AGC on hear a different level (new curve).
* **EMCOM screens** (PR #25, desertblade, issue #24): optional distance to the repeater / channel (*Options → Display → Show dist*), expanded DMR caller, standby and TX layouts, local / Zulu clock and battery voltage options in the status bar.
* **Fixes**:
  * Keypad / PTT lock hung the radio with the EMCOM style (issue #14).
  * VFO step of millions of kHz after *Channel → VFO* with a codeplug written by a CPS (issue #20).
  * EMCOM header: the volume indicator was hidden since PR #25 (it followed the volume pop-up option).
  * EMCOM footer: the battery time left now also shows next to `RX` / `SCAN` / locks when it fits.
  * APRS RX: key hints in the bottom bar of every view.
* CI: the build target can be chosen for manual runs (PR #17, desertblade).
* Memory: the FreeRTOS heap went from 20 KB to 16 KB (5.5 KB were never used), the free main RAM is used by DMR SMS; the SMS and Decoders screens are linked after the codec. 8 KB at the end of the GPS log area of the SPI flash now hold the SMS inbox.

## R20260131-SP7TQ.5 — 2026-11-22

* **VFO power in the channel details**: the Power row showed `N/A` in the VFO; it now shows the master power (e.g. `Master 1W`) and `LEFT` / `RIGHT` change it at once, like `SK2` + `UP` / `DOWN` on the VFO screen.
* **Saved waypoints** *(untested)*: 8 named waypoints kept in the flash (survive a power cycle). GPS waypoint page `*` or the new *Waypoints* quickkey action opens the list: name, distance, compass point and an arrow (north up) from the GPS fix, `--` without a fix, `>` on the current target. `GREEN` navigates (sets the waypoint and opens the waypoint page), `*` saves the current position (`WP1`..`WP8`), `#` types lat / lon (the keypad entry of the waypoint page), `RIGHT` renames (A-Z, 0-9, `-`), `0` then `GREEN` deletes. APRS RX: long `*` on a station also saves it under its callsign. `tools/opengd77_coverage.py --waypoints wpts.gpx` exports them.
* **Comms schedule (sked) reminders** *(untested)*: up to 4 skeds — every 15 / 20 / 30 min, 1..12 h or daily, from a UTC time, 1..60 min long, optionally with a PACE slot or the current channel. 1 min before: `SKED in 1 min` card and a beep; at the start: `SKED n NOW <channel>` card, a beep and `SYS SKED n` in the event log; on the channel / VFO screen `GREEN` on that card tunes to the sked channel (never by itself). Silent in a blackout; needs the UTC clock (GPS or CPS). *Sked* screen from PACE `#` or the *Sked* quickkey action: `GREEN` edit (`UP` / `DOWN` field, `LEFT` / `RIGHT` value, `GREEN` save, `RED` cancel), `0` then `GREEN` delete. The PACE strip (classic: key bar) counts down to the next sked; PACE key bar `1-4 GO * STO 0 CLR # SKED`.
* **Blackout fixes**: voice prompts are no longer queued while the blackout is on (a low battery warning could still speak) and the queue is cleared when it is turned off; the settings are written to the flash from a copy with your backlight values (the live values used to switch to full brightness for a moment); a backlight value set in *Options → Display* during the blackout is kept even when it equals a blackout value (5 %, *Buttons*), and a settings reset during the blackout keeps the defaults.
* Firmware layout: the field tool screens are linked after the AMBE codec (the code area before it was full).
* **Blackout / silent mode** *(untested)*: `SK1 + 5` on the channel / VFO screen (or the new *Blackout* quickkey action) toggles it, with a `BLACKOUT ON` / `OFF` card. While on: the backlight stays off until a key press, then lights at 5 % for the normal timeout (10 s if the timeout is *never*); no key beeps, alert / RX / TX beeps or voice prompts; RX / TX LEDs off. TX (incl. the 1750 Hz burst and DTMF), receive audio, SOS beacons and visual alerts work as before. The EMCOM footer shows a solid `BLK` chip. Not kept over a power cycle: the user backlight values are what is written to the flash, and a backlight setting changed during the blackout is kept.
* **Battery time left** *(untested)*: `~3h20` style estimate from the voltage trend of the last 30 minutes (rest voltage per 2.5 min, TX samples left out, smoothed and counting down; `--` for the first 15 min, while charging or after a charger step). On the *Radio infos* battery page (under the voltage) and on the right of the EMCOM footer when no SOS / SCAN / TX OFF / lock / TX item is shown. Classic main screen: not shown (no room in the header).
* **Net control + QSO log** *(untested)*: new main menu tile *Net control*. `GREEN` starts a net; every station heard on DMR (callsign, else the DMR ID) or APRS checks in by itself. Rows: station, check-in time (UTC), time since last heard, `IN` (solid chip) / `QUIET` (outlined: not heard for the quiet limit; logged once with a warning card and a beep, re-armed when heard again). `*` adds an FM check-in `FM#n`, `GREEN` on it marks it heard now; `GREEN` on another row logs a QSO (report 59); `#` quiet limit 10 / 15 / 30 / 5 min; `0` then `GREEN` ends the net; `RED` leaves, the net keeps running in the background. 18 stations in RAM.
  * **Last heard**: `*` logs a QSO with the selected station (report 59), *QSO logged* card; EMCOM key bar (`GRN CALL  * QSO  RED EXIT`, one row less).
  * **Quickkeys**: Position, Coverage, Radio check and Net control now work as quickkeys (their menu numbers did not fit the quickkey field and opened another screen); they are stored as quickkey actions, also when set with `SK2` + digit on the screen or its tile.
  * **APRS objects / items** are no longer checked in to a net (only stations heard themselves).
  * **Event log**: two new record types, `QSO` (`SP7ABC 59`, `2601234 59`, `FM#3 59`) and `NET` (`START <channel>`, `IN <station>`, `QUIET <station>`, `END <n>`), also in `opengd77_event_log.py`.
* **UI refresh, stage 2 (EMCOM styles)** *(untested)*: the field tool screens get the title bar with a counter, a status strip, state chips (a word and a shape as well as a colour) and a key bar with the real keys; classic style unchanged, keys unchanged.
  * **PACE**: the slot the radio is on in the strip (`ON P`, `OFF PLAN`, `VFO`), rows with name, frequency, role and `ON` / `EMPTY` / `DELETED`.
  * **Radio check**: live `RX` / `IDLE`, S units and over length in the strip; the last report big with `OK` / `WEAK` / `POOR`; older reports with the same chips.
  * **Event log**: date and type of the selected entry in the strip, the whole text above the key bar (8x16 in *EMCOM contr*).
  * **Coverage**: `REC` / `NO FIX` / `STOP` / `FULL`, interval and memory in the strip, points in the title, level word `GOOD` / `WEAK` / `GAP`; in mono / contr weak samples are striped and gaps a tick on the graph.
  * **Position**: source, fix, satellites and HDOP in the strip, UTC in the title; in *EMCOM contr* the MGRS easting and northing in 16x32 (to be read out over the radio).
  * **SSTV**: idle screen in the same layout (pictures saved in the title, `DOPPLER` chip); over a picture only the 8 px bar above it: state, mode, frequency or `DOPPLER`, `row/120` on a block filling up with the progress, then `SAVED n/8`.
  * Clear confirmations (`0`, then `GREEN`) show `CLEAR ...?  GRN YES  ANY NO` in the key bar.
* **UI refresh, stage 1 (EMCOM styles)** *(untested)*: classic style unchanged.
  * **TX view**: elapsed time in 16x32 and the power level in 8x16; with a TOT a bar of the remaining time, the last 10 s hatched with the countdown in an inverse chip (mono / contr); TX tone / bandwidth or TG / CC / TS and contact; the keys usable while transmitting. The `APRS Tx` banner of a beacon now sits on the panel instead of across the channel name.
  * **TX stopped** (time-out) and **TX blocked** (RX only / out of band) as full screen cards.
  * **APRS messages**: two-line rows with `IN` / `OUT` / `BLN`, callsign, distance, UTC time and the ACK state as symbol + word (`ACK`, `WAIT n`, `NO ACK`, `REJ`, `NOT SENT`); `*` opens a message on its own screen (whole text wrapped, `UP` / `DOWN` next message, `GREEN` reply, `RED` / `*` back).
  * **Message boxes and notifications**: one card look, the kind as a word and a glyph shape (info disc, warning triangle, error square, question diamond), long lines wrapped (e.g. an incoming APRS message); squelch, volume and power as a value card.
* **Accessibility display styles** *(untested)*: *Options → Display → Screen* now steps through Classic / EMCOM / **EMCOM mono** / **EMCOM contr**. Existing settings keep their style (two formerly unused option bits, no settings reset).
  * **EMCOM mono** (colour-blind safe): grey shades (night: reds); state by word and shape — outlined idle chip, solid `RX` / `SCAN` chip, inverse status bar on TX, hollow S-meter segments above S9 (main and RSSI screens), `!` + inverse chip for a low battery, thin `GPS` without a fix, inverse `SOS` / `TX OFF` / `TX` in the footer; grey menu preset with a white selection block; band scope waterfall as a brightness ramp.
  * **EMCOM contr** (low vision): white / yellow on black (night: amber / red-orange), bold small text, the channel frequency in 8x16, a thicker S-meter, the selected row as a yellow block, same shape cues.
  * Cancelling *Options → Display* now also re-applies a screen style that was changed and then reverted.
* **SSTV++** (GitHub issue #13) *(untested)*:
  * `UP` / `DOWN` on the SSTV screen: channel / VFO, ISS 145.800, 437.800 (RX only presets; leaving restores the channel).
  * Satellite screen, selected satellite, `9`: SSTV with the Doppler retune of that satellite kept running.
  * Every picture (30 lines or more) is saved to the flash with UTC time, mode and frequency, 8 newest kept; *SSTV* tab on sp7tq.pl/rt3s/narzedzia and `tools/opengd77_sstv.py` export PNG. The GPS NMEA log is 320 KB shorter.
  * APRS beacons and message replies wait while the SSTV screen is open.
* Python tools find the radio by its real USB IDs (1FC9:0094).
* **Scan on a busy channel** (report: scan held on a DMR repeater with a continuous net could not be left) *(untested)*: `▲` / knob clockwise now skip on also while the signal is still being checked (they did nothing then), `▼` held on a signal stops the scan instead of silently reversing it, and a scan that had just skipped a nuisance-deleted channel no longer stalls on the old channel. RED, GREEN, digits, `#` and PTT stopped the scan before and still do.
* GPS screen in the EMCOM style: the page number was drawn twice (top right).
* **GPS screen in the EMCOM style, new pages** *(untested)*: classic style unchanged (5 pages).
  * New first page **status**: big fix state, UTC time or the search time since the GPS was turned on / lost its fix (time to first fix, `TTFF` shown after the fix), lat / lon, locator, altitude, HDOP with GOOD / FAIR / POOR, speed, course, date; GPS off: `SK2`+`GREEN` turns it on.
  * **Signal** page replaces the bar graph: one bar per satellite, strongest first, PRN under each bar, dB-Hz scale; GPS solid, BeiDou hatched, no signal = hollow stub; *No satellites yet* instead of empty bars. Strip: heard / in view per constellation.
  * **Sky plot** as large as fits: N/E/S/W, 30 / 60° rings, GPS discs and BeiDou squares, filled with a signal, hollow without, PRN numbers where free, legend.
  * Quickkeys saved on a GPS page (`SK2`+digit) before this version open one page earlier in the EMCOM style (the status page was added first).
* **Satellite screen in the EMCOM style** *(untested)*: all four views (list, selected satellite, sky plot, pass details) with title bar + view number, status strip and key bar; classic style unchanged.
  * The list shows where the observer position comes from (`GPS` fix / `MANUAL`) and the short lat / lon; `*` opens Radio infos → Location to change it, `RED` comes back and the passes are predicted again for the new position (the screen now re-reads the position every time it opens). `#` was not free on the list (it jumps to the next satellite in view).
  * Selected satellite: AZ / EL, Doppler corrected RX / TX, `AOS in` / `LOS in` countdown, keys `1-3 QRG  9 SSTV` in the key bar.
  * Radio infos → Location (EMCOM style): key bar with the entry keys (13 digits, `▲▼` N/S, `SK2` E/W, `GREEN` save, `RED` clear, `SK2`+`GREEN` GPS on); the title shows the right page number.
* **SOS beacons on more screens** *(untested)*: they went out only on the channel / VFO screen and stopped silently elsewhere. Now also on Position, PACE, Coverage, Radio check, GPS, APRS RX (not while a message is typed), Event log, Last heard and the lock screen. Still held back on the TX, satellite and SSTV screens, CPS / hotspot, calibration, menus and while editing; then `SOS PAUSED` shows once per held back beacon and the beacon goes out as soon as an allowed screen is open.
* 1.4 KB more free RAM: the SEGGER RTT debug output (buffers) is left out of release builds, the battery graph copy uses 16-bit values.
* **Clearing asks first** *(untested)*: PACE slot (`0`, only a stored slot), Radio check reports (`0`, only when there are reports) and Last heard (long `#`) now ask in the key bar; `GREEN` clears, any other key (`RED` too) cancels and stays on the screen.
  * Event log and Coverage: the confirmation could never be accepted — pressing `GREEN` cancelled it on the key press, and its release then left the Event log / started a Coverage recording. The answer is now taken on the key release.

## R20260131-SP7TQ.4 — 2026-11-20

`.gla` language packs changed (new strings `.aprs_comment`, `.aprs_replies`, language version 8: older packs are not loaded): flash the ones built from this version.

* **APRS texts on the radio** *(untested)*:
  * **Options → APRS → Comment**: the beacon comment (23 chars) of the APRS configuration of the current channel / VFO, edited on the radio and written to the codeplug (the CPS reads the same text).
  * **Options → APRS → Replies**: the 12 canned message texts are editable (up to 67 chars), `0` restores the default; kept in the SPI flash (field data sector 0, offset 0x400).
  * **APRS RX → compose**: the reply list starts with *Type a message...* (free text typed on the keypad), `*` there edits the recipient, `1` in any view starts a new message to a typed callsign. Sent with the existing retries / ack handling.
  * Full screen text editor on the usual multi-tap keypad entry (insert mode, `#` or `SK2`+`◄` deletes, `SK2`+`►` space); `|`, `~` and `{` are skipped.

## R20260131-SP7TQ.3 — 2026-11-20

`.gla` language packs changed (new field tool strings): flash the ones from this release.

* **Field tools** (new main-menu tiles, also quickkey targets; data in the last 64 KB of the SPI flash, the GPS NMEA log is 64 KB shorter):
  * **PACE plan**: Primary / Alternate / Contingency / Emergency channels; `1`–`4` or GREEN switch the radio to that channel, `*` stores the current channel in the selected slot, `0` clears it.
  * **Position**: own position in MGRS (big) and UTM, decimal degrees, locator, altitude, GPS fix / satellites / HDOP, sunrise and sunset, distance and bearing to the waypoint; `*` marks the current position as the waypoint. MGRS checked against a reference library on 29 000 points.
  * **Coverage survey**: signal level with GPS position every 1–30 s (3840 points), live graph, session min/max; `tools/opengd77_coverage.py` makes a coloured KML map, GPX or CSV.
  * **Radio check**: each received over is measured (length, average and peak level) and rated readability / strength ("5 by 9"), with the last reports.
* After updating, the radio starts once in VFO mode (the screen numbers changed): switch back to the channel mode.

## R20260131-SP7TQ.2 — 2026-11-19

`.gla` language packs changed (new "Event log" string): flash the ones from this release.

* **Zones**: a zone shortened in the CPS no longer hangs the radio when selected (stale last-used channel position).
* **Launcher menus**: main menu, Options and Contacts as 4x3 icon tiles (26 icons, `docs/icons/`), key numbers open a tile directly, name bar; option lists with right-aligned values.
* **SP7TEAM look for the whole UI**: EMCOM colours as the default theme, flat boxes without shadows, menu title bars and full-width selection, soft key bars (GREEN / RED fields), restyled APRS RX (status strip, chips, radar panel), SSTV and boot screen (SP7TEAM emblem).
* **Event log** (main menu → Event log): TX, FM RX, DMR calls, APRS stations and messages, DTMF, SOS, SSTV, power-on with UTC time, 2048 entries in flash; `tools/opengd77_event_log.py` exports CSV. The GPS NMEA log starts 64 KB later.
* **TX OFF** in the EMCOM footer when TX inhibit is on.
* **EMCOM main screen** (default, Options → Display → Screen): status bar with state field, segmented S-meter, big channel number, parameter panel, live RX level, DMR caller, TX timer, APRS / messages / locator / position rows, UTC footer; red-only night palette.
* **EMCOM colours for the menus**: `*` in the theme editor loads them (dark day / red night theme).
* **SOS**: `SK1+9` twice or a quickkey: APRS emergency position beacons and a flashing banner.
* **Screenshots** over USB (`tools/opengd77_screenshot.py`); README pictures rendered from the firmware layout (`docs/screens/`).
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
