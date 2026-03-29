# Wiring Diagram Reference

## INMP441 Microphone

```
    INMP441
    ┌──────┐
    │  VDD │◄── ESP32-S3 3.3V   (NOT 5V — damages mic)
    │  GND │◄── ESP32-S3 GND
    │  SCK │◄── ESP32-S3 GPIO 18
    │   WS │◄── ESP32-S3 GPIO 17
    │   SD │──► ESP32-S3 GPIO 15
    │  L/R │◄── ESP32-S3 GND   (MUST be GND — not floating)
    └──────┘
```

## MAX98357A Amplifier

```
    MAX98357A
    ┌──────┐
    │  VIN │◄── ESP32-S3 5V    (NOT 3.3V — no sound)
    │  GND │◄── ESP32-S3 GND
    │ BCLK │◄── ESP32-S3 GPIO 3
    │  LRC │◄── ESP32-S3 GPIO 8
    │  DIN │◄── ESP32-S3 GPIO 16
    │   SD │    (leave floating — amp always on)
    │ GAIN │    (leave floating — default 9dB gain)
    │ OUT+ │──► Speaker positive (+)
    │ OUT- │──► Speaker negative (-)   (NOT to GND)
    └──────┘
```

## Speaker

```
    Speaker
    ┌──────┐
    │  (+) │◄── MAX98357A OUT+
    │  (-) │◄── MAX98357A OUT-   (NEVER connect to GND)
    └──────┘
```

## Full Breadboard Layout

```
ESP32-S3           INMP441          MAX98357A        Speaker
────────           ───────          ─────────        ───────
3.3V  ─────────►  VDD
GND   ──┬──────►  GND
        ├──────►  L/R
        └──────────────────────►  GND
5V    ─────────────────────────►  VIN
GPIO3 ─────────────────────────►  BCLK
GPIO8 ─────────────────────────►  LRC
GPIO15 ◄────────  SD
GPIO16 ────────────────────────►  DIN
GPIO17 ────────►  WS
GPIO18 ────────►  SCK
                                   OUT+ ──────────►  +
                                   OUT- ──────────►  -
```

## Pin Reference Table

| ESP32-S3 Pin | Connected To | Component | Notes |
|-------------|--------------|-----------|-------|
| 3.3V | VDD | INMP441 | Power supply |
| GND | GND | INMP441 | Ground |
| GND | L/R | INMP441 | Channel select — must be GND |
| GPIO 15 | SD | INMP441 | Data output from mic |
| GPIO 17 | WS | INMP441 | Word select / LRCLK |
| GPIO 18 | SCK | INMP441 | Bit clock |
| 5V | VIN | MAX98357A | Power supply |
| GND | GND | MAX98357A | Ground |
| GPIO 3 | BCLK | MAX98357A | Bit clock |
| GPIO 8 | LRC | MAX98357A | Word select / LRCLK |
| GPIO 16 | DIN | MAX98357A | Data input to amp |

## Critical Warnings

```
⚠️  INMP441 VDD must be 3.3V — 5V permanently destroys the chip
⚠️  INMP441 L/R must be GND — floating = zero output
⚠️  MAX98357A VIN must be 5V — 3.3V gives no audio output
⚠️  Speaker (−) goes to MAX98357A OUT− ONLY — never to GND
⚠️  MAX98357A SD pin left floating = amp always on (correct for this use)
⚠️  Do not use GPIO 46 on ESP32-S3 — it is a strapping pin
```
