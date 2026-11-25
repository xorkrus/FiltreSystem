#pragma once
#include <Arduino.h>
#include "config.h"

struct Filter {
  char  name[MAX_NAME_LEN];
  float resource;                // паспортный ресурс, л
  float remain;                  // текущий остаток, л
  char  replace[DATE_STR_LEN];   // дата последней замены "YYYY-MM-DD" или ""
};

struct DeviceSettings {
  char   ap_ssid[MAX_SSID_LEN];
  char   ap_pass[MAX_PASS_LEN];
  char   sta_ssid[MAX_SSID_LEN];
  char   sta_pass[MAX_PASS_LEN];
  char   ntp_server[MAX_NTP_LEN];       // пусто => использовать время клиента
  int8_t tz_offset;                      // часы, -12..+14
  float  inlet_k;                        // имп/л
  float  permeate_k;                     // имп/л
  bool   auth_enabled;                   // защита сброса фильтров
  char   auth_user[MAX_USER_LEN];
  char   auth_pass_hash[HASH_HEX_LEN];   // SHA-256 hex
};

struct Stats {
  float totalInlet;              // л, за всё время (вход)
  float totalPermeate;           // л, за всё время (пермеат)
  float todayInlet;              // л, за текущие сутки
  float todayPermeate;
  char  lastDate[DATE_STR_LEN];  // к какой дате относятся today*
  char  firstDate[DATE_STR_LEN]; // первая дата запуска
  float memInletBase;            // totalInlet на момент последней замены мембраны (#4)
  float memPermBase;             // totalPermeate на момент последней замены мембраны (#4)
};

struct DailyHistory {
  uint16_t liters_x10[30];       // литры × 10 (целые)
  uint8_t  head;                 // индекс следующей записи
  uint8_t  count;                // сколько записей заполнено (0..30)
};

extern Filter         filters[NUM_FILTERS];
extern DeviceSettings settings;
extern Stats          stats;
extern DailyHistory   history;
extern volatile bool  nvsDirty;

void storageBegin();
void storageHourlyTick();            // вызывать раз в час из loop()
void storageSaveAll();
void storageSaveFilters();
void storageSaveStats();
void storageSaveSettings();
void storageSaveHistory();
void storageMarkDirty();
void storageResetDefaults();
void storagePushDailySample(uint16_t liters_x10);
