// ╔══════════════════════════════════════════════════════════════╗
// ║           VoiceCore ESP32 — Edge AI Voice Assistant          ║
// ║                                                              ║
// ║  Hardware : ESP32-S3 + INMP441 mic + MAX98357A + Speaker     ║
// ║  AI Stack : Groq Whisper (STT) + Llama 3.3 (LLM) +          ║
// ║             Orpheus TTS                                       ║
// ║  License  : MIT                                              ║
// ║  Repo     : github.com/yourusername/VoiceCore-ESP32          ║
// ╚══════════════════════════════════════════════════════════════╝

#include "WiFi.h"
#include "HTTPClient.h"
#include "ArduinoJson.h"
#include "SPIFFS.h"
#include "driver/i2s.h"

// ═══════════════════════════════════════════════
//  CONFIGURE THESE
// ═══════════════════════════════════════════════
#define WIFI_SSID "your_wifi_name"
#define WIFI_PASSWORD "your_wifi_password"
#define GROQ_API_KEY "gsk_..." // from console.groq.com — free, no card

// ── Speaker pins (MAX98357A) ─────────────────────
#define SPK_BCLK 3
#define SPK_LRC 8
#define SPK_DOUT 16

// ── Microphone pins (INMP441) ────────────────────
#define MIC_SCK 18
#define MIC_WS 17
#define MIC_SD 15

// ── Tuning — adjust for your environment ─────────
#define SAMPLE_RATE 16000     // mic sample rate (Hz)
#define BIT_SHIFT 14          // mic gain scaling (11–16 range)
#define WAKE_THRESHOLD 5000   // amplitude to start recording
#define SILENCE_THRESHOLD 450 // amplitude considered silence
#define SILENCE_MS 1500       // ms of silence before stopping
#define MAX_RECORD_MS 8000    // max recording length (ms)
#define MAX_SAMPLES (SAMPLE_RATE * (MAX_RECORD_MS / 1000))

// ═══════════════════════════════════════════════
//  STATE MACHINE
// ═══════════════════════════════════════════════
enum State { IDLE, RECORDING, SENDING };
State currentState = IDLE;

// ═══════════════════════════════════════════════
//  GLOBALS
// ═══════════════════════════════════════════════
int16_t *recordBuf = nullptr; // mic recording buffer (PSRAM)
int recordCount = 0;
unsigned long silenceStart = 0;
bool silenceStarted = false;
static int32_t mic_buf[256]; // I2S read buffer (32-bit INMP441 frames)

// ═══════════════════════════════════════════════
//  WAV HEADER BUILDER
//  Wraps raw PCM data in a valid WAV container
//  so Groq Whisper can parse it correctly.
// ═══════════════════════════════════════════════
void buildWavHeader(uint8_t *h, int dataBytes) {
  int total = dataBytes + 36;
  h[0] = 'R';
  h[1] = 'I';
  h[2] = 'F';
  h[3] = 'F';
  h[4] = total & 0xFF;
  h[5] = (total >> 8) & 0xFF;
  h[6] = (total >> 16) & 0xFF;
  h[7] = (total >> 24) & 0xFF;
  h[8] = 'W';
  h[9] = 'A';
  h[10] = 'V';
  h[11] = 'E';
  h[12] = 'f';
  h[13] = 'm';
  h[14] = 't';
  h[15] = ' ';
  h[16] = 16;
  h[17] = 0;
  h[18] = 0;
  h[19] = 0;
  h[20] = 1;
  h[21] = 0; // PCM format
  h[22] = 1;
  h[23] = 0; // mono
  h[24] = SAMPLE_RATE & 0xFF;
  h[25] = (SAMPLE_RATE >> 8) & 0xFF;
  h[26] = (SAMPLE_RATE >> 16) & 0xFF;
  h[27] = (SAMPLE_RATE >> 24) & 0xFF;
  int br = SAMPLE_RATE * 2;
  h[28] = br & 0xFF;
  h[29] = (br >> 8) & 0xFF;
  h[30] = (br >> 16) & 0xFF;
  h[31] = (br >> 24) & 0xFF;
  h[32] = 2;
  h[33] = 0; // block align
  h[34] = 16;
  h[35] = 0; // bits per sample
  h[36] = 'd';
  h[37] = 'a';
  h[38] = 't';
  h[39] = 'a';
  h[40] = dataBytes & 0xFF;
  h[41] = (dataBytes >> 8) & 0xFF;
  h[42] = (dataBytes >> 16) & 0xFF;
  h[43] = (dataBytes >> 24) & 0xFF;
}

