#include "config.h"
#include "storage.h"
#include "sensors.h"
#include "timemgr.h"
#include "webui.h"
#include "pinreset.h"

#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1
#include <ElegantOTA.h>

#include <LittleFS.h>
#include <WiFi.h>

static uint32_t lastSensorTick   = 0;
static uint32_t lastNvsTick      = 0;
static uint32_t lastWifiRetry    = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== AquaControl ESP32-C3 ===");
  pinresetBegin();
  // ФС для веб-файлов
  if (!LittleFS.begin(true)) {
    Serial.println("[FS] LittleFS mount FAILED — веб-UI не будет работать");
  } else {
    Serial.printf("[FS] LittleFS ok, used %u / %u bytes\n",
                  (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
  }

  storageBegin();    // NVS: настройки, фильтры, статистика
  sensorsBegin();    // прерывания на YF-S201
  timemgrBegin();    // NTP
  webuiBegin();      // Wi-Fi AP+STA, HTTP-сервер

  Serial.printf("[BOOT] free heap: %u bytes\n", (unsigned)ESP.getFreeHeap());
}

void loop() {
  uint32_t now = millis();

  // --- 1 раз в секунду: счётчики + смена суток ---
  if (now - lastSensorTick >= SENSOR_TICK_MS) {
    lastSensorTick = now;
    sensorsTick();
    timemgrTick();
  }

  // --- 1 раз в час: отложенная запись NVS ---
  if (now - lastNvsTick >= NVS_SAVE_INTERVAL_MS) {
    lastNvsTick = now;
    storageHourlyTick();
  }
  
  // --- Кнопка сброса
  pinresetTick();
  
  // --- Wi-Fi STA: переподключение ---
  if (now - lastWifiRetry >= WIFI_RETRY_MS) {
    lastWifiRetry = now;
    if (strlen(settings.sta_ssid) > 0 && WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] reconnect STA");
      WiFi.disconnect();
      WiFi.begin(settings.sta_ssid, settings.sta_pass);
    }
  }

  delay(5);
}
