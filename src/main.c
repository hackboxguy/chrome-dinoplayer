// Chrome Dino Game Auto-Player - Phase 2: Dual Sensor + Adaptive Speed
// DigiSpark ATtiny85 + 2x LM393 LDR Sensor Boards (HID Keyboard via V-USB)
//
// See PLAN.md for full design documentation.
//
// Wiring:
//                          +-\/-+
//             A0 (D5) PB5  1|    |8  Vcc
//   USB D-    A3 (D3) PB3  2|    |7  PB2 (D2) A1 <-- Lower sensor (cactus)
//   USB D+    A2 (D4) PB4  3|    |6  PB1 (D1) --- (free)
//                     GND  4|    |5  PB0 (D0) <-- Upper sensor (bird)
//                          +----+
//
//   Lower LM393    DigiSpark         Upper LM393    DigiSpark
//   VCC ---------- 5V                VCC ---------- 5V
//   GND ---------- GND               GND ---------- GND
//   D0  ---------- PB2               D0  ---------- PB0

#ifndef F_CPU
#define F_CPU 16500000
#endif

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include "usbdrv.h"

// ---- Pin Configuration ----
#define LOWER_PIN       PB2
#define UPPER_PIN       PB0
#define LOWER_MASK      (1 << LOWER_PIN)
#define UPPER_MASK      (1 << UPPER_PIN)
#define SENSOR_MASKS    (LOWER_MASK | UPPER_MASK)

// ---- Sensor Polarity ----
// 1 = obstacle makes D0 go LOW (most common LM393 boards)
// 0 = obstacle makes D0 go HIGH (inverted boards)
#define OBSTACLE_IS_LOW 1

// ---- Timing (milliseconds) ----
#define STARTUP_DELAY       5000
#define KEY_HOLD_MS         80
#define COOLDOWN_MS         400
#define GAP_THRESHOLD_MS    30      // Max gap within one obstacle envelope
#define DUCK_HOLD_MS        200     // Hold duck longer than jump for safety

// ---- Adaptive Speed ----
#define ENVELOPE_HISTORY    5       // Rolling window size
#define DELAY_SCALE_NUM     1       // Numerator of scale factor (integer math)
#define DELAY_SCALE_DEN     1       // Denominator (factor = NUM/DEN = 1.0)
#define MIN_JUMP_DELAY      10      // Floor (ms)
#define MAX_JUMP_DELAY      150     // Ceiling (ms)
#define DEFAULT_JUMP_DELAY  50      // Before any envelopes measured

// ---- USB HID Keycodes ----
#define KEY_NONE        0x00
#define KEY_SPACE       0x2C
#define KEY_DOWN        0x51
#define MOD_NONE        0x00

// ---- HID Report Descriptor: Standard Boot Keyboard (63 bytes) ----
PROGMEM const char usbHidReportDescriptor[USB_CFG_HID_REPORT_DESCRIPTOR_LENGTH] = {
    0x05, 0x01,         // Usage Page (Generic Desktop)
    0x09, 0x06,         // Usage (Keyboard)
    0xA1, 0x01,         // Collection (Application)
    0x05, 0x07,         //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0,         //   Usage Minimum (Left Control)
    0x29, 0xE7,         //   Usage Maximum (Right GUI)
    0x15, 0x00,         //   Logical Minimum (0)
    0x25, 0x01,         //   Logical Maximum (1)
    0x75, 0x01,         //   Report Size (1)
    0x95, 0x08,         //   Report Count (8)
    0x81, 0x02,         //   Input (Data, Variable, Absolute)
    0x95, 0x01,         //   Report Count (1)
    0x75, 0x08,         //   Report Size (8)
    0x81, 0x01,         //   Input (Constant) - reserved byte
    0x95, 0x05,         //   Report Count (5)
    0x75, 0x01,         //   Report Size (1)
    0x05, 0x08,         //   Usage Page (LEDs)
    0x19, 0x01,         //   Usage Minimum (Num Lock)
    0x29, 0x05,         //   Usage Maximum (Kana)
    0x91, 0x02,         //   Output (Data, Variable, Absolute)
    0x95, 0x01,         //   Report Count (1)
    0x75, 0x03,         //   Report Size (3)
    0x91, 0x01,         //   Output (Constant) - LED padding
    0x95, 0x06,         //   Report Count (6)
    0x75, 0x08,         //   Report Size (8)
    0x15, 0x00,         //   Logical Minimum (0)
    0x25, 0x65,         //   Logical Maximum (101)
    0x05, 0x07,         //   Usage Page (Keyboard/Keypad)
    0x19, 0x00,         //   Usage Minimum (0)
    0x29, 0x65,         //   Usage Maximum (101)
    0x81, 0x00,         //   Input (Data, Array)
    0xC0                // End Collection
};

// ---- USB state ----
static uchar reportBuffer[8];
static uchar idleRate;