// ═══════════════════════════════════════════════
//  SPEAKER — init and stop
//  Uses I2S_NUM_0 (TX only)
//  APLL clock: exact sample rate, eliminates drift
//  tx_desc_auto_clear: underruns output silence
//  Large DMA (16×1024): prevents buffer underruns
// ═══════════════════════════════════════════════
void initSpeaker(uint32_t sampleRate) {
  i2s_driver_uninstall(I2S_NUM_0);
  i2s_config_t cfg = {.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
                      .sample_rate = sampleRate,
                      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
                      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
                      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
                      .intr_alloc_flags = 0,
                      .dma_buf_count = 16,
                      .dma_buf_len = 1024,
                      .use_apll = true,           // hardware PLL — exact clock, no drift spikes
                      .tx_desc_auto_clear = true, // underrun → silence not garbage
                      .fixed_mclk = 0};
  i2s_pin_config_t pins = {.bck_io_num = SPK_BCLK,
                           .ws_io_num = SPK_LRC,
                           .data_out_num = SPK_DOUT,
                           .data_in_num = I2S_PIN_NO_CHANGE};
  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_zero_dma_buffer(I2S_NUM_0);
  Serial.printf("[SPK] Ready @ %u Hz\n", sampleRate);
}

void stopSpeaker() {
  // Drain silence through DMA pipeline before uninstalling
  // This prevents the end-of-audio click/spike
  int16_t *silence = (int16_t *)ps_malloc(4096);
  if (silence) {
    memset(silence, 0, 4096);
    size_t w = 0;
    for (int i = 0; i < 8; i++)
      i2s_write(I2S_NUM_0, silence, 4096, &w, portMAX_DELAY);
    free(silence);
  }
  delay(80);
  i2s_driver_uninstall(I2S_NUM_0);
}

// ═══════════════════════════════════════════════
//  MIC — init and stop
//  Uses I2S_NUM_1 (RX only)
//  INMP441 outputs 32-bit frames, audio in upper 24 bits
//  Must read 32-bit and shift right to extract audio
// ═══════════════════════════════════════════════
void initMic() {
  i2s_driver_uninstall(I2S_NUM_1);
  i2s_config_t cfg = {.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
                      .sample_rate = SAMPLE_RATE,
                      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT, // INMP441 MUST be 32-bit
                      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
                      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
                      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
                      .dma_buf_count = 8,
                      .dma_buf_len = 512,
                      .use_apll = false,
                      .tx_desc_auto_clear = false,
                      .fixed_mclk = 0};
  i2s_pin_config_t pins = {.bck_io_num = MIC_SCK,
                           .ws_io_num = MIC_WS,
                           .data_out_num = I2S_PIN_NO_CHANGE,
                           .data_in_num = MIC_SD};
  i2s_driver_install(I2S_NUM_1, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &pins);
  i2s_zero_dma_buffer(I2S_NUM_1);
  Serial.println("[OK] Microphone ready");
}

void stopMic() { i2s_driver_uninstall(I2S_NUM_1); }

