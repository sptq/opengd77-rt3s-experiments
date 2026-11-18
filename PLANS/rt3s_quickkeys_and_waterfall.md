# RT3S: Quickkeys menu + waterfall improvements

Target radio: **Retevis RT3S** (built as `PLATFORM=MDUV380`).
Physical controls: PTT, SK1, SK2, channel knob (rotary), 4x4 keypad.
There is **no** orange/top button on the RT3S — in this firmware
`BUTTON_ORANGE` is emulated on MDUV380 with **SK1 + GREEN**
(`application/source/applicationMain.c`).

## Findings (current state)

* Quickkeys already exist but are invisible to the user:
  * `SK2 + digit` (short) on the Channel/VFO screen runs slot 0..9.
  * `SK2 + digit` inside a supported menu stores "this menu entry" in the slot.
  * `SK2 + long digit` on the Channel/VFO screen clears the slot.
  * Stored as 10 × `uint16_t` in the codeplug (`CODEPLUG_ADDR_QUICKKEYS`),
    encoding in `menuSystem.h` (`QUICKKEY_*` macros): bit15 = type
    (0 = contact, 1 = menu), menu id (5 bits), entry id, function.
* Many useful functions are only reachable through undocumented combos
  (`SK1+1` APRS on/off, `SK1+2` send APRS beacon, `SK1+RED` day/night,
  long `#` in VFO starts the sweep).
* SK1/SK2 alone are already taken (voice prompt repeat, monitor on extra-long
  SK2, select TX TG on SK2 during RX), so new triggers must not hijack them.

## Part A — Quickkeys menu (Options → Quickkeys)

New menu `MENU_QUICKKEYS` (id must stay < 32, it is quickkey-addressable).

* Shows slots `0..9` with a description of the current assignment:
  `None`, a menu title, a quick-menu entry, a zone, a contact name, or a
  new *action* name.
* `UP/DOWN` / knob: select slot. `LEFT/RIGHT`: cycle the assignment through a
  curated list (keep current, None, actions, common menus).
* `GREEN`: write changed slots to the codeplug. `RED`: discard and exit.
  (Writes are batched — no flash write per key press.)

### New quickkey *actions*

Encoded as `QUICKKEY_CONTACT` type with a reserved high index range
(`0x7F00 | actionId`). Upstream firmware validates contact indices against
`CODEPLUG_CONTACTS_MIN..MAX` and silently ignores these, so a codeplug that
goes back to upstream firmware cannot trigger anything unexpected (using a
spare *menu id* instead could push e.g. the TX screen on upstream builds).

| Action | Calls |
|---|---|
| APRS beacon now | `aprsBeaconingSendBeacon(false, true)` (same as `SK1+2`) |
| APRS beaconing on/off | `aprsBeaconingToggles()` (same as `SK1+1`) |
| Day / night theme | `uiChannelModeOrVFOModeThemeDaytimeChange(true, …)` |
| Band scope / waterfall | switch to VFO and start the sweep (same as long `#`) |

## Part B — waterfall / band-scope improvements (VFO sweep)

* **Peak hold** trace on the band scope: per-column decaying maximum, drawn
  as a single pixel above the live bar.
* **Strongest signal marker**: after each full sweep, mark the strongest
  column above the noise floor.
* **Tune to peak** — `SK1 + 0` while sweeping: re-centre the VFO on the
  strongest signal (rounded to the VFO step), shifting the waterfall history
  instead of clearing it.

## Out of scope / later

GPS track log to SPI flash, fox-hunt RSSI tone, APRS RX decode, DMR SMS — see
[`rt3s_ideas_backlog.md`](rt3s_ideas_backlog.md); each gets its own plan.

## Status

Implemented on branch `feature/sk-shortcuts-waterfall`; builds clean for all three variants. **Not yet tested on hardware** — run the checklist below.

## Verification

* Build `PLATFORM=MDUV380` (and `RT84_DM1701`, `MDUV380 UV380_PLUS_10W`) in
  the docker builder; no new warnings.
* On-radio test checklist (manual, needs hardware):
  1. Options → Quickkeys lists slots; existing SK2+digit assignments show.
  2. Assign "APRS beacon" to 5, GREEN, exit; `SK2+5` on channel screen sends.
  3. RED in the menu discards changes.
  4. `SK2+long 5` still clears the slot; menu shows `None`.
  5. Assign an action to a slot, then in a menu press SK2+that digit: the
     action must NOT be silently overwritten (clear with SK2+long digit first).
  6. Start a channel scan, trigger "Band scope": scan stops, VFO sweep starts.
  7. Sweep: peak-hold dots decay; marker follows strongest carrier;
     `SK1+0` re-centres on it, waterfall history shifts rather than clears.
