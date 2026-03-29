# Tuning Guide

## The 4 Key Parameters

All tuning is done by changing the `#define` values at the top of `VoiceCore_ESP32.ino`.

---

## BIT_SHIFT (default: 14)

Controls how the raw 32-bit INMP441 output is scaled to 16-bit.

The INMP441 sends audio in the upper 24 bits of a 32-bit frame. Shifting right extracts it.

| BIT_SHIFT | Effect |
|-----------|--------|
| 11 | Maximum sensitivity — all values large, clips easily |
| 13 | High sensitivity |
| 14 | Default — balanced for most environments |
| 15 | Lower sensitivity |
| 16 | Minimum sensitivity — very quiet values |

**How to find the right value:**
1. Upload the code and watch the Idle meter in serial monitor
2. Stay completely silent
3. Idle values should be under 500
4. If idle is near 32767 → increase BIT_SHIFT by 1
5. If idle is always 0 even when speaking → decrease BIT_SHIFT by 1

---

## WAKE_THRESHOLD (default: 5000)

The peak amplitude required to trigger recording.

| Environment | Suggested Value |
|-------------|----------------|
| Very quiet room | 3000 |
| Normal room | 5000 |
| Noisy room / TV on | 8000–12000 |
| Near speaker output | 10000+ |

**How to tune:**
- Watch idle meter, note the highest value you see in background noise
- Set WAKE_THRESHOLD to 2× that value
- Example: idle noise peaks at 800 → set WAKE_THRESHOLD to 1600

---

## SILENCE_THRESHOLD (default: 450)

The peak amplitude below which a chunk is considered silence.

| BIT_SHIFT | Good SILENCE_THRESHOLD |
|-----------|----------------------|
| 11 | 1500 |
| 14 | 450 |
| 16 | 150 |

**How to tune:**
- After setting BIT_SHIFT, speak then stop
- Watch REC meter — note what value it shows during silence
- Set SILENCE_THRESHOLD to 1.5× that idle value

---

## SILENCE_MS (default: 1500)

How many milliseconds of continuous silence triggers end of recording.

| Use case | Suggested Value |
|----------|----------------|
| Quick commands ("What time is it") | 1000 |
| Normal conversation | 1500 |
| Long pauses while thinking | 2500 |
| Slow speakers | 3000 |

---

## Quick Tuning Procedure

1. Upload with default settings
2. Watch serial monitor idle meter — note idle amplitude
3. Set `WAKE_THRESHOLD` = idle peak × 3
4. Clap to wake, speak for 2 seconds, stop
5. Watch REC meter during silence — note silence amplitude  
6. Set `SILENCE_THRESHOLD` = silence amplitude × 1.5
7. Test — if it cuts off too early, raise `SILENCE_MS` to 2000
8. If it takes too long to stop, lower `SILENCE_MS` to 1000

---

## Changing TTS Voice

In `fetchTTSintoPSRAM()`, change the `voice` field:

```cpp
doc["voice"] = "troy";   // change this
```

Available Orpheus voices:

| Voice | Character |
|-------|-----------|
| troy | Male, clear, neutral |
| tara | Female, warm |
| leah | Female, professional |
| jess | Female, friendly |
| leo | Male, deep |
| dan | Male, conversational |
| mia | Female, expressive |
| zac | Male, energetic |
| zoe | Female, calm |

---

## Changing LLM Model

In `getAnswer()`, change the `model` field:

```cpp
doc["model"] = "llama-3.3-70b-versatile";
```

Available Groq models (free tier):

| Model | Speed | Quality | Best for |
|-------|-------|---------|---------|
| llama-3.3-70b-versatile | Medium | Highest | General questions |
| llama3-8b-8192 | Fastest | Good | Simple commands |
| mixtral-8x7b-32768 | Fast | Very good | Long context |

---

## Changing Response Language

In `transcribeAudio()`, change the language field:

```cpp
"Content-Disposition: form-data; name=\"language\"\r\n\r\nen\r\n"
```

Replace `en` with any ISO 639-1 language code:
- `ur` — Urdu
- `ar` — Arabic  
- `de` — German
- `fr` — French
- `es` — Spanish
- `zh` — Chinese

Note: TTS voice must also support the language. Orpheus currently best supports English.