// ═══════════════════════════════════════════════
//  PLAY PCM FROM PSRAM
//  Plays raw 16-bit mono PCM directly from a
//  PSRAM buffer — no SPIFFS, no file parsing,
//  no size limits.
//
//  Fixes applied:
//  - Pre-fill DMA with silence (no start spike)
//  - Soft fade-in on first chunk (no start click)
//  - APLL + tx_desc_auto_clear (no mid-audio noise)
//  - vTaskDelay(1) per chunk (no watchdog reset)
//  - stopSpeaker() flushes silence (no end spike)
// ═══════════════════════════════════════════════
void playPCM(int16_t *samples, int numSamples, uint32_t sampleRate) {
  if (!samples || numSamples <= 0) {
    Serial.println("[SPK] No samples to play");
    return;
  }

  initSpeaker(sampleRate);

  // Pre-fill ALL DMA buffers with silence
  // 16 buffers × 1024 samples × 2 bytes = 32768 bytes
  // This ensures DMA is ready before first real sample arrives
  {
    const int preFill = 16 * 1024 * 2;
    int16_t *sil = (int16_t *)ps_malloc(preFill);
    if (sil) {
      memset(sil, 0, preFill);
      size_t w = 0;
      i2s_write(I2S_NUM_0, sil, preFill, &w, portMAX_DELAY);
      free(sil);
    }
  }
  delay(150);

  const int CHUNK = 512;
  int16_t *outBuf = (int16_t *)ps_malloc(CHUNK * 4);
  if (!outBuf) {
    Serial.println("[SPK] ps_malloc failed for output buffer");
    stopSpeaker();
    return;
  }

  int pos = 0;
  bool firstChunk = true;
  size_t written = 0;

  while (pos < numSamples) {
    int n = min(CHUNK, numSamples - pos);

    for (int i = 0; i < n; i++) {
      int16_t s = samples[pos + i];

      // Soft fade-in on first chunk — eliminates start click
      if (firstChunk)
        s = (int16_t)(s * ((float)i / n));

      outBuf[i * 2] = s;     // left channel
      outBuf[i * 2 + 1] = s; // right channel (duplicate mono)
    }

    i2s_write(I2S_NUM_0, outBuf, n * 4, &written, portMAX_DELAY);
    pos += n;
    firstChunk = false;
    vTaskDelay(1); // yield to prevent watchdog reset
  }

  free(outBuf);
  stopSpeaker();
  Serial.printf("[SPK] Played %d samples @ %u Hz (%.1f sec)\n", numSamples, sampleRate,
                (float)numSamples / sampleRate);
}

