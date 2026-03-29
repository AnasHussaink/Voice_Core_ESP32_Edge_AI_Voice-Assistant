# Architecture Deep Dive

## Why These Design Decisions Were Made

---

## I2S Driver — Legacy vs New API

The ESP32-S3 has two I2S APIs:
- **Legacy:** `driver/i2s.h` — older, widely compatible
- **New:** `driver/i2s_std.h` — modern, more features

The ESP32-audioI2S library (used for reference) uses the **legacy driver internally**.  
If you use the new driver for the mic on the same core, you get:
```
CONFLICT! The new i2s driver can't work along with the legacy i2s driver
```
This causes an immediate crash. The final code uses the **legacy driver for both** I2S peripherals — mic on `I2S_NUM_1` and speaker on `I2S_NUM_0` — eliminating the conflict entirely.

---

## PSRAM — Why Everything Goes There

ESP32-S3 internal SRAM: **512KB**  
Required allocations:

| Buffer | Size |
|--------|------|
| 8 sec mic recording (16kHz × 2B × 8s) | 256KB |
| WAV + multipart body for Whisper | ~260KB |
| TTS raw response buffer | ~600KB |
| TTS PCM playback buffer | ~300KB |

Total needed: **~1.4MB** — 3× more than internal SRAM.  
Solution: `ps_malloc()` allocates in PSRAM (8MB on OPI variant). Never use `malloc()` for large buffers.

---

## Why SPIFFS Is Not Used for Audio

SPIFFS on ESP32 has a **hard 262,144 byte (256KB) per-file limit**.

A typical Groq Orpheus TTS response:
- 3 second sentence → ~300KB at 24kHz 16-bit mono
- 5 second sentence → ~500KB

Files larger than 256KB are **silently truncated**. The audio plays then abruptly cuts off.

The final design streams the TTS response directly into PSRAM and plays from there — no SPIFFS involved in the audio path. SPIFFS is mounted but reserved for config/future use.

---

## The LIST/INFO Chunk Problem

Groq's Orpheus TTS WAV output has this structure:
```
RIFF ....
  WAVE
    fmt  (audio format: 24kHz, mono, 16-bit)
    LIST (metadata: "Lavf61.7.100")
      INFO
        ISFT "Lavf61.7.100"
    data (actual PCM audio samples)
```

A naive WAV parser that only looks for `fmt ` then `data` will find:
1. `fmt ` — correct
2. `LIST` — misidentified as `data` → plays metadata as audio = **loud buzz**

The fix: scan all sub-chunks by ID, skip everything that is not `data`, stop when `data` is found. This is implemented in the chunk parsing loop in `fetchTTSintoPSRAM()`.

---

## The 0xFFFFFFFF Data Size Problem

Standard WAV format requires knowing the total data size before writing the header. When streaming audio in real-time (as Groq does), the size is unknown at the time the header is written.

Convention: write `0xFFFFFFFF` as a placeholder meaning "unknown / read until EOF".

The code detects this:
```cpp
dataBytes = (chunkSize == 0xFFFFFFFF) ? (uint32_t)remaining : chunkSize;
```

If size is `0xFFFFFFFF`, use the actual number of bytes received up to the end of the stream.

---

## Audio Spike Root Causes and Fixes

### Start spike
**Cause:** DMA buffers contain random garbage when I2S driver first starts. The first few milliseconds of output are noise before real audio arrives.  
**Fix:** Write 32,768 bytes of silence (16 buffers × 1024 samples × 2 bytes) immediately after `initSpeaker()`. This fills all DMA descriptors with zeros before any real audio is queued.

### Start click
**Cause:** Abrupt jump from 0 (silence) to the first non-zero sample of the audio.  
**Fix:** Fade-in the first chunk. Scale each sample by `i/n` where `i` is position in chunk and `n` is chunk size. This ramps from 0 to full amplitude over ~512 samples (~10ms at 48kHz).

### Mid-audio noise
**Cause:** DMA buffer underrun — I2S hardware runs out of data and outputs the last buffer contents (garbage) instead of silence.  
**Fix:** `tx_desc_auto_clear = true` in the I2S config. This makes the hardware automatically output zeros on underrun instead of repeating stale data.

**Secondary cause:** The LIST chunk being parsed as audio data.  
**Fix:** Proper sub-chunk parsing (see above).

### End spike
**Cause:** `i2s_driver_uninstall()` stops the DMA immediately. Any samples still in the DMA pipeline are cut off abruptly, causing a click.  
**Fix:** Write 8 × 4096 bytes of silence after the last audio sample in `stopSpeaker()`. This pushes the real audio through the pipeline to completion before the driver is stopped.

### Clock drift noise
**Cause:** Default I2S clock uses the APB bus clock (80MHz) divided down. This produces a slightly imprecise sample rate that accumulates as jitter.  
**Fix:** `use_apll = true` uses the hardware Audio PLL to generate the exact sample rate (e.g. exactly 24000.00 Hz instead of 23999.6 Hz). This eliminates drift-related artifacts entirely.

---

## State Machine Design

```
IDLE ──(peak > WAKE_THRESHOLD)──► RECORDING
  ▲                                    │
  │                                    │ (peak < SILENCE_THRESHOLD
  │                                    │  for SILENCE_MS ms)
  │                                    │  OR
  │                                    │ (recordCount >= MAX_SAMPLES)
  │                                    ▼
  └────────────────────────────── SENDING
                                  STT → LLM → TTS → play
                                  (synchronous, blocks loop)
```

The SENDING state is synchronous — all three API calls and playback happen in sequence before returning to IDLE. This is intentional: it's simpler, more reliable, and appropriate for a single-user device. A future async design would use FreeRTOS tasks.

---

## HTTP Multipart for Whisper

Groq's Whisper endpoint requires `multipart/form-data` exactly like OpenAI's:

```
POST /openai/v1/audio/transcriptions
Content-Type: multipart/form-data; boundary=ESP32Boundary

--ESP32Boundary
Content-Disposition: form-data; name="model"

whisper-large-v3-turbo
--ESP32Boundary
Content-Disposition: form-data; name="file"; filename="audio.wav"
Content-Type: audio/wav

[binary WAV data]
--ESP32Boundary--
```

The entire body is assembled in PSRAM as a single contiguous buffer and sent in one `http.POST()` call. This is more reliable than streaming the body in chunks, which some servers reject.