// ---- Envelope tracking (lower sensor only, for speed estimation) ----
static uint16_t envelopeHistory[ENVELOPE_HISTORY];
static uint8_t  envelopeIndex = 0;
static uint8_t  envelopeCount = 0;  // how many valid entries we have

// ---- USB HID callbacks ----
uchar usbFunctionSetup(uchar data[8]) {
    usbRequest_t *rq = (void *)data;
    usbMsgPtr = reportBuffer;
    if ((rq->bmRequestType & USBRQ_TYPE_MASK) == USBRQ_TYPE_CLASS) {
        if (rq->bRequest == USBRQ_HID_GET_REPORT) {
            return sizeof(reportBuffer);
        } else if (rq->bRequest == USBRQ_HID_GET_IDLE) {
            usbMsgPtr = &idleRate;
            return 1;
        } else if (rq->bRequest == USBRQ_HID_SET_IDLE) {
            idleRate = rq->wValue.bytes[1];
        }
    }
    return 0;
}

// ---- Oscillator calibration ----
void calibrateOscillator(void) {
    uchar step = 128;
    uchar trialValue = 0, optimumValue;
    int x, optimumDev;
    int targetValue = (unsigned)(1499 * (double)F_CPU / 10.5e6 + 0.5);

    do {
        OSCCAL = trialValue + step;
        x = usbMeasureFrameLength();
        if (x < targetValue)
            trialValue += step;
        step >>= 1;
    } while (step > 0);

    optimumValue = trialValue;
    optimumDev = x;
    for (OSCCAL = trialValue - 1; OSCCAL <= trialValue + 1; OSCCAL++) {
        x = usbMeasureFrameLength() - targetValue;
        if (x < 0)
            x = -x;
        if (x < optimumDev) {
            optimumDev = x;
            optimumValue = OSCCAL;
        }
    }
    OSCCAL = optimumValue;
}

void usbEventResetReady(void) {
    calibrateOscillator();
}

// ---- Sensor reading ----
static inline uint8_t readLower(void) {
    uint8_t high = (PINB & LOWER_MASK) ? 1 : 0;
#if OBSTACLE_IS_LOW
    return !high;
#else
    return high;
#endif
}

static inline uint8_t readUpper(void) {
    uint8_t high = (PINB & UPPER_MASK) ? 1 : 0;
#if OBSTACLE_IS_LOW
    return !high;
#else
    return high;
#endif
}

// ---- Key press/release via USB ----
static void sendKey(uchar key, uint8_t holdMs) {
    reportBuffer[0] = MOD_NONE;
    reportBuffer[1] = 0;
    reportBuffer[2] = key;
    reportBuffer[3] = 0;
    reportBuffer[4] = 0;
    reportBuffer[5] = 0;
    reportBuffer[6] = 0;
    reportBuffer[7] = 0;

    while (!usbInterruptIsReady()) {
        wdt_reset();
        usbPoll();
    }
    usbSetInterrupt(reportBuffer, sizeof(reportBuffer));

    uint8_t i;
    for (i = 0; i < holdMs; i++) {
        wdt_reset();
        usbPoll();
        _delay_ms(1);
    }

    reportBuffer[2] = KEY_NONE;
    while (!usbInterruptIsReady()) {
        wdt_reset();
        usbPoll();
    }
    usbSetInterrupt(reportBuffer, sizeof(reportBuffer));
}

// ---- Blocking delay that keeps USB alive ----
static void usbDelay(uint16_t ms) {
    while (ms--) {
        wdt_reset();
        usbPoll();
        _delay_ms(1);
    }
}

// ---- Envelope history: record and compute rolling minimum ----
static void recordEnvelope(uint16_t width) {
    envelopeHistory[envelopeIndex] = width;
    envelopeIndex = (envelopeIndex + 1) % ENVELOPE_HISTORY;
    if (envelopeCount < ENVELOPE_HISTORY)
        envelopeCount++;
}

static uint16_t getRollingMin(void) {
    if (envelopeCount == 0)
        return 0;
    uint16_t min = envelopeHistory[0];
    uint8_t i;
    for (i = 1; i < envelopeCount; i++) {
        if (envelopeHistory[i] < min)
            min = envelopeHistory[i];
    }
    return min;
}

static uint16_t getAdaptiveDelay(void) {
    uint16_t min = getRollingMin();
    if (min == 0)
        return DEFAULT_JUMP_DELAY;

    uint16_t delay = (min * DELAY_SCALE_NUM) / DELAY_SCALE_DEN;

    if (delay < MIN_JUMP_DELAY)
        delay = MIN_JUMP_DELAY;
    if (delay > MAX_JUMP_DELAY)
        delay = MAX_JUMP_DELAY;

    return delay;
}

// ---- Millisecond tick counter (approximate, using main loop timing) ----
// Each call to tickMs() represents ~1ms of real time when called in the
// main loop with _delay_ms(1). Not precise, but sufficient for envelope
// measurement where we need ~5ms resolution.