// ═══════════════════════════════════════════════
//  GROQ TTS — fetch into PSRAM
//
//  Key problems solved here:
//
//  1. Groq sends chunked HTTP — read ALL bytes first,
//     then parse. Never parse while streaming.
//
//  2. WAV data size = 0xFFFFFFFF (streaming unknown).
//     Code detects this and uses actual received bytes.
//
//  3. Groq embeds LIST/INFO metadata chunk between
//     fmt and data chunks. A naive parser plays this
//     as audio (loud buzz). Code skips all non-data chunks.
//
//  4. SPIFFS has 256KB file size limit — TTS audio is
//     300–600KB. Bypass SPIFFS entirely, use PSRAM.
// ═══════════════════════════════════════════════
int fetchTTSintoPSRAM(const String &text, int16_t **outBuf, uint32_t *sampleRate_out) {
  String t = (text.length() > 190) ? text.substring(0, 190) : text;
  Serial.printf("[TTS] Fetching: \"%s\"\n", t.c_str());

  JsonDocument doc;
  doc["model"] = "canopylabs/orpheus-v1-english";
  doc["input"] = t;
  doc["voice"] = "troy"; // options: troy, tara, leah, jess, leo, dan, mia, zac, zoe
  doc["response_format"] = "wav";
  String body;
  serializeJson(doc, body);

  HTTPClient http;
  http.begin("https://api.groq.com/openai/v1/audio/speech");
  http.addHeader("Authorization", String("Bearer ") + GROQ_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(30000);

  int httpCode = http.POST(body);
  if (httpCode != 200) {
    Serial.printf("[TTS] ERROR HTTP %d: %s\n", httpCode, http.getString().c_str());
    http.end();
    return 0;
  }

  // ── Read entire response into PSRAM first ──────
  const int maxBytes = 600000; // 600KB — fits comfortably in 8MB PSRAM
  uint8_t *rawBuf = (uint8_t *)ps_malloc(maxBytes);
  if (!rawBuf) {
    Serial.println("[TTS] ERROR: ps_malloc for raw buffer failed");
    http.end();
    return 0;
  }

  WiFiClient *stream = http.getStreamPtr();
  int rawTotal = 0;
  unsigned long lastData = millis();

  while (rawTotal < maxBytes) {
    if (millis() - lastData > 8000) {
      Serial.println("[TTS] Stream timeout");
      break;
    }
    int avail = stream->available();
    if (avail > 0) {
      int toRead = min(avail, maxBytes - rawTotal);
      int got = stream->readBytes(rawBuf + rawTotal, toRead);
      if (got > 0) {
        rawTotal += got;
        lastData = millis();
      }
    } else {
      if (!http.connected())
        break;
      delay(2);
    }
  }
  http.end();
  Serial.printf("[TTS] Received %d bytes\n", rawTotal);

  // ── Locate RIFF header in raw buffer ──────────
  int riffPos = -1;
  for (int i = 0; i <= rawTotal - 4; i++) {
    if (rawBuf[i] == 'R' && rawBuf[i + 1] == 'I' && rawBuf[i + 2] == 'F' && rawBuf[i + 3] == 'F') {
      riffPos = i;
      break;
    }
  }
  if (riffPos < 0) {
    Serial.println("[TTS] ERROR: RIFF header not found");
    free(rawBuf);
    return 0;
  }
  Serial.printf("[TTS] RIFF at offset %d (skipped %d prefix bytes)\n", riffPos, riffPos);

  // ── Parse WAV sub-chunks ───────────────────────
  uint8_t *p = rawBuf + riffPos + 12; // skip RIFF + size + WAVE
  int remaining = rawTotal - riffPos - 12;

  uint32_t wavSampleRate = 0;
  uint16_t wavChannels = 0;
  uint16_t wavBits = 0;
  uint8_t *dataPtr = nullptr;
  uint32_t dataBytes = 0;

  while (remaining >= 8) {
    char id[5] = {0};
    memcpy(id, p, 4);
    uint32_t chunkSize = p[4] | (p[5] << 8) | (p[6] << 16) | (p[7] << 24);
    p += 8;
    remaining -= 8;

    Serial.printf("[TTS] Chunk: [%.4s] size: %u\n", id, chunkSize);

    if (memcmp(id, "fmt ", 4) == 0) {
      if (remaining < 16)
        break;
      wavChannels = p[2] | (p[3] << 8);
      wavSampleRate = p[4] | (p[5] << 8) | (p[6] << 16) | (p[7] << 24);
      wavBits = p[14] | (p[15] << 8);
      Serial.printf("[TTS] Format: %u Hz, %u ch, %u-bit\n", wavSampleRate, wavChannels, wavBits);
      uint32_t skip = min((uint32_t)remaining, chunkSize);
      p += skip;
      remaining -= skip;

    } else if (memcmp(id, "data", 4) == 0) {
      dataPtr = p;
      // 0xFFFFFFFF = streaming WAV — size unknown at write time
      dataBytes = (chunkSize == 0xFFFFFFFF) ? (uint32_t)remaining : chunkSize;
      dataBytes = min(dataBytes, (uint32_t)remaining);
      Serial.printf("[TTS] Audio data: %u bytes (%.1f sec)\n", dataBytes,
                    (float)(dataBytes / 2) / wavSampleRate);
      break;

    } else {
      // Skip LIST, INFO, and any other metadata chunks
      uint32_t skip = min((uint32_t)remaining, chunkSize);
      p += skip;
      remaining -= skip;
    }
  }

  if (!dataPtr || dataBytes == 0 || wavSampleRate == 0) {
    Serial.println("[TTS] ERROR: Could not find valid audio data chunk");
    free(rawBuf);
    return 0;
  }

  // ── Copy PCM into dedicated PSRAM buffer ───────
  int numSamples = dataBytes / 2;
  int16_t *pcmBuf = (int16_t *)ps_malloc(numSamples * sizeof(int16_t));
  if (!pcmBuf) {
    Serial.println("[TTS] ERROR: ps_malloc for PCM buffer failed");
    free(rawBuf);
    return 0;
  }

  memcpy(pcmBuf, dataPtr, numSamples * sizeof(int16_t));
  free(rawBuf);

  *outBuf = pcmBuf;
  *sampleRate_out = wavSampleRate;

  Serial.printf("[TTS] PCM ready: %d samples @ %u Hz\n", numSamples, wavSampleRate);
  return numSamples;
}

// ═══════════════════════════════════════════════
//  GROQ STT — Whisper transcription
//  Sends recorded WAV to Groq Whisper endpoint
//  Returns transcript as String
// ═══════════════════════════════════════════════
String transcribeAudio() {
  Serial.println("[STT] Building WAV...");

  int dataBytes = recordCount * 2;
  int wavSize = dataBytes + 44;
  uint8_t *wavBuf = (uint8_t *)ps_malloc(wavSize);
  if (!wavBuf) {
    Serial.println("ERROR: ps_malloc WAV failed");
    return "";
  }

  buildWavHeader(wavBuf, dataBytes);
  memcpy(wavBuf + 44, recordBuf, dataBytes);
  Serial.printf("[STT] Sending %.1f sec / %d bytes to Whisper...\n",
                (float)recordCount / SAMPLE_RATE, wavSize);

  String boundary = "ESP32Boundary";
  String bodyStart =
      "--" + boundary +
      "\r\n"
      "Content-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-large-v3-turbo\r\n"
      "--" +
      boundary +
      "\r\n"
      "Content-Disposition: form-data; name=\"response_format\"\r\n\r\njson\r\n"
      "--" +
      boundary +
      "\r\n"
      "Content-Disposition: form-data; name=\"language\"\r\n\r\nen\r\n"
      "--" +
      boundary +
      "\r\n"
      "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
      "Content-Type: audio/wav\r\n\r\n";
  String bodyEnd = "\r\n--" + boundary + "--\r\n";

  int totalLen = bodyStart.length() + wavSize + bodyEnd.length();
  uint8_t *fullBody = (uint8_t *)ps_malloc(totalLen);
  if (!fullBody) {
    free(wavBuf);
    Serial.println("ERROR: ps_malloc body failed");
    return "";
  }

  memcpy(fullBody, bodyStart.c_str(), bodyStart.length());
  memcpy(fullBody + bodyStart.length(), wavBuf, wavSize);
  memcpy(fullBody + bodyStart.length() + wavSize, bodyEnd.c_str(), bodyEnd.length());
  free(wavBuf);

  HTTPClient http;
  http.begin("https://api.groq.com/openai/v1/audio/transcriptions");
  http.addHeader("Authorization", String("Bearer ") + GROQ_API_KEY);
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
  http.addHeader("Content-Length", String(totalLen));
  http.setTimeout(20000);

  int httpCode = http.POST(fullBody, totalLen);
  free(fullBody);

  String transcript = "";
  if (httpCode == 200) {
    JsonDocument doc;
    deserializeJson(doc, http.getString());
    transcript = doc["text"].as<String>();
    transcript.trim();
    Serial.printf("[STT] Transcript: \"%s\"\n", transcript.c_str());
  } else {
    Serial.printf("[STT] ERROR HTTP %d: %s\n", httpCode, http.getString().c_str());
  }
  http.end();
  return transcript;
}

// ═══════════════════════════════════════════════
//  GROQ LLM — Llama 3.3 70B answer
//  Sends transcript, gets short spoken answer
// ═══════════════════════════════════════════════
String getAnswer(const String &question) {
  Serial.printf("[LLM] Question: \"%s\"\n", question.c_str());

  JsonDocument doc;
  doc["model"] = "llama-3.3-70b-versatile";
  doc["max_tokens"] = 80;
  JsonArray msgs = doc["messages"].to<JsonArray>();
  JsonObject sys = msgs.add<JsonObject>();
  sys["role"] = "system";
  sys["content"] = "You are a helpful voice assistant on an ESP32 device. "
                   "Answer in ONE short sentence under 180 characters. "
                   "No markdown, no bullet points, plain speech only.";
  JsonObject usr = msgs.add<JsonObject>();
  usr["role"] = "user";
  usr["content"] = question;

  String body;
  serializeJson(doc, body);

  HTTPClient http;
  http.begin("https://api.groq.com/openai/v1/chat/completions");
  http.addHeader("Authorization", String("Bearer ") + GROQ_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(20000);

  String answer = "Sorry, I could not get an answer.";
  int httpCode = http.POST(body);
  if (httpCode == 200) {
    JsonDocument resp;
    deserializeJson(resp, http.getString());
    answer = resp["choices"][0]["message"]["content"].as<String>();
    answer.trim();
    Serial.printf("[LLM] Answer: \"%s\"\n", answer.c_str());
  } else {
    Serial.printf("[LLM] ERROR HTTP %d\n", httpCode);
  }
  http.end();
  return answer;
}

// ═══════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n==============================");
  Serial.println("  VoiceCore ESP32");
  Serial.println("  AI Voice Assistant");
  Serial.println("  Powered by Groq");
  Serial.println("==============================\n");

  // PSRAM is required — 8MB OPI PSRAM on ESP32-S3
  if (!psramFound()) {
    Serial.println("FATAL: PSRAM not found!");
    Serial.println("  → Tools → PSRAM → OPI PSRAM");
    while (1)
      delay(1000);
  }
  Serial.printf("[OK] PSRAM: %d bytes free\n", ESP.getFreePsram());

  // SPIFFS needed for future use / config storage
  if (!SPIFFS.begin(true)) {
    Serial.println("FATAL: SPIFFS mount failed!");
    while (1)
      delay(1000);
  }
  Serial.println("[OK] SPIFFS mounted");

  // Allocate mic recording buffer in PSRAM
  recordBuf = (int16_t *)ps_malloc(MAX_SAMPLES * sizeof(int16_t));
  if (!recordBuf) {
    Serial.println("FATAL: Cannot allocate recording buffer in PSRAM!");
    while (1)
      delay(1000);
  }
  Serial.printf("[OK] Record buffer: %d samples (%d sec max)\n", MAX_SAMPLES, MAX_RECORD_MS / 1000);

  // Connect WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[..] WiFi connecting");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries++ < 40) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nFATAL: WiFi failed! Check SSID and password.");
    while (1)
      delay(1000);
  }
  Serial.printf("\n[OK] WiFi: %s\n", WiFi.localIP().toString().c_str());

  // Init microphone
  initMic();

  Serial.println("\n==============================");
  Serial.println("  READY");
  Serial.println("  Clap or speak loudly");
  Serial.println("  to start recording!");
  Serial.println("==============================\n");
}

