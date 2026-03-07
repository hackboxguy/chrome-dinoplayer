# Chrome Dino Auto-Player - Design Plan

## Overview

A DigiSpark ATtiny85 USB dongle that plays the Chrome dino game automatically.
It enumerates as a standard HID keyboard, uses two LM393 LDR sensor boards to
detect obstacles on the monitor, and injects spacebar (jump) or down-arrow
(duck) keystrokes into the host PC. No host software or drivers required.

## Hardware

- DigiSpark ATtiny85 (micronucleus bootloader, 8KB flash)
- 2x LM393 LDR comparator boards (with potentiometer for threshold tuning)
- USB provides power (5V) to everything

### Pin Assignment

```
                     +-\/-+
        A0 (D5) PB5  1|    |8  Vcc
  USB-  A3 (D3) PB3  2|    |7  PB2 (D2) A1 <-- Lower sensor (cactus height)
  USB+  A2 (D4) PB4  3|    |6  PB1 (D1) --- (free / future use)
                GND  4|    |5  PB0 (D0) <-- Upper sensor (bird height)
                     +----+
```

### Wiring

```
LM393 Board (Lower)     DigiSpark
VCC  ------------------  5V
GND  ------------------  GND
D0   ------------------  PB2 (Pin 2)

LM393 Board (Upper)     DigiSpark
VCC  ------------------  5V
GND  ------------------  GND
D0   ------------------  PB0 (Pin 0)
```

### Sensor Placement

Both LM393 LDR sensors are mounted vertically on the monitor, one above
the other, at the same horizontal position (a few cm ahead of the dino):

```
  Monitor screen
  ┌──────────────────────────────────────┐
  │                                      │
  │           [Upper LDR]  ← bird        │
  │                                      │
  │  🦖      [Lower LDR]  ← cactus      │
  │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│
  └──────────────────────────────────────┘
```

- **Lower sensor**: Aligned with the dino / cactus height (ground obstacle zone)
- **Upper sensor**: Aligned with medium-height bird flight path
- Both at the **same horizontal position** (vertically stacked)

### LM393 Potentiometer Calibration

Adjust each board's potentiometer so that:
- D0 = HIGH when viewing white background (no obstacle)
- D0 = LOW when a dark obstacle passes under the sensor

If your board has inverted output, set `OBSTACLE_IS_LOW 0` in the firmware.

## Detection Logic

### Obstacle Classification

Two vertically stacked sensors produce four possible states:

| Lower (PB2) | Upper (PB0) | Obstacle       | Action     |
|-------------|-------------|----------------|------------|
| triggers    | triggers    | Tall cactus    | JUMP       |
| triggers    | -           | Short cactus   | JUMP       |
| triggers    | -           | Low bird       | JUMP       |
| -           | triggers    | Medium/high bird | DUCK     |
| -           | -           | Clear ground   | (nothing)  |

Simplified decision rule:
- **Upper triggers WITHOUT lower** → DUCK (down arrow)
- **Any other trigger combination** → JUMP (spacebar)

Rationale:
- A cactus (short or tall) always has dark pixels at ground level → lower
  sensor always fires for any cactus
- A bird flying at medium/high altitude has no ground-level pixels → only
  upper sensor fires
- A low-flying bird has ground-level pixels → lower fires → jump clears it

### Sensor Snapshot

Both sensors are read in the same polling iteration (microsecond-level)
to ensure a consistent snapshot. This prevents a fast-moving tall cactus
from briefly appearing as "only lower triggered" before the upper sensor
catches up.

## Speed Estimation via Pulse-Width Envelope

### The Problem

The dino game accelerates over time. A fixed jump delay works initially
but becomes too slow (or too fast) as game speed changes. We need to
measure obstacle speed without a third sensor.

### Approach: Envelope Measurement

Instead of measuring individual pulses, we measure the **envelope** —
the total time from when the sensor first detects an obstacle to when
the obstacle fully clears.

This handles the fact that cacti are fork-shaped (not solid blocks)
and can produce multiple sub-pulses:

```
Forked cactus signal on lower sensor:

Pin state:  HIGH ██ LOW ██ LOW ███ LOW ██████ HIGH
                 |    gap    gap         |
                 |_______________________|
                 |    envelope width     |
                 first LOW         stays HIGH > GAP_THRESHOLD
```

#### Envelope State Machine

1. Sensor goes LOW → record `envelope_start`, enter TRACKING state
2. Sensor goes HIGH → start `gap_timer`
3. If sensor goes LOW again before `gap_timer > GAP_THRESHOLD` (~30ms)
   → same obstacle, continue tracking
4. If sensor stays HIGH beyond `GAP_THRESHOLD` → obstacle fully passed,
   compute `envelope_width = now - envelope_start`

The GAP_THRESHOLD (~30ms) is chosen so that:
- Gaps between cactus forks (< 5ms at any speed) are merged
- Real gaps between separate obstacles (> 200ms) are not

### Speed from Envelope Width

A single small cactus has a fixed pixel width on screen. At any given
game speed, it takes a proportional time to pass the sensor:

```
envelope_width = pixel_width / game_speed
game_speed ∝ 1 / envelope_width
```

### Rolling Minimum Filter

Different obstacles have different pixel widths, producing different
envelope widths at the same speed:

```
At the same game speed:
  Small cactus:    40ms  ← narrowest, most consistent
  Tall cactus:     55ms
  Cactus group:    90ms  ← multiple cacti clustered together
  Bird:            45ms
```

