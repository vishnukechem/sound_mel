extern "C" {
#include "esp32_aws_sigV4.h"
}
#include "esp_task_wdt.h"

#include <Arduino.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include "mel_spectrogram.h"
#include <driver/i2s.h>
#include <time.h>
#include "mbedtls/aes.h"

uint8_t *audioBuffer;
int32_t *rawBuffer;
// ------------------ I2S Config -------------------
#define I2S_PORT I2S_NUM_0
#define I2S_BCK_IO 26
#define I2S_WS_IO 25
#define I2S_DATA_IN_IO 22
#define SAMPLE_RATE 44100
#define SAMPLE_BUFFER_SIZE 441000  
i2s_port_t i2s_num = I2S_NUM_0;



struct UploadPayload {
  uint8_t* data;
  size_t len;
};


// ------------------ AWS Keys + Encrypted -------------------
const uint8_t aes_key[16] = { 'S', 'E', 'C', 'R', 'E', 'T', '_', 'A', 'E', 'S', '_', 'K', 'E', 'Y', '1', '6' };
const uint8_t enc_access_key[] = {
};
const uint8_t enc_secret_key[] = {

};

char access_key[33] = { 0 };
char secret_key[65] = { 0 };
char *bucket = "xxxxxxxx";
char *region = "ap-south-1";
const char* service = "s3";
const char* endpoint = "s3.ap-south-1.amazonaws.com";
char *expires = "604800";
char *security_token = "";

void uploadToS3(uint8_t* data, size_t len) ;
void setupI2S();
// ------------------ File Names -------------------
String filename = "/mel_output.bin";
String s3Path = "uploads/mel_output.bin";

// ------------------ WiFi / WebServer / Buttons -------------------
WiFiClientSecure client;
WiFiManager wm;
WebServer server(80);
#define BUTTON_PIN 4
#define HOLD_DURATION_MS 15000
#define DEBOUNCE_DELAY_MS 50
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectInterval = 10000;

// ------------------ Buffers -------------------
//float pcmBuffer[SAMPLE_BUFFER_SIZE];
//float melSpectrogram[MEL_BANDS][NUM_FRAMES];
float* pcmBuffer;
float* melSpectrogram;

// ------------------ Tasks -------------------
TaskHandle_t recordTaskHandle, uploadTaskHandle;

// ------------------ Decrypt AES -------------------
void decrypt_aes(const uint8_t *input, size_t length, const uint8_t *key, char *output) {
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, key, 128);
  uint8_t decrypted[16];
  for (size_t i = 0; i < length; i += 16) {
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, input + i, decrypted);
    memcpy(output + i, decrypted, 16);
  }
  uint8_t pad = output[length - 1];
  output[length - pad] = '\0';
  mbedtls_aes_free(&aes);
}

// ------------------ I2S Setup -------------------
void setupI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCK_IO,
    .ws_io_num = I2S_WS_IO,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_DATA_IN_IO
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_zero_dma_buffer(I2S_PORT);
}

// ------------------ Mel Record Task -------------------
void recordAudio(void* parameter) {
  while (true) {
    Serial.println("🎙️ Recording 10 sec audio...");

    size_t bytesRead = 0;
    i2s_read(i2s_num, (char*)pcmBuffer, SAMPLE_BUFFER_SIZE * sizeof(float), &bytesRead, portMAX_DELAY);

    Serial.println("✅ Audio recorded. Computing Mel...");

    unsigned long start = millis();
    computeMelSpectrogram(pcmBuffer, SAMPLE_BUFFER_SIZE, (float(*)[NUM_FRAMES])melSpectrogram);
    unsigned long duration = millis() - start;

    Serial.printf("🎛️ Mel computed in %lu ms\n", duration);

    size_t melSize = sizeof(float) * MEL_BANDS * NUM_FRAMES;
    memcpy(audioBuffer, melSpectrogram, melSize);
    Serial.printf("📦 Mel copied to PSRAM: %u bytes\n", melSize);
    Serial.printf("📈 First 5 mel values: %.3f %.3f %.3f %.3f %.3f\n", 
              melSpectrogram[0], melSpectrogram[1], melSpectrogram[2], melSpectrogram[3], melSpectrogram[4]);

    // Allocate UploadPayload
    UploadPayload* payload = (UploadPayload*)malloc(sizeof(UploadPayload));
    if (payload == nullptr) {
      Serial.println("❌ Failed to allocate upload payload");
    } else {
      payload->data = audioBuffer;
      payload->len = melSize;

     xTaskCreatePinnedToCore(uploadTask, "UploadTask", 8192, payload, 1, &uploadTaskHandle, 1);
    }

    vTaskDelay(15000 / portTICK_PERIOD_MS); // Wait 15s
  }
}