// ═══════════════════════════════════════════════
//  LOOP — 4-state machine
//
//  IDLE      → monitor mic amplitude, wait for wake
//  RECORDING → capture audio to PSRAM buffer
//  SENDING   → STT → LLM → TTS → play
//  (no SPEAKING state — playPCM is synchronous)
// ═══════════════════════════════════════════════
void loop() {

  // Read one chunk from mic
  size_t bytes_read = 0;
  i2s_read(I2S_NUM_1, mic_buf, sizeof(mic_buf), &bytes_read, portMAX_DELAY);
  int samples = bytes_read / 4; // 4 bytes per 32-bit INMP441 frame

  // Find peak amplitude in this chunk
  int16_t peak = 0;
  for (int i = 0; i < samples; i++) {
    int16_t s = (int16_t)(mic_buf[i] >> BIT_SHIFT);
    if (abs(s) > peak)
      peak = abs(s);
  }

  // ────────────────────────────────────────────
  if (currentState == IDLE) {
    // ────────────────────────────────────────────

    // Print live level meter to serial
    static int tick = 0;
    if (++tick % 16 == 0) {
      int bar = constrain(map(peak, 0, 32767, 0, 20), 0, 20);
      Serial.print("Idle [");
      for (int b = 0; b < 20; b++)
        Serial.print(b < bar ? "-" : " ");
      Serial.printf("] %d\n", peak);
    }

    // Wake trigger
    if (peak > WAKE_THRESHOLD) {
      Serial.println("\n>>> WAKE TRIGGERED — speak your command now...\n");
      recordCount = 0;
      silenceStarted = false;
      currentState = RECORDING;
      // Capture trigger chunk so first word is not lost
      for (int i = 0; i < samples && recordCount < MAX_SAMPLES; i++)
        recordBuf[recordCount++] = (int16_t)(mic_buf[i] >> BIT_SHIFT);
    }

    // ────────────────────────────────────────────
  } else if (currentState == RECORDING) {
    // ────────────────────────────────────────────

    // Store samples in PSRAM buffer
    for (int i = 0; i < samples && recordCount < MAX_SAMPLES; i++)
      recordBuf[recordCount++] = (int16_t)(mic_buf[i] >> BIT_SHIFT);

    // Print live recording meter
    int bar = constrain(map(peak, 0, 32767, 0, 20), 0, 20);
    Serial.print("REC  [");
    for (int b = 0; b < 20; b++)
      Serial.print(b < bar ? "=" : " ");
    Serial.printf("] %d  (%d/%d)\n", peak, recordCount, MAX_SAMPLES);

    // Check for silence (end of speech)
    if (peak < SILENCE_THRESHOLD) {
      if (!silenceStarted) {
        silenceStarted = true;
        silenceStart = millis();
      } else if (millis() - silenceStart > SILENCE_MS) {
        Serial.println("\n[Recording complete — silence detected]");
        currentState = SENDING;
      }
    } else {
      silenceStarted = false; // reset timer on any sound
    }

    // Safety: stop at max length
    if (recordCount >= MAX_SAMPLES) {
      Serial.println("\n[Recording complete — max length reached]");
      currentState = SENDING;
    }

    // ────────────────────────────────────────────
  } else if (currentState == SENDING) {
    // ────────────────────────────────────────────

    stopMic(); // free I2S_NUM_1 during HTTP calls

    // Step 1: Speech → Text
    String transcript = transcribeAudio();
    if (transcript.length() == 0) {
      Serial.println("[!] Empty transcript — returning to idle");
      initMic();
      currentState = IDLE;
      return;
    }

    // Step 2: Text → Answer
    String answer = getAnswer(transcript);

    // Step 3: Answer → Speech → Play
    int16_t *pcmBuf = nullptr;
    uint32_t ttsRate = 0;
    int numSamples = fetchTTSintoPSRAM(answer, &pcmBuf, &ttsRate);

    if (numSamples > 0 && pcmBuf != nullptr) {
      playPCM(pcmBuf, numSamples, ttsRate);
      free(pcmBuf);
    } else {
      Serial.println("[!] TTS fetch failed — skipping playback");
    }

    // Return to idle
    Serial.println("\n[IDLE] Ready — clap or speak to ask another question!\n");
    currentState = IDLE;
    initMic();
  }
}