// ---- Main ----
int main(void) {
    // Configure sensor pins as inputs (no pull-ups, LM393 drives them)
    DDRB  &= ~SENSOR_MASKS;
    PORTB &= ~SENSOR_MASKS;

    // USB init: disconnect, wait, reconnect
    usbInit();
    usbDeviceDisconnect();
    uchar i = 0;
    while (--i) {
        wdt_reset();
        _delay_ms(1);
    }
    usbDeviceConnect();
    wdt_enable(WDTO_2S);
    sei();

    // Wait for USB to settle
    usbDelay(STARTUP_DELAY);

    // Send spacebar to start the game
    sendKey(KEY_SPACE, KEY_HOLD_MS);
    usbDelay(500);

    // ---- State for main loop ----
    // Obstacle detection state
    uint8_t  obstacleActive = 0;    // currently inside an obstacle detection
    uint8_t  actionTaken = 0;       // already sent key for this obstacle
    uint8_t  upperTriggered = 0;    // upper sensor fired during this obstacle

    // Envelope measurement (lower sensor)
    uint16_t envelopeStart = 0;     // ms tick when envelope began
    uint16_t gapTimer = 0;          // ms since lower sensor went HIGH
    uint8_t  inEnvelope = 0;        // tracking an envelope

    // Cooldown
    uint16_t cooldownTimer = 0;

    // Approximate ms counter (wraps at 65535, ~65 seconds, fine for our use)
    uint16_t msTick = 0;

    // ---- Main game loop ----
    while (1) {
        wdt_reset();
        usbPoll();
        _delay_ms(1);
        msTick++;

        // Tick cooldown
        if (cooldownTimer > 0) {
            cooldownTimer--;

            // Even during cooldown, keep tracking envelope on lower sensor
            // so we get accurate width measurements
            uint8_t lower = readLower();
            if (inEnvelope) {
                if (lower) {
                    // Sensor clear — obstacle still passing or gap within it?
                    gapTimer++;
                    if (gapTimer > GAP_THRESHOLD_MS) {
                        // Envelope ended
                        uint16_t width = msTick - envelopeStart;
                        recordEnvelope(width);
                        inEnvelope = 0;
                    }
                } else {
                    // Still triggered (or re-triggered within gap)
                    gapTimer = 0;
                }
            } else if (!lower) {
                // New envelope starting during cooldown
                envelopeStart = msTick;
                inEnvelope = 1;
                gapTimer = 0;
            }

            continue;
        }

        // Read both sensors simultaneously
        uint8_t lower = readLower();
        uint8_t upper = readUpper();
        uint8_t anyTrigger = lower || upper;

        // ---- Envelope tracking on lower sensor ----
        if (inEnvelope) {
            if (lower) {
                gapTimer = 0;   // still active or re-triggered
            } else {
                gapTimer++;
                if (gapTimer > GAP_THRESHOLD_MS) {
                    // Envelope ended
                    uint16_t width = msTick - envelopeStart;
                    recordEnvelope(width);
                    inEnvelope = 0;
                }
            }
        } else if (!lower && lower == 0) {
            // Lower sensor just triggered — start new envelope
            // (only when not already in envelope)
            if (readLower() == 0) {  // double-check (noise rejection)
                envelopeStart = msTick;
                inEnvelope = 1;
                gapTimer = 0;
            }
        }

        // ---- Obstacle detection and action ----
        if (anyTrigger && !obstacleActive) {
            // Rising edge: obstacle just appeared
            obstacleActive = 1;
            actionTaken = 0;
            upperTriggered = upper;
        }

        if (obstacleActive && anyTrigger) {
            // Track if upper sensor fires at any point during this obstacle
            if (upper)
                upperTriggered = 1;
        }

        if (obstacleActive && !actionTaken) {
            // Decide action: wait a tiny bit to let both sensors register,
            // then act based on the combination
            // We act on the first poll where we have a trigger

            // Compute adaptive delay from speed history
            uint16_t jumpDelay = getAdaptiveDelay();

            if (upperTriggered && !lower) {
                // Only upper triggered → bird → DUCK
                usbDelay(jumpDelay);
                sendKey(KEY_DOWN, DUCK_HOLD_MS);
                actionTaken = 1;
                cooldownTimer = COOLDOWN_MS;
            } else if (lower) {
                // Lower triggered (with or without upper) → cactus → JUMP
                usbDelay(jumpDelay);
                sendKey(KEY_SPACE, KEY_HOLD_MS);
                actionTaken = 1;
                cooldownTimer = COOLDOWN_MS;
            }
        }

        // Reset when obstacle fully clears
        if (!anyTrigger) {
            obstacleActive = 0;
            upperTriggered = 0;
            actionTaken = 0;
        }
    }
    return 0;
}
