#include "storage.h"
#include <Preferences.h>
#include <string.h>

Filter         filters[NUM_FILTERS];
DeviceSettings settings;
Stats          stats;
DailyHistory   history;
volatile bool  nvsDirty = false;

static Preferences prefs;

// ---------- defaults ----------
static void setFilterDefaults() {
  static const struct { const char* name; float res; } defs[NUM_FILTERS] = {
    {"Механика 10мкм", 15000.0f},
    {"Механика 0.5мкм",  15000.0f},
    {"Карбон-блок",    8000.0f},
    {"Мембрана RO",   3500.0f},
    {"Пост-фильтр",     6000.0f},
    {"Минерализатор",  4000.0f},
  };
  for (int i = 0; i < NUM_FILTERS; i++) {
    memset(&filters[i], 0, sizeof(Filter));
    strncpy(filters[i].name, defs[i].name, MAX_NAME_LEN - 1);
    filters[i].resource = defs[i].res;
    filters[i].remain   = defs[i].res;
    filters[i].replace[0] = 0;
  }
}

static void setSettingsDefaults() {
  memset(&settings, 0, sizeof(settings));
  strncpy(settings.ap_ssid,     DEFAULT_AP_SSID,    MAX_SSID_LEN - 1);
  strncpy(settings.ap_pass,     DEFAULT_AP_PASS,    MAX_PASS_LEN - 1);
  strncpy(settings.sta_ssid, DEFAULT_STA_SSID, MAX_SSID_LEN - 1);
  strncpy(settings.sta_pass, DEFAULT_STA_PASS, MAX_PASS_LEN - 1);
  strncpy(settings.ntp_server,  DEFAULT_NTP_SERVER, MAX_NTP_LEN  - 1);
  settings.tz_offset    = DEFAULT_TZ_OFFSET;
  settings.inlet_k      = DEFAULT_INLET_K;
  settings.permeate_k   = DEFAULT_PERMEATE_K;
  settings.auth_enabled = false;
  strncpy(settings.auth_user, DEFAULT_AUTH_USER, MAX_USER_LEN - 1);
  settings.auth_pass_hash[0] = 0;
}

// ---------- load ----------
void storageBegin() {
  prefs.begin(NVS_NS, false);

  if (prefs.isKey(NVS_KEY_SETTINGS) &&
      prefs.getBytesLength(NVS_KEY_SETTINGS) == sizeof(settings)) {
    prefs.getBytes(NVS_KEY_SETTINGS, &settings, sizeof(settings));
  } else {
    setSettingsDefaults();
    prefs.putBytes(NVS_KEY_SETTINGS, &settings, sizeof(settings));
  }

  if (prefs.isKey(NVS_KEY_FILTERS) &&
      prefs.getBytesLength(NVS_KEY_FILTERS) == sizeof(filters)) {
    prefs.getBytes(NVS_KEY_FILTERS, &filters, sizeof(filters));
  } else {
    setFilterDefaults();
    prefs.putBytes(NVS_KEY_FILTERS, &filters, sizeof(filters));
  }

  memset(&stats, 0, sizeof(stats));
  if (prefs.isKey(NVS_KEY_STATS)) {
    size_t len = prefs.getBytesLength(NVS_KEY_STATS);
    if (len == sizeof(stats)) {
      prefs.getBytes(NVS_KEY_STATS, &stats, sizeof(stats));
    } else if (len > 0 && len < sizeof(stats)) {
      // Совместимость со старым форматом: дозагружаем сколько влезло,
      // новые поля останутся нулевыми (КПД мембраны начнётся с текущего момента)
      prefs.getBytes(NVS_KEY_STATS, &stats, len);
      Serial.printf("[NVS] stats: legacy blob %u -> %u bytes, mem bases reset\n",
                    (unsigned)len, (unsigned)sizeof(stats));
    }
  }

  if (prefs.isKey(NVS_KEY_HISTORY) &&
      prefs.getBytesLength(NVS_KEY_HISTORY) == sizeof(history)) {
    prefs.getBytes(NVS_KEY_HISTORY, &history, sizeof(history));
  } else {
    memset(&history, 0, sizeof(history));
  }

  Serial.printf("[NVS] settings: ap=%s sta='%s' ntp='%s' tz=%d k=%.1f/%.1f auth=%d\n",
                settings.ap_ssid, settings.sta_ssid, settings.ntp_server,
                settings.tz_offset, settings.inlet_k, settings.permeate_k,
                settings.auth_enabled ? 1 : 0);
}

// ---------- save ----------
void storageSaveSettings() { prefs.putBytes(NVS_KEY_SETTINGS, &settings, sizeof(settings)); }
void storageSaveFilters()  { prefs.putBytes(NVS_KEY_FILTERS,  &filters,  sizeof(filters));  }
void storageSaveStats()    { prefs.putBytes(NVS_KEY_STATS,    &stats,    sizeof(stats));    }
void storageSaveHistory()  { prefs.putBytes(NVS_KEY_HISTORY,  &history,  sizeof(history));  }

void storageSaveAll() {
  storageSaveSettings();
  storageSaveFilters();
  storageSaveStats();
  storageSaveHistory();
  nvsDirty = false;
  Serial.println("[NVS] saved all");
}

void storageMarkDirty() { nvsDirty = true; }

// Раз в час: пишем только если есть изменения
void storageHourlyTick() {
  if (nvsDirty) storageSaveAll();
}

void storageResetDefaults() {
  setSettingsDefaults();
  setFilterDefaults();
  memset(&stats, 0, sizeof(stats));
  memset(&history, 0, sizeof(history));
  storageSaveAll();
}

void storagePushDailySample(uint16_t liters_x10) {
  history.liters_x10[history.head] = liters_x10;
  history.head = (history.head + 1) % 30;
  if (history.count < 30) history.count++;
  nvsDirty = true;
}
