#include "webui.h"
#include "config.h"
#include "storage.h"
#include "sensors.h"
#include "timemgr.h"

#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <string.h>

#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1
#include <ElegantOTA.h>

static AsyncWebServer server(80);
static String         sessionToken = "";

// ==================== helpers ====================

String webuiSha256Hex(const String& in) {
  uint8_t hash[32];
  mbedtls_sha256((const uint8_t*)in.c_str(), in.length(), hash, 0);
  char buf[65];
  for (int i = 0; i < 32; i++) sprintf(buf + i * 2, "%02x", hash[i]);
  buf[64] = 0;
  return String(buf);
}

void webuiInvalidateSessions() {
  sessionToken = "";
}

static String genToken() {
  char buf[33];
  for (int i = 0; i < 4; i++) sprintf(buf + i * 8, "%08x", (unsigned)esp_random());
  buf[32] = 0;
  return String(buf);
}

static bool checkAuth(AsyncWebServerRequest* req) {
  if (!settings.auth_enabled) return true;
  if (sessionToken.length() == 0) return false;
  if (!req->hasHeader("Cookie")) return false;
  String cookie = req->getHeader("Cookie")->value();
  return cookie.indexOf("aqua_sid=" + sessionToken) >= 0;
}

static void sendJson(AsyncWebServerRequest* req, JsonDocument& doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  req->send(code, "application/json", out);
}

// ==================== JSON body accumulation ====================

namespace BodyAccum {
  void onData(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
    if (total > HTTP_BODY_LIMIT) return;
    if (index == 0) req->_tempObject = new String();
    String* body = (String*)req->_tempObject;
    if (body) {
      if (index == 0) body->reserve(total + 1);
      body->concat((const char*)data, len);
    }
  }
  String* take(AsyncWebServerRequest* req) {
    String* b = (String*)req->_tempObject;
    req->_tempObject = nullptr;
    return b;
  }
}

static bool parseJsonBody(AsyncWebServerRequest* req, JsonDocument& doc) {
  String* body = BodyAccum::take(req);
  if (!body) return false;
  DeserializationError err = deserializeJson(doc, *body);
  delete body;
  return !err;
}

// ==================== API: /api/status ====================

static void handleStatus(AsyncWebServerRequest* req) {
  DynamicJsonDocument doc(2304); // Увеличен размер под новые поля
  JsonArray arr = doc.createNestedArray("filters");
  for (int i = 0; i < NUM_FILTERS; i++) {
    JsonObject f = arr.createNestedObject();
    f["id"]       = i + 1;
    f["name"]     = filters[i].name;
    f["resource"] = filters[i].resource;
    f["remain"]   = filters[i].remain;
    f["percent"]  = (filters[i].resource > 0.0f)
                    ? (filters[i].remain / filters[i].resource * 100.0f) : 0.0f;
    f["replace"]  = filters[i].replace;
  }
  JsonObject s = doc.createNestedObject("stats");
  s["total"]      = stats.totalPermeate;
  s["today"]      = stats.todayPermeate;
  s["before"]     = stats.totalInlet;
  s["after"]      = stats.totalPermeate;
  // КПД мембраны — с момента последней замены (#4), а не за всё время
  float memIn  = stats.totalInlet    - stats.memInletBase;
  float memPerm = stats.totalPermeate - stats.memPermBase;
  if (memIn < 0.0f)  memIn  = 0.0f;
  if (memPerm < 0.0f) memPerm = 0.0f;
  s["efficiency"] = (memIn > 0.0f) ? (100.0f * memPerm / memIn) : 0.0f;
  
  // --- ДОБАВЛЕНО ДЛЯ UI ---
  s["mem_before"] = memIn;
  s["mem_after"]  = memPerm;
  // ------------------------

  s["since"]      = stats.firstDate;

  JsonObject d = doc.createNestedObject("device");
  d["time"] = timemgrClock();

  sendJson(req, doc);
}

// ==================== API: /api/filters ====================

static void handleFiltersGet(AsyncWebServerRequest* req) {
  DynamicJsonDocument doc(1024);
  JsonArray arr = doc.createNestedArray("filters");
  for (int i = 0; i < NUM_FILTERS; i++) {
    JsonObject f = arr.createNestedObject();
    f["id"]       = i + 1;
    f["name"]     = filters[i].name;
    f["resource"] = filters[i].resource;
    f["remain"]   = filters[i].remain;
    f["replace"]  = filters[i].replace;
  }
  sendJson(req, doc);
}

