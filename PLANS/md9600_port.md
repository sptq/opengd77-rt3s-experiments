# SP7TEAM EMCOM on the TYT MD-9600 / Retevis RT90 (HW V5, GPS)

Status 2026-11-23: stage 0 done, screens waiting for the owner's approval.

## Target

- Only hardware V5 (the 2571V5 firmware line), with the GPS module. V1/V2/V4 use
  other PLL / IF chips and need other builds; out of scope.
- Separate platform: `MD9600_firmware/` next to `MDUV380_firmware/`, own `Core/`
  (CubeMX pin init), own Makefile, output `OpenGD77_MD9600_HW_V5.bin`.
- Same MCU (STM32F405VG, 1 MB / 128 KB), same TYT DFU bootloader, MD-9600 upload
  cipher, AMBE codec at the same address as RT3S (0x0807537C, bin offset 0x6937C),
  taken from the MD-9600 V26.45 donor firmware.

## Hardware differences that drive the design

| | RT3S | MD-9600 V5 |
|---|---|---|
| Display | HX8353E 160x128 colour | ST7567 128x64 mono, fonts 6x8 / 6x8 bold / 8x8 / 8x16 / 16x32 |
| Keys | keypad on the radio | front: P1-P4, ALARM, up/down, green/red, rotary; mic: 0-9 * # A-D |
| Power | battery | external 13.8 V |
| GPS | own power switch | shares power with the microphone (`gpsAndMicPower`); cannot be switched off while the radio is in use, "GPS off" only ignores the data |
| Extra | - | detachable remote head (`remoteHead.c`) |

Dropped vs RT3S: SSTV, colour styles, APRS radar. Battery runtime becomes a supply
voltage screen with a 60 min history.

## Screens

Pixel-exact mockups drawn with the firmware fonts: `MD9600_firmware/tools/mockups/`
(`python3 screens.py` writes `out/*.png`). Review page with IDs E1-E8, A1-A4, M1-M3,
G1-G5, S1-S2, P1-P2, N1-N7, R1-R4.

Open decisions for the owner:
1. Menu: list (M1, recommended default) or tiles (M2).
2. Front keys: P1 blackout, P2 position, P3 radio check, P4 PACE next, ALARM SOS.
3. Key hint bar in tools (yes), not on the main screen.

## Stages

0. Done: upstream R20260131 MD9600 sources imported, container build, V5 binary.
1. Web flasher page on sp7tq.pl (own menu item); flash the vanilla build on the
   owner's radio and check RX / TX / GPS / mic keypad / front keys.
2. Shared application code (see `unify_sourcecodes.md`, phase 1 (a) + phase 3):
   per-platform `Core/`, one `application/` tree, display drivers selected by
   platform. Fork features then reach both radios.
3. Non-UI features: GPS rework, APRS RX + messages, event log, PACE, net control,
   waypoints, sked, blackout, supply monitor. Host tests as for RT3S.
4. Screens per the approved mockups: main + TX, alerts, menu, GPS / position,
   field tools, satellites, APRS.
5. Release with `OpenGD77_MD9600_HW_V5.bin`, flasher switched to the release,
   MD-9600 chapter in the guide.
