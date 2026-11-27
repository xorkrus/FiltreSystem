#pragma once
#include <Arduino.h>

// ==================== Железо ====================
// ESP32-C3 strapping: избегаем GPIO 2, 8, 9 (strap), 18/19 (USB-JTAG), 20/21 (UART0)
#define PIN_INLET_SENSOR      3     // YF-S201 на входе (до фильтров)
#define PIN_PERMEATE_SENSOR   4     // YF-S201 после мембраны (пермеат)

// ==================== Тайминги ====================
#define SENSOR_TICK_MS        1000UL          // опрос счётчиков, 1 Гц
#define NVS_SAVE_INTERVAL_MS  (3600UL*1000UL) // запись в NVS раз в час
#define WIFI_RETRY_MS         30000UL
#define NTP_RETRY_MS          60000UL
#define NTP_INITIAL_WAIT_MS   15000UL

// ==================== Значения по умолчанию ====================
#define DEFAULT_AP_SSID       "AquaControl_AP"
#define DEFAULT_AP_PASS       "12345678"
#define DEFAULT_STA_SSID      "HOME_SSID"
#define DEFAULT_STA_PASS      "HOME_PASS"
#define DEFAULT_NTP_SERVER    "pool.ntp.org"
#define DEFAULT_TZ_OFFSET     3               // Москва
#define DEFAULT_INLET_K       450.0f          // импульсов на литр (YF-S201)
#define DEFAULT_PERMEATE_K    450.0f
#define DEFAULT_AUTH_USER     "admin"

#define NUM_FILTERS           6
#define MEMBRANE_FILTER_ID    4               // ступень с обратным осмосом (для сброса КПД мембраны)

// ==================== NVS ====================
#define NVS_NS                "aqua"
#define NVS_KEY_SETTINGS      "settings"
#define NVS_KEY_FILTERS       "filters"
#define NVS_KEY_STATS         "stats"
#define NVS_KEY_HISTORY       "history"

// ==================== Буферы ====================
#define MAX_NAME_LEN          40
#define MAX_SSID_LEN          33
#define MAX_PASS_LEN          65
#define MAX_NTP_LEN           64
#define MAX_USER_LEN          33
#define HASH_HEX_LEN          65
#define DATE_STR_LEN          11   // "YYYY-MM-DD"

// ==================== Проверка времени ====================
// 1700000000 == ноябрь 2023. Всё, что меньше — невалидное системное время.
#define TIME_VALID_THRESHOLD  1700000000UL

// ==================== Лимиты тела запроса ====================
#define HTTP_BODY_LIMIT       8192

// ==================== Кнопка сброса пина =====================
#define PIN_RESET_PIN       5
#define PIN_RESET_HOLD_MS   5000