static void handleFiltersPost(AsyncWebServerRequest* req) {
  if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }

  DynamicJsonDocument doc(2048);
  if (!parseJsonBody(req, doc)) {
    req->send(400, "application/json", "{\"error\":\"invalid json\"}"); return;
  }
  JsonArray arr = doc["filters"].as<JsonArray>();
  if (arr.isNull()) {
    req->send(400, "application/json", "{\"error\":\"no filters array\"}"); return;
  }

  for (JsonVariant v : arr) {
    int id = v["id"] | 0;
    if (id < 1 || id > NUM_FILTERS) continue;
    int idx = id - 1;
    if (v.containsKey("resource")) {
      float r = v["resource"].as<float>();
      if (r >= 100.0f) filters[idx].resource = r;
    }
    if (v.containsKey("remain")) {
      float r = v["remain"].as<float>();
      if (r < 0.0f) r = 0.0f;
      if (r > filters[idx].resource) r = filters[idx].resource;
      filters[idx].remain = r;
    }
    if (v.containsKey("replace")) {
      String d = v["replace"].as<String>();
      if (d.length() <= 10) strncpy(filters[idx].replace, d.c_str(), DATE_STR_LEN - 1);
    }
    // Ручная отметка замены мембраны (#4) — тоже сбрасывает окно расчёта КПД
    if (id == MEMBRANE_FILTER_ID && v.containsKey("memKpdReset") && v["memKpdReset"].as<bool>()) {
      stats.memInletBase = stats.totalInlet;
      stats.memPermBase  = stats.totalPermeate;
    }
  }
  storageSaveFilters();
  storageSaveStats();
  nvsDirty = false;
  req->send(200, "application/json", "{\"ok\":true}");
}

// ==================== API: /api/reset ====================

static void handleReset(AsyncWebServerRequest* req) {
  if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }

  StaticJsonDocument<128> doc;
  if (!parseJsonBody(req, doc)) {
    req->send(400, "application/json", "{\"error\":\"invalid json\"}"); return;
  }
  int id = doc["id"] | 0;
  if (id < 1 || id > NUM_FILTERS) {
    req->send(400, "application/json", "{\"error\":\"invalid id\"}"); return;
  }
  int idx = id - 1;
  filters[idx].remain = filters[idx].resource;
  String today = timemgrToday();
  if (today.length() == 10) strncpy(filters[idx].replace, today.c_str(), DATE_STR_LEN - 1);

  // Замена мембраны: обнуляем базу для расчёта КПД (счётчики потребления не трогаем)
  if (id == MEMBRANE_FILTER_ID) {
    stats.memInletBase = stats.totalInlet;
    stats.memPermBase  = stats.totalPermeate;
    storageSaveStats();
  }

  // Немедленная запись (действие пользователя — критично)
  storageSaveFilters();
  nvsDirty = false;

  StaticJsonDocument<128> out;
  out["ok"]      = true;
  out["id"]      = id;
  out["replace"] = filters[idx].replace;
  sendJson(req, out);
}

// ==================== API: /api/membrane/kpd-reset ====================
// Сбрасывает только окно расчёта КПД мембраны (без сброса счётчиков и ресурса):
// база = текущие тоталы, дальше КПД считается по данным новой мембраны

static void handleMemKpdReset(AsyncWebServerRequest* req) {
  if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }

  stats.memInletBase = stats.totalInlet;
  stats.memPermBase  = stats.totalPermeate;
  storageSaveStats();
  nvsDirty = false;

  req->send(200, "application/json", "{\"ok\":true}");
}

// ==================== API: /api/settings ====================

static void handleSettingsGet(AsyncWebServerRequest* req) {
  DynamicJsonDocument doc(1024);
  JsonObject ap = doc.createNestedObject("ap");
  ap["ssid"]     = settings.ap_ssid;
  ap["pass_set"] = (strlen(settings.ap_pass) > 0);

  JsonObject sta = doc.createNestedObject("sta");
  sta["ssid"]     = settings.sta_ssid;
  sta["pass_set"] = (strlen(settings.sta_pass) > 0);

  doc["ntp_server"]   = settings.ntp_server;
  doc["tz_offset"]    = settings.tz_offset;
  doc["inlet_k"]      = settings.inlet_k;
  doc["permeate_k"]   = settings.permeate_k;
  doc["auth_enabled"] = settings.auth_enabled;
  doc["auth_user"]    = settings.auth_user;
  doc["auth_pass_set"]= (strlen(settings.auth_pass_hash) > 0);

  sendJson(req, doc);
}

