# 🎙️ VoiceCore ESP32 — Edge AI Voice Assistant

> A fully offline-triggered, cloud-AI-powered voice assistant built on the ESP32-S3.  
> Speak a question. Get a spoken answer. No app. No wake-word cloud. Just hardware and intelligence.

---

![Platform](https://img.shields.io/badge/platform-ESP32--S3-blue)
![AI Backend](https://img.shields.io/badge/AI-Groq%20%7C%20Llama3%20%7C%20Whisper-orange)
![License](https://img.shields.io/badge/license-MIT-green)
![Status](https://img.shields.io/badge/status-working-brightgreen)

---

## 📌 What Is This?

**VoiceCore ESP32** is a complete, production-quality voice assistant that runs on a $5 ESP32-S3 microcontroller. It uses:

- A **MEMS microphone (INMP441)** to capture your voice
- An **amplitude-based wake detector** — clap or speak loudly to trigger
- **Groq Whisper** (cloud STT) to transcribe your speech to text
- **Groq Llama 3.3 70B** (cloud LLM) to generate an intelligent answer
- **Groq Orpheus TTS** to convert the answer to natural speech
- A **class-D amplifier (MAX98357A)** and speaker to play the response

Everything runs on **free Groq API tier** — no OpenAI billing, no Google Cloud setup, no credit card required.

---

## 🏗️ System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        ESP32-S3                             │
│                                                             │
│  ┌──────────┐    I2S RX     ┌─────────────────────────┐    │
│  │ INMP441  │ ────────────► │   Amplitude Wake         │    │
│  │   Mic    │               │   Detector (IDLE state)  │    │
│  └──────────┘               └────────────┬────────────┘    │
│                                          │ peak > threshold │
│                               ┌──────────▼────────────┐    │
│                               │   Record to PSRAM      │    │
│                               │   (up to 8 seconds)    │    │
│                               └──────────┬────────────┘    │
│                                          │ silence detected │
│                               ┌──────────▼────────────┐    │
│                               │   Build WAV + POST     │    │
│                               │   Groq Whisper STT     │    │
│                               └──────────┬────────────┘    │
│                                          │ transcript text  │
│                               ┌──────────▼────────────┐    │
│                               │   POST to Groq         │    │
│                               │   Llama 3.3 70B LLM    │    │
│                               └──────────┬────────────┘    │
│                                          │ answer text      │
│                               ┌──────────▼────────────┐    │
│                               │   POST to Groq         │    │
│                               │   Orpheus TTS          │    │
│                               └──────────┬────────────┘    │
│                                          │ PCM audio        │
│  ┌──────────┐    I2S TX     ┌────────────▼───────────┐    │
│  │MAX98357A │ ◄──────────── │  Play from PSRAM        │    │
│  │ + Speaker│               │  (no SPIFFS involved)   │    │
│  └──────────┘               └─────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

---

## 🧰 Hardware Required

| Component | Model | Purpose | Cost (approx) |
|-----------|-------|---------|----------------|
| Microcontroller | ESP32-S3 Dev Module | Main processor + WiFi | ~$5 |
| MEMS Microphone | INMP441 | I2S digital mic input | ~$2 |
| Audio Amplifier | MAX98357A | I2S class-D 3W amplifier | ~$2 |
| Speaker | 4Ω or 8Ω, 2W–5W | Audio output | ~$2 |
| Jumper Wires | Male-to-male | Breadboard connections | ~$1 |
| Breadboard | Full size | Prototyping | ~$2 |
| USB Cable | Matches your board | Power + programming | — |

**Total cost: ~$14**

---

## 🔌 Wiring Diagram

### INMP441 Microphone → ESP32-S3

```
ESP32-S3 Pin        INMP441 Pin       Notes
────────────        ───────────       ──────────────────────────────
3.3V          ───►  VDD               3.3V ONLY — 5V will damage mic permanently
GND           ───►  GND
GND           ───►  L/R               MUST be GND — selects LEFT channel
GPIO 18       ───►  SCK               Serial clock (BCLK)
GPIO 17       ───►  WS                Word select (LRCLK)
GPIO 15       ───►  SD                Serial data output
```

> ⚠️ **Critical:** L/R pin MUST be connected to GND. If left floating, mic outputs zero.  
> ⚠️ **Critical:** VDD must be 3.3V only. 5V permanently destroys the INMP441.

---

### MAX98357A Amplifier → ESP32-S3

```
ESP32-S3 Pin        MAX98357A Pin     Notes
────────────        ─────────────     ──────────────────────────────
5V            ───►  VIN               Must be 5V — 3.3V gives no sound
GND           ───►  GND
GPIO 3        ───►  BCLK              Serial clock
GPIO 8        ───►  LRC               Word select (LRCLK)
GPIO 16       ───►  DIN               Serial data input
(leave SD pin floating)               Amp stays always-on
(leave GAIN pin floating)             Default gain = 9dB
```

> ⚠️ **Critical:** Never connect speaker negative (−) wire to GND.  
> ⚠️ **Critical:** Speaker − wire goes to OUT− on MAX98357A only.

---

### MAX98357A → Speaker

```
MAX98357A Pin       Speaker Wire
─────────────       ────────────
OUT+          ───►  Positive (+) wire
OUT−          ───►  Negative (−) wire   ← NOT to GND
```

---

### Full Wiring Summary Table

| Wire | From | To |
|------|------|----|
| Power mic | ESP32-S3 3.3V | INMP441 VDD |
| Ground mic | ESP32-S3 GND | INMP441 GND |
| Mic channel select | ESP32-S3 GND | INMP441 L/R |
| Mic clock | ESP32-S3 GPIO 18 | INMP441 SCK |
| Mic word select | ESP32-S3 GPIO 17 | INMP441 WS |
| Mic data | ESP32-S3 GPIO 15 | INMP441 SD |
| Power amp | ESP32-S3 5V | MAX98357A VIN |
| Ground amp | ESP32-S3 GND | MAX98357A GND |
| Amp clock | ESP32-S3 GPIO 3 | MAX98357A BCLK |
| Amp word select | ESP32-S3 GPIO 8 | MAX98357A LRC |
| Amp data | ESP32-S3 GPIO 16 | MAX98357A DIN |
| Speaker positive | MAX98357A OUT+ | Speaker + |
| Speaker negative | MAX98357A OUT− | Speaker − |

---

## 💻 Arduino IDE Setup

### Board Settings

Go to **Tools** in Arduino IDE and configure exactly:

| Setting | Value |
|---------|-------|
| Board | ESP32S3 Dev Module |
| Partition Scheme | **Huge APP (3MB No OTA / 1MB SPIFFS)** |
| PSRAM | **OPI PSRAM** |
| USB CDC On Boot | **Enabled** |
| Upload Mode | UART0 / Hardware CDC |
| USB Mode | Hardware CDC and JTAG |
| Upload Speed | 921600 |
| CPU Frequency | 240MHz |

> ⚠️ Wrong partition scheme = upload fails. Wrong PSRAM = crash on boot.

---

### Required Libraries

Install all via **Tools → Manage Libraries** in Arduino IDE:

| Library | Author | Version | Purpose |
|---------|--------|---------|---------|
| ESP32-audioI2S | schreibfaul1 | Latest | Audio playback (used for reference, not active in final code) |
| ArduinoJson | Benoit Blanchon | 7.x | JSON parsing for API responses |

The following are built-in to ESP32 Arduino core (no install needed):

| Built-in | Purpose |
|----------|---------|
| WiFi.h | WiFi connection |
| HTTPClient.h | HTTPS API calls |
| SPIFFS.h | Flash filesystem |
| driver/i2s.h | I2S audio driver |

---

## 🔑 API Setup — Groq (Free, No Credit Card)

1. Go to **[console.groq.com](https://console.groq.com)**
2. Sign up with any Google or GitHub account
3. Navigate to **API Keys** in the left sidebar
4. Click **Create API Key**
5. Copy the key — it starts with `gsk_...`
6. Paste it into the sketch as `GROQ_API_KEY`

### Free Tier Limits (as of 2025)

| Model | Free Limit |
|-------|-----------|
| Whisper (STT) | 28,800 audio seconds/day |
| Llama 3.3 70B | 1,000 requests/day |
| Orpheus TTS | Generous free tier |

This is more than enough for personal use. No billing setup required.

---

## 📁 Repository Structure

```
VoiceCore-ESP32/
│
├── VoiceCore_ESP32.ino          # Main sketch — upload this
├── README.md                    # This file
├── LICENSE                      # MIT License
│
├── docs/
│   ├── TROUBLESHOOTING.md       # Common errors and fixes
│   ├── TUNING.md                # How to tune wake threshold and silence detection
│   └── ARCHITECTURE.md         # Deep dive into how each stage works
│
└── schematics/
    └── wiring_diagram.md        # ASCII wiring reference
```

---

## 🚀 Quickstart

### Step 1 — Clone the repository
```bash
git clone https://github.com/yourusername/VoiceCore-ESP32.git
cd VoiceCore-ESP32
```

### Step 2 — Open in Arduino IDE
Open `VoiceCore_ESP32.ino` in Arduino IDE 2.x

### Step 3 — Configure your credentials
Edit the top of the file:
```cpp
#define WIFI_SSID     "your_wifi_name"
#define WIFI_PASSWORD "your_wifi_password"
#define GROQ_API_KEY  "gsk_your_key_here"
```

### Step 4 — Set board settings
Follow the Arduino IDE Setup table above exactly.

### Step 5 — Upload
Connect ESP32-S3 via USB, select the correct COM port, click Upload.

### Step 6 — Open Serial Monitor
Set baud rate to **115200**. You should see:
```
==============================
  ESP32-S3 Voice Assistant
  Powered by Groq
==============================

[OK] PSRAM: 8386096 bytes free
[OK] SPIFFS mounted
[OK] Record buffer: 128000 samples (8 sec)
[OK] WiFi: 192.168.1.x
[OK] Microphone ready

==============================
  READY — clap or speak loud!
==============================
```

### Step 7 — Use it
**Clap loudly** or make a loud noise to wake. Then speak your question. Wait for the response to play through the speaker.

---

## 🎛️ Tuning Parameters

All tuning is done at the top of the sketch:

```cpp
#define BIT_SHIFT         14    // Mic sensitivity scaling
#define WAKE_THRESHOLD    5000  // Amplitude to trigger recording
#define SILENCE_THRESHOLD 450   // Amplitude considered silence
#define SILENCE_MS        1500  // Ms of silence before stopping record
#define MAX_RECORD_MS     8000  // Maximum recording length
```

### How to tune for your environment

| Problem | What to change |
|---------|---------------|
| Never wakes up | Lower `WAKE_THRESHOLD` to 3000 |
| Wakes up from noise / TV | Raise `WAKE_THRESHOLD` to 8000 |
| Cuts off mid-sentence | Lower `SILENCE_THRESHOLD` to 200, raise `SILENCE_MS` to 2500 |
| Records silence for too long | Raise `SILENCE_THRESHOLD` to 800 |
| Audio too quiet / distorted | Adjust `BIT_SHIFT` (11–16 range) |
| Idle level not near zero | Raise `BIT_SHIFT` by 1 |

---

## 🔊 How the Audio Pipeline Works

### Recording
The INMP441 outputs 32-bit I2S frames containing 24-bit audio in the upper bits. The code reads 32-bit frames and shifts right by `BIT_SHIFT` to extract 16-bit PCM samples stored in PSRAM.

### Wake Detection
Every 256-sample chunk is scanned for peak amplitude. When peak exceeds `WAKE_THRESHOLD`, recording begins immediately including the trigger chunk so the first word is never lost.

### Silence Detection
During recording, when peak drops below `SILENCE_THRESHOLD` for `SILENCE_MS` milliseconds continuously, recording stops. The silence timer resets if any sound exceeds the threshold.

### Speech to Text
The recorded PCM samples are wrapped in a proper WAV header and sent as a multipart HTTP POST to Groq's Whisper endpoint. The response is a JSON object with a `text` field.

### LLM Answer
The transcript is sent to Groq's Llama 3.3 70B model as a chat completion request. The system prompt constrains answers to one short sentence for fast TTS.

### Text to Speech
The answer text is sent to Groq's Orpheus TTS model. The response is a WAV audio stream. The code reads the entire response into PSRAM, parses the WAV header (skipping LIST/INFO metadata chunks), extracts the raw PCM data, and plays it directly via I2S — no filesystem involved.

### Playback
The PCM buffer plays through I2S with:
- DMA pre-filled with silence to prevent start click
- Soft fade-in on first chunk to prevent start spike
- `tx_desc_auto_clear = true` so underruns output silence not garbage
- APLL clock for precise sample rate — eliminates drift artifacts
- Silence flush through DMA before driver uninstall to prevent end click

---

## 📊 Performance Benchmarks

Measured on ESP32-S3 @ 240MHz with good WiFi:

| Stage | Typical Time |
|-------|-------------|
| Wake detection | Instant (real-time) |
| Recording (2 sec question) | 2 sec + 1.5 sec silence = 3.5 sec |
| Upload to Whisper | 1–2 sec |
| Whisper transcription | 0.5–1 sec |
| Llama 3.3 response | 0.5–1 sec |
| Orpheus TTS generation | 1–3 sec |
| Playback | Real-time |
| **Total round trip** | **~7–10 seconds** |

---

## 🐛 Troubleshooting

### Board crashes on boot
- Check: Tools → PSRAM → OPI PSRAM
- Check: Tools → Partition Scheme → Huge APP

### No sound from speaker
- VIN on MAX98357A must be 5V not 3.3V
- Speaker − wire must go to OUT− not GND
- SD pin must be floating (not connected to GND)

### Mic shows zero always
- L/R pin must be connected to GND
- VDD must be 3.3V not 5V
- Check GPIO numbers match your board

### Wakes up randomly
- Raise `WAKE_THRESHOLD` to 8000 or higher
- Move the mic away from the speaker physically

### Whisper returns empty string
- Check your Groq API key is correct
- Check WiFi is connected (see serial monitor)
- Your recording might be too short — speak for at least 1 second

### TTS plays garbled audio
- This was caused by LIST/INFO WAV chunks being played as audio
- The current code parses and skips all non-data chunks correctly
- If it recurs, check serial for `[TTS] Chunk:` lines

### `I2S port 0 has not installed` warning
- Harmless warning from calling `i2s_driver_uninstall` on a port not yet installed
- Does not affect functionality

---

## 🧠 Technical Deep Dive

### Why PSRAM?
The ESP32-S3 has only 512KB of internal SRAM. Recording 8 seconds at 16kHz = 256KB for mic buffer alone. The TTS response adds another 300–600KB. PSRAM (8MB on OPI variant) is essential. All large buffers use `ps_malloc()`.

### Why Legacy I2S Driver?
The ESP32-S3 has both a legacy (`driver/i2s.h`) and new (`driver/i2s_std.h`) I2S API. The ESP32-audioI2S library uses the legacy driver internally. If you use the new driver for the mic simultaneously, you get a CONFLICT error and crash. The final code uses legacy driver for both mic and speaker on separate I2S peripherals (I2S_NUM_0 for speaker, I2S_NUM_1 for mic).

### Why No SPIFFS for Audio?
SPIFFS on ESP32 has a maximum file size limit of 262,144 bytes (256KB). A typical 3-second TTS response is 300–500KB at 24kHz. Files larger than 256KB get silently truncated, causing incomplete or corrupted audio. The final design bypasses SPIFFS entirely for TTS — audio is received and played entirely from PSRAM.

### Why Does Groq WAV Have `FF FF FF FF` Size?
Groq streams the TTS response using chunked HTTP transfer. Because the total size is unknown before generation completes, the WAV header is written with `0xFFFFFFFF` as the data chunk size — a valid streaming WAV convention. The code detects this and falls back to using the actual received byte count.

### The LIST/INFO Chunk Problem
Groq's Orpheus TTS embeds a `LIST INFO` metadata chunk between the `fmt ` chunk and the `data` chunk. This chunk contains software version info (e.g. `Lavf61.7.100`). A naive WAV parser that doesn't handle this chunk will misidentify it as audio data, producing a loud buzz at the start of every response. The code explicitly scans and skips all non-`data` sub-chunks.

---

## 🛠️ Possible Extensions

| Feature | Difficulty | Notes |
|---------|-----------|-------|
| LED status indicator | Easy | Add WS2812B to show IDLE/RECORDING/THINKING/SPEAKING states |
| Push-to-talk button | Easy | Replace amplitude trigger with GPIO button for quieter environments |
| Multiple languages | Easy | Change `language` field in STT call |
| Different TTS voice | Easy | Change `voice` field: troy, tara, leah, jess, leo, dan, mia, zac, zoe |
| Custom wake word | Medium | Replace amplitude trigger with TensorFlow Lite micro model |
| Conversation memory | Medium | Maintain chat history array across turns |
| Home automation | Medium | Parse LLM output for commands, trigger GPIO |
| Battery powered | Medium | Add LiPo + charging circuit, deep sleep between queries |
| MQTT integration | Medium | Publish transcripts to Home Assistant |
| Local LLM | Hard | Run quantized Llama via llama.cpp on a Raspberry Pi on local network |

---

## 📜 License

MIT License — free to use, modify, and distribute for any purpose.

---

## 🙏 Acknowledgements

- **Groq** — for providing genuinely fast, genuinely free AI inference
- **schreibfaul1** — for the ESP32-audioI2S library
- **Benoit Blanchon** — for ArduinoJson
- **Espressif** — for the ESP32-S3 and its Arduino core
- **InvenSense** — INMP441 datasheet and application notes
- **Maxim Integrated** — MAX98357A datasheet

---

*Built from scratch — wired, debugged, and documented one serial line at a time.*
