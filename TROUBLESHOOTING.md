# Troubleshooting Guide

## Boot Issues

### `FATAL: PSRAM not found!`
- Go to **Tools → PSRAM → OPI PSRAM**
- Make sure you have an ESP32-S3 with embedded PSRAM (QFN56 variant)

### `FATAL: SPIFFS mount failed!`
- Go to **Tools → Partition Scheme → Huge APP (3MB No OTA / 1MB SPIFFS)**
- Try uploading with Erase Flash option enabled once

### Crashes immediately on boot (core dump)
- Wrong partition scheme — use Huge APP
- Wrong PSRAM setting — use OPI PSRAM
- Never include headers inside functions

### `FATAL: WiFi failed!`
- Double-check SSID and password (case sensitive)
- Make sure your network is 2.4GHz (ESP32 does not support 5GHz)
- Move closer to router

---

## Microphone Issues

### Idle level stuck at 0 always
- L/R pin on INMP441 must be connected to GND — not floating
- Check GPIO numbers match your board and the defines in code

### Mic shows random large values at idle (32767 always)
- VDD connected to 5V — must be 3.3V only
- Increase BIT_SHIFT from 14 to 15 or 16

### Never wakes up even when clapping
- Lower WAKE_THRESHOLD (try 2000 or 1000)
- Check BIT_SHIFT — if too high, all values near zero

### Wakes up from background noise / TV
- Raise WAKE_THRESHOLD (try 8000 or 12000)
- Move mic away from speaker

### Cuts off mid-sentence
- Lower SILENCE_THRESHOLD to 200
- Raise SILENCE_MS to 2500

### Records too long after you stop speaking
- Raise SILENCE_THRESHOLD to 800 or 1000

---

## Speaker Issues

### No sound at all
- VIN on MAX98357A must be 5V — not 3.3V
- Check speaker wires are firmly connected
- SD pin must be floating (not connected to GND)

### Very quiet
- Connect GAIN pin on MAX98357A to 3.3V for +12dB gain

### Crackling or buzzing
- Speaker negative wire must go to OUT− on MAX98357A, never to GND
- Check for loose connections

### `E (xxxxx) i2s(legacy): I2S port 0 has not installed`
- This is a harmless warning from calling uninstall on an already-stopped port
- Does not affect functionality, can be ignored

---

## API Issues

### `[STT] ERROR HTTP 401`
- Invalid Groq API key
- Check key starts with `gsk_` and is copied in full

### `[STT] ERROR HTTP 429`
- Rate limit hit on free tier
- Wait a few minutes and try again
- Free tier: 28,800 audio seconds per day

### `[STT] Transcript: ""`
- Recording too short — speak for at least 1 full second
- Recording too quiet — lower WAKE_THRESHOLD so you can speak at normal volume

### `[LLM] ERROR HTTP 413`
- Request too large — this shouldn't happen with short recordings

### `[TTS] ERROR HTTP 400`
- Answer text contains special characters
- The code truncates to 190 characters automatically

### `[TTS] ERROR: RIFF header not found`
- Groq API returned an error body instead of audio
- Check your API key and quota

---

## Audio Quality Issues

### Noise / spike at the start of playback
- Pre-fill DMA is in the code — make sure `use_apll = true`
- Make sure `tx_desc_auto_clear = true`

### Noise / spike in the middle
- Was caused by LIST/INFO WAV chunk being played as audio
- Fixed in current code — WAV parser skips all non-data chunks
- Check serial for `[TTS] Chunk: [LIST]` — should be skipped

### Noise / spike at the end
- stopSpeaker() flushes silence through DMA before uninstalling
- Make sure you are using the latest version of the code

### Audio cuts off before sentence finishes
- SPIFFS 256KB limit was truncating the file
- Current code bypasses SPIFFS entirely — audio loads into PSRAM directly
- If audio is very long, increase `maxBytes` in fetchTTSintoPSRAM (currently 600000)

### Audio sounds robotic / distorted
- Wrong sample rate — code reads sample rate from WAV header automatically
- Make sure `use_apll = true` in initSpeaker()
