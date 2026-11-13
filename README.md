# VoiceCore ESP32 — Edge AI Voice Assistant

A compact voice-assistant prototype built around an ESP32-S3, INMP441 microphone, and MAX98357A speaker amplifier. Audio capture, buffering, device control, and playback run on the ESP32-S3 while configured AI services provide speech recognition, language reasoning, and text-to-speech.

## Architecture

INMP441 → I2S → ESP32-S3 → trigger → PSRAM recording → WAV upload → speech recognition → LLM → TTS → I2S speaker output

Runtime state flow:

IDLE → RECORDING → SENDING → RESPONSE → PLAYBACK → IDLE

## Hardware

| Component | Purpose |
|---|---|
| ESP32-S3 | Main controller and Wi-Fi |
| INMP441 | Digital microphone |
| MAX98357A | I2S Class-D amplifier |
| Speaker | Voice output |

Microphone: GPIO18 SCK, GPIO17 WS, GPIO15 SD, L/R → GND.  
Speaker: GPIO3 BCLK, GPIO8 LRC, GPIO16 DIN.

## Software Stack

- C++
- ESP32-S3 / Arduino
- I2S
- PSRAM
- Wi-Fi / HTTP
- WAV audio handling
- Whisper-based speech recognition
- Llama-family LLM backend
- TTS playback

## Engineering Highlights

- Large audio buffers allocated in PSRAM.
- Multipart audio requests for speech recognition.
- WAV parsing that accounts for non-trivial chunk layouts and data-size edge cases.
- State-machine separation of capture, networking, and playback.
- Independent I2S input and output paths.
- Amplitude monitoring and audio protection.

## Project Structure

src/ — firmware source  
include/ — headers and configuration  
ARCHITECTURE.md — detailed design decisions  
README.md  
LICENSE

## Setup

1. Install ESP32 board support in Arduino IDE.
2. Configure the ESP32-S3 and PSRAM.
3. Wire the microphone and amplifier.
4. Configure AI credentials locally.
5. Upload the firmware.
6. Monitor serial output during capture and playback.

Never commit API keys or credentials.

## Configuration Notes

The firmware is designed around constrained embedded resources. PSRAM is used for large audio buffers and persistent audio storage is minimized where possible.

See ARCHITECTURE.md for implementation notes and troubleshooting.

## Roadmap

- Expand local/edge inference options.
- Improve wake-word detection.
- Improve playback interruption handling.
- Add richer device diagnostics.

## License

See LICENSE.


## Development and quality

[![Repository quality](https://github.com/AnasHussaink/Voice_Core_ESP32_Edge_AI_Voice-Assistant/actions/workflows/quality.yml/badge.svg)](https://github.com/AnasHussaink/Voice_Core_ESP32_Edge_AI_Voice-Assistant/actions/workflows/quality.yml)

See [CONTRIBUTING.md](CONTRIBUTING.md) for branch/PR workflow, local checks and CI coverage.

## Technical scope

ESP32-S3 voice assistant firmware using Arduino C++, I2S audio, PSRAM, WAV encoding, Wi-Fi and cloud speech/LLM APIs. PlatformIO compiles the original sketch through `pio-src/voicecore.cpp`.