static void handleSettingsAp(AsyncWebServerRequest* req) {
  if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
  StaticJsonDocument<256> doc;
  if (!parseJsonBody(req, doc)) { req->send(400, "application/json", "{\"error\":\"invalid json\"}"); return; }

  String ssid = doc["ssid"] | "";
  String pass = doc["pass"] | "";
  if (ssid.length() < 1 || ssid.length() > 32)  { req->send(400, "application/json", "{\"error\":\"ssid\"}");   return; }
  if (pass.length() < 8 || pass.length() > 63)  { req->send(400, "application/json", "{\"error\":\"pass\"}");   return; }

  strncpy(settings.ap_ssid, ssid.c_str(), MAX_SSID_LEN - 1);
  strncpy(settings.ap_pass, pass.c_str(), MAX_PASS_LEN - 1);
  storageSaveSettings();
  nvsDirty = false;

  // Перезапустить AP
  WiFi.softAPdisconnect(true);
  WiFi.softAP(settings.ap_ssid, settings.ap_pass);

  req->send(200, "application/json", "{\"ok\":true}");
}

static void handleSettingsSta(AsyncWebServerRequest* req) {
  if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
  StaticJsonDocument<256> doc;
  if (!parseJsonBody(req, doc)) { req->send(400, "application/json", "{\"error\":\"invalid json\"}"); return; }

  String ssid = doc["ssid"] | "";
  String pass = doc["pass"] | "";
  if (ssid.length() > 32) { req->send(400, "application/json", "{\"error\":\"ssid\"}"); return; }
  if (pass.length() > 63) { req->send(400, "application/json", "{\"error\":\"pass\"}"); return; }

  strncpy(settings.sta_ssid, ssid.c_str(), MAX_SSID_LEN - 1);
  if (pass.length() > 0) strncpy(settings.sta_pass, pass.c_str(), MAX_PASS_LEN - 1);
  storageSaveSettings();
  nvsDirty = false;

  WiFi.disconnect(true);
  if (ssid.length() > 0) {
    WiFi.begin(settings.sta_ssid, settings.sta_pass);
  }
  req->send(200, "application/json", "{\"ok\":true}");
}

static void handleSettingsGeneral(AsyncWebServerRequest* req) {
  if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
  DynamicJsonDocument doc(512);
  if (!parseJsonBody(req, doc)) { req->send(400, "application/json", "{\"error\":\"invalid json\"}"); return; }

  bool ntpChanged = false;

  if (doc.containsKey("ntp_server")) {
    String v = doc["ntp_server"].as<String>();
    if (v.length() <= MAX_NTP_LEN - 1) {
      if (strncmp(settings.ntp_server, v.c_str(), MAX_NTP_LEN) != 0) ntpChanged = true;
      strncpy(settings.ntp_server, v.c_str(), MAX_NTP_LEN - 1);
    }
  }
  if (doc.containsKey("tz_offset")) {
    int tz = doc["tz_offset"] | (int)settings.tz_offset;
    if (tz < -12) tz = -12;
    if (tz > 14)  tz = 14;
    if (tz != settings.tz_offset) ntpChanged = true;
    settings.tz_offset = (int8_t)tz;
  }
  if (doc.containsKey("inlet_k")) {
    float k = doc["inlet_k"].as<float>();
    if (k >= 1.0f && k <= 100000.0f) settings.inlet_k = k;
  }
  if (doc.containsKey("permeate_k")) {
    float k = doc["permeate_k"].as<float>();
    if (k >= 1.0f && k <= 100000.0f) settings.permeate_k = k;
  }
  if (doc.containsKey("auth_enabled")) {
    settings.auth_enabled = doc["auth_enabled"].as<bool>();
  }
  if (doc.containsKey("auth_user")) {
    String u = doc["auth_user"].as<String>();
    if (u.length() > 0 && u.length() < MAX_USER_LEN) {
      strncpy(settings.auth_user, u.c_str(), MAX_USER_LEN - 1);
    }
  }
  if (doc.containsKey("auth_pass")) {
    String p = doc["auth_pass"].as<String>();
    if (p.length() > 0) {
      String h = webuiSha256Hex(p);
      strncpy(settings.auth_pass_hash, h.c_str(), HASH_HEX_LEN - 1);
    }
  }

  storageSaveSettings();
  nvsDirty = false;

  if (ntpChanged) timemgrApplySettings();

  req->send(200, "application/json", "{\"ok\":true}");
}

// ==================== API: /api/login / logout ====================