char *generatePresignedUrl() {
  time_t now = time(nullptr);
  struct tm *utc = gmtime(&now);

  char date[9];
  char timeStr[7];
  strftime(date, sizeof(date), "%Y%m%d", utc);
  strftime(timeStr, sizeof(timeStr), "%H%M%S", utc);

  char objectPath[100];
  String filename = "morn" + String(date) + "_" + String(timeStr) + ".bin";
  snprintf(objectPath, sizeof(objectPath), "/path1/%s", filename.c_str());

  return aws_sigV4_presign_url(access_key, secret_key, security_token,
                               bucket, objectPath, region,
                               date, timeStr, expires);
}
// ------------------ Upload Task -------------------
void uploadToS3(uint8_t* data, size_t len) {
  HTTPClient http;
  String url = generatePresignedUrl();

  Serial.println("📡 Uploading Mel to AWS...");
  Serial.println("🔗 URL: " + url);

  http.begin(url);
  http.addHeader("Content-Type", "application/octet-stream");

  int httpResponseCode = http.PUT(data, len);
  Serial.printf("✅ HTTP Response: %d\n", httpResponseCode);

  http.end();
}
// ------------------ WiFi Reset Logic -------------------
void handleButtonReset() {
  static unsigned long pressTime = 0;
  static bool active = false;

  if (digitalRead(BUTTON_PIN) == HIGH) {
    if (!active) {
      active = true;
      pressTime = millis();
    }
    if (millis() - pressTime > HOLD_DURATION_MS) {
      Serial.println("🔁 Resetting WiFi...");
      wm.resetSettings();
      ESP.restart();
    }
  } else {
    active = false;
  }
}
void uploadTask(void* parameter) {
  UploadPayload* payload = (UploadPayload*)parameter;
  if (payload) {
    uploadToS3(payload->data, payload->len);
    free(payload);  // ✅ cleanup
  }
  vTaskDelete(NULL);
}


// ------------------ Web Server -------------------
void startWebServer() {
  server.on("/", HTTP_GET, []() {
    String html = "<h2>Mel Recorder</h2>";
    html += "<p>Status: " + String(WiFi.isConnected() ? "Connected" : "Disconnected") + "</p>";
    html += "<p>IP: " + WiFi.localIP().toString() + "</p>";
    html += "<p>Hold button 15s to reset Wi-Fi</p>";
    server.send(200, "text/html", html);
  });
  server.begin();
}

// ------------------ NTP Sync -------------------
void syncTime() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("⏳ Syncing time...");
  time_t now = time(nullptr);
  while (now < 100000) {
    delay(500);
    now = time(nullptr);
  }
  Serial.println("🕒 Time synced.");
}

// ------------------ Setup -------------------
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      Serial.printf("WiFi disconnected, reason: %d\n", info.wifi_sta_disconnected.reason);
      WiFi.reconnect();
    }
  });

  WiFi.begin();
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n❌ Failed to connect. Portal will not open.");
  } else {
    Serial.println("\n✅ Connected to WiFi");
  }
  
  startWebServer();
  syncTime();

  decrypt_aes(enc_access_key, sizeof(enc_access_key), aes_key, access_key);
  decrypt_aes(enc_secret_key, sizeof(enc_secret_key), aes_key, secret_key);
  Serial.println("🔐 AWS keys decrypted");


  audioBuffer = (uint8_t*) heap_caps_malloc(sizeof(float) * MEL_BANDS * NUM_FRAMES, MALLOC_CAP_SPIRAM);
melSpectrogram = (float*) heap_caps_malloc(sizeof(float) * MEL_BANDS * NUM_FRAMES, MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
pcmBuffer = (float*) heap_caps_malloc(sizeof(float) * SAMPLE_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);

if (!pcmBuffer || !melSpectrogram) {
  Serial.println("❌ Failed to allocate PSRAM buffers");
  while (true) delay(1000);
}

  setupI2S();
  // Sync NTP time
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("⏳ Waiting for NTP time sync...");
  time_t now = time(nullptr);
  while (now < 100000) {
    delay(500);
    now = time(nullptr);
  }
  Serial.println("🕒 Time synchronized");

  xTaskCreatePinnedToCore(recordAudio, "RecordTask", 12288, NULL, 1, &recordTaskHandle, 0);

}

// ------------------ Loop -------------------
void loop() {
  server.handleClient();
  handleButtonReset();
   
  if (!WiFi.isConnected()) {
    if (millis() - lastReconnectAttempt > reconnectInterval) {
      Serial.println("🔄 Reconnecting to WiFi...");
      WiFi.disconnect();
      WiFi.reconnect();
      lastReconnectAttempt = millis();
    }
  }

  delay(10);
}
