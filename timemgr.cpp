#include "timemgr.h"
#include "config.h"
#include "storage.h"
#include <sys/time.h>

static bool     ntpActive     = false;
static uint32_t lastNtpTry    = 0;

static inline bool validTime() {
  return (uint32_t)time(nullptr) >= TIME_VALID_THRESHOLD;
}

static void startNtp() {
  if (strlen(settings.ntp_server) == 0) {
    ntpActive = false;
    Serial.println("[NTP] server empty -> client-time mode");
    return;
  }
  configTime((long)settings.tz_offset * 3600L, 0, settings.ntp_server);
  ntpActive  = true;
  lastNtpTry = millis();
  Serial.printf("[NTP] start, server=%s tz=%d\n", settings.ntp_server, settings.tz_offset);
}

void timemgrBegin() { startNtp(); }

void timemgrApplySettings() {
  // сброс системного времени невозможен, но можно перезапустить NTP
  ntpActive = false;
  startNtp();
}

void timemgrTick() {
  static uint32_t lastCheck = 0;
  uint32_t now = millis();
  if (now - lastCheck < 1000) return;
  lastCheck = now;

  // смена суток
  static char prevDate[DATE_STR_LEN] = {0};
  String today = timemgrToday();

  if (today.length() == 10) {
    if (prevDate[0] == 0) {
      // первая валидная дата после старта
      strncpy(prevDate, today.c_str(), DATE_STR_LEN - 1);
      if (stats.firstDate[0] == 0) {
        strncpy(stats.firstDate, today.c_str(), DATE_STR_LEN - 1);
        nvsDirty = true;
      }
      if (stats.lastDate[0] == 0) {
        strncpy(stats.lastDate, today.c_str(), DATE_STR_LEN - 1);
        nvsDirty = true;
      }
    } else if (strncmp(prevDate, today.c_str(), DATE_STR_LEN) != 0) {
      // наступили новые сутки: сохраняем вчерашний пермеат в кольцо
      uint16_t sample = (uint16_t)(stats.todayPermeate * 10.0f + 0.5f);
      storagePushDailySample(sample);
      stats.todayInlet    = 0.0f;
      stats.todayPermeate = 0.0f;
      strncpy(stats.lastDate, today.c_str(), DATE_STR_LEN - 1);
      strncpy(prevDate,       today.c_str(), DATE_STR_LEN - 1);
      nvsDirty = true;
      Serial.printf("[TIME] new day %s, yesterday permeate=%.1f L\n",
                    today.c_str(), sample / 10.0f);
    }
  }

  // повторные попытки NTP
  if (ntpActive && !validTime() && (now - lastNtpTry > NTP_RETRY_MS)) {
    lastNtpTry = now;
    configTime((long)settings.tz_offset * 3600L, 0, settings.ntp_server);
    Serial.println("[NTP] re-sync");
  }
}

bool timemgrHasTime() { return validTime(); }

time_t timemgrEpoch() { return time(nullptr); }

String timemgrToday() {
  if (!validTime()) return String();
  struct tm t;
  if (!getLocalTime(&t, 0)) return String();
  char buf[DATE_STR_LEN];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
  return String(buf);
}

void timemgrSetFromClient(uint32_t epoch) {
  if (validTime()) return;                 // уже есть NTP — не перезаписываем
  if (epoch < TIME_VALID_THRESHOLD) return;
  struct timeval tv = { .tv_sec = (time_t)epoch, .tv_usec = 0 };
  settimeofday(&tv, NULL);
  Serial.printf("[TIME] set from client: %u\n", (unsigned)epoch);
  nvsDirty = true;
}

String timemgrClock() {
  if (!validTime()) return String("--/--/---- | --:--:--");
  struct tm t;
  if (!getLocalTime(&t, 0)) return String("--/--/---- | --:--:--");
  char buf[32];
  snprintf(buf, sizeof(buf), "%02d/%02d/%04d | %02d:%02d:%02d",
           t.tm_mday, t.tm_mon + 1, t.tm_year + 1900,
           t.tm_hour, t.tm_min, t.tm_sec);
  return String(buf);
}