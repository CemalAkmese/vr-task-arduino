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

Unchanged: `toggle`(4), `ch_spoutmotor`(7), `lsenseOut`(42), `rsenseOut`(44), `toneOut`(46), `servoOut`(50) — not part of the audio path, no new pin was specified for these.

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

## Companion Due sketch

This Mega sketch expects a Due running a matching listener sketch (`CustomTriggerMega.ino` / `CustomTriggerDue.ino` in the lab archive) that reads `duePin3`/`duePin5` and drives its DAC accordingly. **Double-check both boards agree on which physical Due pin is wired to Mega pin 5** — the reference Due sketch read it as Due pin 2, while the reference Mega sketch's wiring comment says Due pin 5; make sure the actual wiring and both sketches match before running.
