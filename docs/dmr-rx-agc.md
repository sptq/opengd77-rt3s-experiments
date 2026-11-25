# DMR receive AGC

The DMR receive AGC uses the tuning profile adapted from TA8BMO's OpenGD77
work: its target curve, thresholds, median window, rise/release response and
peak limiter are attributed in
`MDUV380_firmware/application/include/functions/dmrRxAgc.h` and
`MDUV380_firmware/application/source/functions/dmrRxAgc.c`. Station ownership, PCM bypass,
interrupt synchronization and host tests are maintained in this fork.

## Settings and upgrades

The stored setting is unchanged:

| Stored value | Menu value |
| ---: | ---: |
| 0 | Off |
| 1 | -9 dB |
| 2 | -6 dB |
| 3 | -3 dB |
| 4 | 0 dB |
| 5 | +3 dB |
| 6 | +6 dB |
| 7 | +9 dB |
| 8 | +12 dB |

AGC Off remains unchanged. Existing enabled settings retain their numeric
value, but the new TA8BMO-derived target curve means their gain behavior is not
the old algorithm's behavior. Values above 8 left by a legacy firmware are
clamped to 8 when settings load. No persistent structure or storage magic
number migration is performed. Recalibrate the preferred AGC setting after an
upgrade using a normal, loud and quiet station.

The factory default remains AGC Off (`dmrRxAGC = 0`), which is intentional:
receivers retain their previous unprocessed-audio behavior until the user
enables AGC.

## Ownership and audio buffers

The last-heard list assigns a monotonically increasing `agcStationToken` to a
new station or recycled entry. The active AGC owner is published with the
explicit list entry pointer, DMR ID and token; pointer equality alone is not an
identity guarantee. List reset invalidates the owner before rebuilding the
list. Station changes reset only the transient median history and output ramp,
while a recalled `rxAGCGain` supplies the learned peak for a known station.

`hrc6000HandleLCData()` identifies a caller and sets `updateLastHeard`; later,
`applicationMain()` calls `lastHeardListUpdate()`. On the receive path,
`codecDecode()` runs inside the HRC task's critical section. For every decoded
block it calls `soundSetupBuffer()`, decodes into that write slot, then calls
`soundStoreBuffer()` to commit it. Owner publication also uses a task critical
section. On this single-core firmware, publication therefore cannot interleave
with an in-flight decoder call: blocks decoded before publication are committed
before its queue count is captured, and later blocks are queued after the new
owner is published.

For a station change on the normal receive PCM path,
`soundDMRRxAGCSetStationLevel()` captures the committed queue count. Rapid
successive changes recapture the then-current count, so every already-queued
slot remains quarantined for the latest owner; refreshing the same owner keeps
its current transition state and count. Those slots drain in FIFO order through
a transition state with peak limiting and no writeback to the new station.
`soundRefillData()` and `soundRetrieveBuffer()` both decrement the
quarantine only when they actually consume a queued slot. The shared
`dmrRxAgcTransitionBuffersAfterConsume()` helper prevents underflow and clamps
the count to the number of queue slots remaining. The latter is the transmit
encoder consumer; normal DMR TX calls
`codecInit()`/`soundInit()` first, which resets the queue and quarantine.

The shared ring also serves microphone, hotspot-encoded and CPS AMBE-compression
flows. Station updates during transmit, `USB_MODE_HOTSPOT`, or CPS AMBE
compression do not create a DMR PCM quarantine. Hotspot queue discard uses
`soundResetBufferQueue()` to reset the indices, count and quarantine together;
`soundInit()` also resets all four.
`soundTerminateSound()` only stops DMA and deliberately leaves queued data and
its accounting intact. CPS AMBE compression is explicitly marked by
`isCompressingAMBE`; its raw-buffer count is not treated as a DMR receive queue.
Closing that CPS compression session discards any residual raw queue through
`soundResetBufferQueue()` before clearing the mode flag.
`soundStoreBuffer()` does not increase the count on overflow; underruns do not
consume a slot or change the quarantine, and normal same-station underruns
preserve the learned peak. Ring wraparound is handled by the existing modulo
index updates and does not affect the prefix count.

The queue remains untagged, so correctness relies on the serialized producer
and owner-publication ordering above and on resetting accounting when a mode
discards or repurposes the shared ring. No per-slot station metadata is added.

`soundRefillData()` snapshots the owner before processing. If ownership is
unknown, changes during processing, or the transition-buffer quarantine is
active, it does not commit median history, gain or learned peak data to the
station owner. It does not flush queued decoder audio. An ordinary refill
underrun preserves the learned station level and clears only transient median
history.

The disabled-AGC and voice-prompt branches copy PCM bytes directly. They do not
apply gain, interpolation or saturation, including for `INT16_MIN`.

## Interrupt and real-time constraints

On the RT3S target, `Core/Src/main.c` assigns DMA1 Stream 5 (I2S3_EXT_TX) NVIC
priority 6. `Core/Inc/FreeRTOSConfig.h` sets
`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` to 5, so a task critical
section masks this DMA interrupt on this target. Ownership publication and
station reset therefore use a short bounded critical section only; no SPI,
delay, allocation or other blocking operation is added to the I2S refill path.
The sequence field also makes an inconsistent owner publication fail closed if
the helper is used outside that expected masked transition.

The top-level firmware Makefile discovers every
`application/source/**/*.c`, so `dmrRxAgc.c` is included in all platform
variants. The host test Makefile compiles the extracted module directly.

## Quiet-station limitation

An unknown station is qualified only when a peak is greater than 500. Sustained
peaks from 201 through 500 remain at unity gain and do not initialize the
learned level. This is preserved tuning, not a hardware measurement: no
HRC6000 audio capture was available to justify changing the threshold. The
host regression test records the boundary, and the physical procedure below
must specifically exercise quiet speech in this range.

## RT3S physical validation

Record the firmware version and back up the codeplug before flashing. With the
radio connected to a dummy load or suitable safe RF setup:

1. Set volume to its minimum audible level. Test AGC Off, then each of the
   eight settings with a normal station, a loud station and a quiet station.
2. Check unknown-station startup, a station switch in both loud-to-quiet and
   quiet-to-loud directions, rapid last-heard changes, and a recycled
   last-heard entry. Listen for clipping, sudden gain jumps or loss of speech.
3. Interrupt audio mid-transmission and allow an underrun/recovery. Repeat
   with voice prompts, rapid key beeps, melody replacement/cancellation and
   canceling the sound menu.
4. Receive APRS, then transmit and receive DMR. Confirm no click, mute or
   incorrect audio path appears at the APRS/I2S handoff.
5. Upgrade over an existing saved settings image. Confirm AGC Off remains Off,
   legacy values above 8 load as +12 dB, and enabled values can be
   recalibrated without changing the volume setting.

Automated host tests cover the AGC state machine, limiter, PCM helper,
ownership identities and deterministic boundaries. They do not replace this
radio procedure or prove hardware audio quality.