Using a **rolling minimum over the last N envelopes** (N ≈ 5) naturally
selects the single small cactus as the reference, filtering out wider
obstacles:

```
Recent envelopes: [90, 55, 40, 45, 90]
Rolling min = 40ms → clean speed reference
```

As the game accelerates, all envelopes shrink proportionally:

```
Early game:   rolling_min ≈ 40ms → slow, use longer jump delay
Mid game:     rolling_min ≈ 25ms → medium speed
Late game:    rolling_min ≈ 15ms → fast, use shorter jump delay
```

### Adaptive Jump Delay

The jump delay is linearly scaled from the rolling minimum envelope width:

```
jump_delay = rolling_min * DELAY_SCALE_FACTOR
```

Where `DELAY_SCALE_FACTOR` is a tunable constant (start with ~1.0 and
adjust during testing). The idea: if obstacles take X ms to pass, we
need roughly X ms of lead time to jump at the right moment.

Initial bounds to prevent extreme values:
- `MIN_JUMP_DELAY` = 10ms  (floor, prevents jumping too late)
- `MAX_JUMP_DELAY` = 150ms (ceiling, prevents jumping too early)

## USB HID Implementation

### Device Identity

- VID/PID: 0x16c0 / 0x27dc (obdev shared VID for HID)
- Device name: "DinoPlayer"
- Manufacturer: "digistump.com"
- Protocol: Boot keyboard (standard 8-byte report)

### HID Report Format

Standard boot keyboard report (8 bytes, no report ID):

```
Byte 0: Modifier keys (Ctrl, Shift, Alt, GUI)
Byte 1: Reserved (0x00)
Byte 2: Key code 1
Byte 3: Key code 2 (unused)
Byte 4: Key code 3 (unused)
Byte 5: Key code 4 (unused)
Byte 6: Key code 5 (unused)
Byte 7: Key code 6 (unused)
```

Key codes used:
- `0x2C` = Spacebar (jump)
- `0x51` = Down Arrow (duck)
- `0x00` = No key (release)

### Key Timing

- Key hold duration: ~80ms (short press)
- USB poll interval: 10ms
- V-USB interrupt handling adds ~5ms jitter (acceptable)

## Startup Sequence

1. USB disconnect/reconnect (forces re-enumeration)
2. Wait `STARTUP_DELAY` (5 seconds) for USB to settle
3. Send spacebar to start the game
4. Wait 500ms for game to initialize
5. Enter main detection loop

## Tunable Parameters

| Parameter          | Default | Description                                    |
|--------------------|---------|------------------------------------------------|
| OBSTACLE_IS_LOW    | 1       | Sensor polarity (1=LOW means obstacle)         |
| STARTUP_DELAY      | 5000ms  | Wait before starting game                      |
| KEY_HOLD_MS        | 80ms    | How long spacebar is held (jump height)        |
| DUCK_HOLD_MS       | 200ms   | How long down-arrow is held (duck duration)    |
| COOLDOWN_MS        | 400ms   | Min time between actions                       |
| GAP_THRESHOLD_MS   | 30ms    | Max gap within one obstacle envelope           |
| ENVELOPE_HISTORY   | 5       | Number of envelopes for rolling minimum        |
| DELAY_SCALE_FACTOR | 1.0     | Multiplier: jump_delay = min_envelope * factor |
| MIN_JUMP_DELAY     | 10ms    | Floor for adaptive jump delay                  |
| MAX_JUMP_DELAY     | 150ms   | Ceiling for adaptive jump delay                |
| DEFAULT_JUMP_DELAY | 50ms    | Jump delay before any envelopes measured        |

## Firmware Size

ATtiny85 has 8192 bytes flash, micronucleus bootloader uses ~2KB,
leaving ~6KB for firmware.

- Phase 1 (single sensor, fixed delay): 2136 bytes (text) ✓
- Phase 2 (dual sensor, adaptive speed): 2620 bytes (text) ✓

## Day/Night Mode Issue

The Chrome dino game alternates between day mode (white background, dark
obstacles) and night mode (dark background, light obstacles) approximately
every 700 points. The LDR sensors are calibrated for day mode contrast
(dark obstacles on white). When night mode activates, the entire background
goes dark, causing the sensors to see it as one continuous obstacle.

### Workaround: Disable via Chrome DevTools

Open `chrome://dino`, press Ctrl+Shift+I, start the game, then run in
the Console:

```javascript
Runner.getInstance().invert = function(reset) {};
```

This overrides the night mode inversion function with a no-op, keeping
the game permanently in day mode. Must be re-run after each game restart.

### Future: Firmware-Based Detection

A potential firmware solution would:
1. Track how long the lower sensor stays continuously triggered
2. If triggered > 300ms (no real obstacle is that wide), it's a mode switch
3. Flip the sensor polarity variable at runtime
4. Suppress actions for ~500ms while the transition settles
5. Clear envelope history (speed characteristics may differ after flip)

This approach requires changing `OBSTACLE_IS_LOW` from a compile-time
`#define` to a runtime variable, which adds ~100 bytes of flash.

## Future Ideas

- PB1 is free — could add an LED for status indication
- Firmware-based day/night mode detection (auto-flip sensor polarity)
- Software debounce / minimum trigger duration to filter light gray
  background variations (clouds, ground texture)
- Score tracking via obstacle count
 