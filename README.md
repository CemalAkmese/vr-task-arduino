# VR Task Arduino Sketch

Arduino Mega sketch (`VR_Taskv_PreCue_WN_DelayAdded_3Lick.ino`) controlling a VR behavioral task: reads left/right lick sensors, drives reward solenoids and a spout-positioning servo, plays audio cues, and reports trial outcomes back over serial.

This version updates the pin mapping to match new wiring, and moves audio generation off the Mega onto a companion Arduino Due that does the actual tone/white-noise synthesis.

## Pin changes

| Function | Variable | Old pin | New pin |
|---|---|---|---|
| Left lick sensor | `lcap` | 28 | 30 |
| Right lick sensor | `rcap` | 30 | 45 |
| Left solenoid | `lspout` | 40 | 2 |
| Right solenoid | `rspout` | 38 | 40 |
| Audio trigger (bit 0) | `duePin3` | — (new) | 3 |
| Audio trigger (bit 1) | `duePin5` | — (new, replaces `audioamp`) | 5 |
| Trial event pulse codes → behavior DAQ only | `EVT_PIN` | 46 (was `toneOut`) | 46 |
| Sync barcode → both DAQs | `BARCODE_PIN` | — (new) | 48 |

Unchanged: `toggle`(4), `ch_spoutmotor`(7), `lsenseOut`(42), `rsenseOut`(44), `servoOut`(50) — not part of the audio or DAQ-sync path, no new pin was specified for these.

## Audio architecture

Previously the Mega generated its own tones (`tone()`) and white noise (an on-board LFSR bit-banged onto the `audioamp` pin). That's been removed. Audio synthesis now happens on a separate Due board; the Mega only tells it *what* to play by holding two pins (`duePin3`, `duePin5`) in one of four level combinations:

| `duePin3` | `duePin5` | Due plays |
|---|---|---|
| HIGH | HIGH | idle / silence |
| LOW | HIGH | Tone A |
| HIGH | LOW | Tone B |
| LOW | LOW | White noise |

Wiring: `Mega pin 3` and `Mega pin 5` go through a logic level shifter (5V → 3.3V) to the Due's input pins, with a shared ground between boards. The Due runs its own sketch that reads these two lines and synthesizes the selected sound via its onboard DAC.

New helper functions in the Mega sketch:

- `SetSoundState(state)` — immediately sets `duePin3`/`duePin5` to the level pair for `SND_IDLE` / `SND_TONE_A` / `SND_TONE_B` / `SND_NOISE`.
- `StartSound(state, duration)` — sets a state and schedules a **non-blocking** return to idle after `duration` ms. Used for the pre-cue tone and the reward cue, so the toggle button and serial reads stay responsive while it plays.
- `SoundTimerFunc()` — called every `loop()` iteration; performs the scheduled revert-to-idle from `StartSound()`.
- `PlayWhiteNoiseBlocking(duration)` — sets `SND_NOISE`, `delay(duration)`, reverts to idle. Used in `WhiteNoise()` (wrong-spout correction cue) and `FalseAlarm()`, which were already blocking sections in the original code.

Call sites that changed from `tone(audioamp, ...)` to the new signaling:

- `CueFunc()` — main task cue (tone A/B chosen by `freq == freq_b`)
- `GiveReward()` — reward cue for `'G'`/Go-NoGo trials (`rewardcuedur`)
- `SerialInfo()` — pre-cue tone sent before a trial starts (`buf[4] == 'H'`/`'T'`)

## DAQ sync: EVT_PIN and BARCODE_PIN

Two more outputs were added to mark trial events and keep the Mega's clock aligned with the DAQ(s), separate from the audio-trigger pins above.

### EVT_PIN (46) — trial event pulse codes, behavior DAQ only

Non-blocking pulse-count encoding: N × 5ms HIGH pulses with 5ms LOW gaps between them = event code N, followed by a 20ms silence before the next code starts (so consecutive codes are distinguishable). This pin used to be `toneOut` (a simple "cue playing" HIGH/LOW flag); that behavior is gone, replaced entirely by these event codes.

| Code | Event |
|---|---|
| 1 | Reward right |
| 2 | Reward left |
| 3 | Reward centre *(defined for parity with other rigs — this task has no center spout, so nothing currently triggers it)* |
| 4 | White noise played |
| 5 | Servo moved (extended to task position) |
| 6 | Servo retracted |

Events are pushed onto a small FIFO (`QueueEvent()`) and drained one at a time by `EvtFunc()`, called every `loop()`. This guarantees two events firing close together are sent one after another rather than colliding on the wire — at the cost of the second event's pulses being delayed until the first one (plus the 20ms gap) finishes.

**Timing caveat:** `WhiteNoise()`'s 500ms noise burst still uses a blocking `delay()` internally (pre-existing in this sketch). While blocked, `EvtFunc()` isn't polled, so a queued code just waits until the blocking section ends before it starts pulsing — nothing collides, just a short delay. `FalseAlarm()`'s penalty timeout is no longer blocking (see below), so it no longer holds up the queue.

### BARCODE_PIN (48) — 32-bit sync barcode, both DAQs

Fires automatically every 30s, independent of task state, following the open-ephys/sync-barcodes standard: `20ms HIGH start pulse → 20ms LOW gap → 32 × 29ms data bits (LSB first) → 30s wait`. The 32-bit value is a simple counter that increments once per barcode sent (starts at 0). Implemented non-blockingly via `BarcodeFunc()`, called every `loop()`, so it stays on schedule even across false alarms.

Wire `BARCODE_PIN` to the NIDQ sync input and to the behavior DAQ, per your colleague's spec.

### FalseAlarm() penalty is non-blocking

`FalseAlarm()`'s post-noise penalty (`falseAlarmTimeout`, now 2000ms — shortened from the original 5000ms) used to be a blocking `delay()` that froze the whole loop, including `BarcodeFunc()`, `EvtFunc()`, and lick sampling. It's now a `millis()`-based timer (`falseAlarmStarted` / `falseAlarmPenaltyEnd`) checked every `loop()`, so the rest of the sketch keeps running during the penalty. The white-noise burst itself (`PlayWhiteNoiseBlocking`, ~500ms) still blocks, per the caveat above.

## Companion Due sketch

This Mega sketch expects a Due running a matching listener sketch that reads `duePin3`/`duePin5` and drives its DAC accordingly. Both are included in this repo:

- [`CustomTriggerMega/CustomTriggerMega.ino`](CustomTriggerMega/CustomTriggerMega.ino) — standalone reference/test sketch showing the signaling API (`go_idle()`, `play_tone_a()`, `play_tone_b()`, `play_white_noise()`) that the main task sketch's `SetSoundState()`/`StartSound()` are modeled on.
- [`CustomTriggerDue/CustomTriggerDue.ino`](CustomTriggerDue/CustomTriggerDue.ino) — runs on the Due; reads the 2-bit state and synthesizes tone A, tone B, or white noise via its onboard DAC (96 kHz DDS with attack/release envelope).

Confirmed wiring: **Mega pin 3 → Due pin 3**, **Mega pin 5 → Due pin 2**, through a logic level shifter, with a shared ground between boards. The Mega only decides *when* and *which* sound to play by holding these two pins in one of the four level combinations above; the Due does all actual sound synthesis.