static void handleLogin(AsyncWebServerRequest* req) {
  StaticJsonDocument<256> doc;
  if (!parseJsonBody(req, doc)) { req->send(400, "application/json", "{\"error\":\"invalid json\"}"); return; }

  String pin  = doc["pin"] | doc["pass"].as<String>();
  String hash = webuiSha256Hex(pin);

  bool ok = (strlen(settings.auth_pass_hash) > 0) &&
            (hash == String(settings.auth_pass_hash));

  if (!ok) { req->send(401, "application/json", "{\"error\":\"invalid pin\"}"); return; }

  sessionToken = genToken();
  AsyncWebServerResponse* res = req->beginResponse(200, "application/json", "{\"ok\":true}");
  res->addHeader("Set-Cookie", "aqua_sid=" + sessionToken + "; Path=/; HttpOnly; Max-Age=86400");
  req->send(res);
}

static void handleLogout(AsyncWebServerRequest* req) {
  sessionToken = "";
  AsyncWebServerResponse* res = req->beginResponse(200, "application/json", "{\"ok\":true}");
  res->addHeader("Set-Cookie", "aqua_sid=; Path=/; Max-Age=0");
  req->send(res);
}

// ==================== API: /api/time ====================

static void handleTimeGet(AsyncWebServerRequest* req) {
  StaticJsonDocument<160> doc;
  doc["valid"]  = timemgrHasTime();
  doc["epoch"]  = (uint32_t)timemgrEpoch();
  doc["today"]  = timemgrToday();
  doc["source"] = (strlen(settings.ntp_server) > 0) ? "ntp" : "client";
  sendJson(req, doc);
}

static void handleTimePost(AsyncWebServerRequest* req) {
  StaticJsonDocument<128> doc;
  if (!parseJsonBody(req, doc)) { req->send(400, "application/json", "{\"error\":\"invalid json\"}"); return; }
  uint32_t epoch = doc["epoch"] | 0;
  if (epoch < TIME_VALID_THRESHOLD) {
    req->send(400, "application/json", "{\"error\":\"invalid epoch\"}"); return;
  }
  timemgrSetFromClient(epoch);
  req->send(200, "application/json", "{\"ok\":true}");
}

// ==================== API: /api/device ====================

static void handleDevice(AsyncWebServerRequest* req) {
  StaticJsonDocument<256> doc;
  doc["heap_free"]  = (uint32_t)ESP.getFreeHeap();
  doc["flash_used"] = (uint32_t)(LittleFS.usedBytes() * 100UL / LittleFS.totalBytes());
  doc["uptime"]     = (uint32_t)(millis() / 1000);
  doc["mac"]        = WiFi.macAddress();
  doc["ip_ap"]      = WiFi.softAPIP().toString();
  doc["ip_sta"]     = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("");
  doc["time_valid"] = timemgrHasTime();
  doc["fw"]         = "2.5.0";
  sendJson(req, doc);
}

// ==================== API: /api/analytics ====================

static void handleAnalytics(AsyncWebServerRequest* req) {
  DynamicJsonDocument doc(1024);
  // КПД мембраны считаем с момента последней замены (#4):
  // базовые значения вычитаются из тоталов, сами счётчики не сбрасываются
  float inlet = stats.totalInlet    - stats.memInletBase;
  float perm  = stats.totalPermeate - stats.memPermBase;
  if (inlet < 0.0f) inlet = 0.0f;
  if (perm  < 0.0f) perm  = 0.0f;
  float drain = inlet - perm;
  if (drain < 0) drain = 0;

  doc["total_in"] = inlet;
  doc["drain"]    = drain;
  doc["permeate"] = perm;
  doc["efficiency"] = (inlet > 0.0f) ? (100.0f * perm / inlet) : 0.0f;

  char ratio[16];
  if (perm > 0.1f) snprintf(ratio, sizeof(ratio), "1:%.2f", drain / perm);
  else             strcpy(ratio, "—");
  doc["ratio"] = ratio;

  float sum = 0;
  for (int i = 0; i < history.count; i++) sum += history.liters_x10[i] / 10.0f;
  doc["daily_avg_30d"] = (history.count > 0) ? (sum / history.count) : 0.0f;

  JsonArray hist = doc.createNestedArray("history");
  int idx = (history.count < 30) ? 0 : history.head;
  for (int i = 0; i < history.count; i++) {
    JsonObject h = hist.createNestedObject();
    h["liters"] = history.liters_x10[idx] / 10.0f;
    idx = (idx + 1) % 30;
  }

  sendJson(req, doc);
}

// ==================== API: /api/analytics ====================

static void handleCalibGet(AsyncWebServerRequest* req) {
  StaticJsonDocument<128> doc;
  doc["running"]          = sensorsCalibRunning();
  doc["inlet_pulses"]     = sensorsCalibInletPulses();
  doc["permeate_pulses"]  = sensorsCalibPermeatePulses();
  sendJson(req, doc);
}

static void handleCalibPost(AsyncWebServerRequest* req) {
  if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }

  StaticJsonDocument<128> doc;
  if (!parseJsonBody(req, doc)) { req->send(400, "application/json", "{\"error\":\"invalid json\"}"); return; }
  String action = doc["action"] | "";

  if      (action == "start") sensorsCalibStart();
  else if (action == "stop")  sensorsCalibStop();
  else { req->send(400, "application/json", "{\"error\":\"action\"}"); return; }

  req->send(200, "application/json", "{\"ok\":true}");
}


// ==================== API: /api/reset ====================

static void handleStatsReset(AsyncWebServerRequest* req) {
  if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }

  StaticJsonDocument<128> doc;
  if (!parseJsonBody(req, doc)) { req->send(400, "application/json", "{\"error\":\"invalid json\"}"); return; }
  String what = doc["what"] | "total";

  if (what == "total") {
    stats.totalInlet    = 0;
    stats.totalPermeate = 0;
    stats.todayInlet    = 0;
    stats.todayPermeate = 0;
    stats.memInletBase  = 0;
    stats.memPermBase   = 0;
    memset(&history, 0, sizeof(history));
  } else if (what == "today") {
    stats.todayInlet    = 0;
    stats.todayPermeate = 0;
  } else {
    req->send(400, "application/json", "{\"error\":\"what\"}"); return;
  }

  storageSaveStats();
  storageSaveHistory();
  nvsDirty = false;
  req->send(200, "application/json", "{\"ok\":true}");
}


// ==================== init ====================

void webuiBegin() {
  // --- Wi-Fi: AP + STA ---
  WiFi.mode(WIFI_AP_STA);
  WiFi.setHostname("aquacontrol");
  WiFi.softAP(settings.ap_ssid, settings.ap_pass);
  Serial.printf("[WiFi] AP '%s' -> %s\n",
                settings.ap_ssid, WiFi.softAPIP().toString().c_str());
  if (strlen(settings.sta_ssid) > 0) {
    WiFi.begin(settings.sta_ssid, settings.sta_pass);
  }

  // --- CORS-заголовки по умолчанию ---
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin",  "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

  // --- статика ---
  server.serveStatic("/", LittleFS, "/")
        .setDefaultFile("index.html")
        .setCacheControl("max-age=3600");

  // --- API ---
  server.on("/api/status",           HTTP_GET,  handleStatus);
  server.on("/api/filters",          HTTP_GET,  handleFiltersGet);
  server.on("/api/filters",          HTTP_POST, handleFiltersPost,    nullptr, BodyAccum::onData);
  server.on("/api/reset",            HTTP_POST, handleReset,          nullptr, BodyAccum::onData);
  server.on("/api/membrane/kpd-reset", HTTP_POST, handleMemKpdReset);
  server.on("/api/settings",         HTTP_GET,  handleSettingsGet);
  server.on("/api/settings/ap",      HTTP_POST, handleSettingsAp,     nullptr, BodyAccum::onData);
  server.on("/api/settings/sta",     HTTP_POST, handleSettingsSta,    nullptr, BodyAccum::onData);
  server.on("/api/settings/general", HTTP_POST, handleSettingsGeneral,nullptr, BodyAccum::onData);
  server.on("/api/login",            HTTP_POST, handleLogin,          nullptr, BodyAccum::onData);
  server.on("/api/logout",           HTTP_POST, handleLogout);
  server.on("/api/time",             HTTP_GET,  handleTimeGet);
  server.on("/api/time",             HTTP_POST, handleTimePost,       nullptr, BodyAccum::onData);
  server.on("/api/device",           HTTP_GET,  handleDevice);
  server.on("/api/analytics",        HTTP_GET,  handleAnalytics);
  server.on("/api/calib", 			 HTTP_GET,  handleCalibGet);
  server.on("/api/calib", 			 HTTP_POST, handleCalibPost, nullptr, BodyAccum::onData);
  server.on("/api/stats/reset", 	 HTTP_POST, handleStatsReset, nullptr, BodyAccum::onData);

  // --- OPTIONS / 404 ---
  server.onNotFound([](AsyncWebServerRequest* req) {
    if (req->method() == HTTP_OPTIONS) { req->send(200); return; }
    req->send(404, "application/json", "{\"error\":\"not found\"}");
  });

    // --- OTA ---
  ElegantOTA.begin(&server);   // /update — веб-страница обновления
  ElegantOTA.onStart([]() { Serial.println("[OTA] start"); });
  ElegantOTA.onEnd([](bool ok) { Serial.printf("[OTA] end, ok=%d\n", ok); });

  server.begin();
  Serial.println("[HTTP] server on port 80");
}