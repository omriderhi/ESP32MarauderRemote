#include "RemoteServer.h"
#include "WiFiScan.h"
#include "CommandLine.h"
#include <ArduinoJson.h>

extern WiFiScan            wifi_scan_obj;
extern CommandLine         cli_obj;
extern LinkedList<AccessPoint>*   access_points;
extern LinkedList<Station>*       stations;
extern LinkedList<ssid>*          ssids;
extern LinkedList<ProbeReqSsid>*  probe_req_ssids;

static AsyncWebServer _remote_http(REMOTE_SERVER_PORT);

// ---------------------------------------------------------------------------

void RemoteServer::RunSetup() {
  _prefs.begin("remote_ap", false);
  _ssid  = _prefs.getString("ssid",  REMOTE_DEFAULT_SSID);
  _pw    = _prefs.getString("pw",    REMOTE_DEFAULT_PW);
  _token = _prefs.getString("token", REMOTE_DEFAULT_TOKEN);
  _prefs.end();

  _server = &_remote_http;
  this->setupRoutes();
  this->startAP();
}

void RemoteServer::startAP() {
  // Full teardown → bring up as AP so the radio is in a clean state.
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(_ssid.c_str(), _pw.c_str());
  delay(150);
  _server->begin();
  Serial.println("[RemoteServer] AP: " + _ssid +
                 "  IP: " + WiFi.softAPIP().toString() +
                 "  port: " + String(REMOTE_SERVER_PORT));
}

bool RemoteServer::checkAuth(AsyncWebServerRequest* req) {
  if (!req->hasHeader("X-API-Key")) return false;
  return req->getHeader("X-API-Key")->value() == _token;
}

// ---------------------------------------------------------------------------

void RemoteServer::setupRoutes() {

  // ── GET /api/status ──────────────────────────────────────────────────────
  _server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
    StaticJsonDocument<256> doc;
    doc["mode"]       = wifi_scan_obj.currentScanMode;
    doc["scanning"]   = wifi_scan_obj.scanning();
    doc["ap_clients"] = WiFi.softAPgetStationNum();
    doc["free_heap"]  = ESP.getFreeHeap();
    doc["ap_ip"]      = WiFi.softAPIP().toString();
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // ── GET /api/aps ─────────────────────────────────────────────────────────
  _server->on("/api/aps", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
    DynamicJsonDocument doc(3072);
    JsonArray arr = doc.to<JsonArray>();
    int n = min((int)access_points->size(), 15);
    for (int i = 0; i < n; i++) {
      AccessPoint ap = access_points->get(i);
      JsonObject o = arr.createNestedObject();
      o["i"]       = i;
      o["essid"]   = ap.essid;
      o["ch"]      = ap.channel;
      o["rssi"]    = ap.rssi;
      o["bssid"]   = macToString(ap.bssid);
      o["sel"]     = ap.selected;
      o["sec"]     = ap.sec;
      o["pkts"]    = ap.packets;
    }
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // ── GET /api/stations ────────────────────────────────────────────────────
  _server->on("/api/stations", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
    DynamicJsonDocument doc(1536);
    JsonArray arr = doc.to<JsonArray>();
    int n = min((int)stations->size(), 15);
    for (int i = 0; i < n; i++) {
      Station st = stations->get(i);
      JsonObject o = arr.createNestedObject();
      o["i"]    = i;
      o["mac"]  = macToString(st.mac);
      o["ap"]   = st.ap;
      o["pkts"] = st.packets;
      o["sel"]  = st.selected;
    }
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // ── GET /api/ssids ───────────────────────────────────────────────────────
  _server->on("/api/ssids", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
    DynamicJsonDocument doc(1536);
    JsonArray arr = doc.to<JsonArray>();
    int n = min((int)ssids->size(), 20);
    for (int i = 0; i < n; i++) {
      ssid s = ssids->get(i);
      JsonObject o = arr.createNestedObject();
      o["i"]     = i;
      o["essid"] = s.essid;
      o["ch"]    = s.channel;
      o["sel"]   = s.selected;
    }
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // ── GET /api/probes ──────────────────────────────────────────────────────
  _server->on("/api/probes", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
    DynamicJsonDocument doc(1536);
    JsonArray arr = doc.to<JsonArray>();
    int n = min((int)probe_req_ssids->size(), 20);
    for (int i = 0; i < n; i++) {
      ProbeReqSsid p = probe_req_ssids->get(i);
      JsonObject o = arr.createNestedObject();
      o["i"]     = i;
      o["essid"] = p.essid;
      o["reqs"]  = p.requests;
      o["sel"]   = p.selected;
    }
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // ── POST /api/cmd ────────────────────────────────────────────────────────
  // Body: {"cmd": "<marauder cli command>"}
  _server->on("/api/cmd", HTTP_POST,
    [this](AsyncWebServerRequest* req) {
      if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
      if (!req->_tempObject) { req->send(400, "application/json", "{\"error\":\"no body\"}"); return; }
      String* body = (String*)req->_tempObject;
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, *body) || !doc.containsKey("cmd")) {
        delete body; req->_tempObject = nullptr;
        req->send(400, "application/json", "{\"error\":\"expected {\\\"cmd\\\": \\\"...\\\"}\"}" );
        return;
      }
      cli_obj.executeCommand(doc["cmd"].as<String>());
      delete body; req->_tempObject = nullptr;
      req->send(200, "application/json", "{\"ok\":true}");
    },
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) req->_tempObject = new String();
      if (req->_tempObject) ((String*)req->_tempObject)->concat((const char*)data, len);
    }
  );

  // ── POST /api/stopscan ───────────────────────────────────────────────────
  _server->on("/api/stopscan", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
    wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
    req->send(200, "application/json", "{\"ok\":true}");
  });

  // ── GET /api/apconfig ────────────────────────────────────────────────────
  _server->on("/api/apconfig", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
    StaticJsonDocument<256> doc;
    doc["ssid"] = _ssid;
    doc["pw"]   = _pw;
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // ── POST /api/apconfig ───────────────────────────────────────────────────
  // Body (all fields optional): {"ssid": "...", "pw": "...", "token": "..."}
  _server->on("/api/apconfig", HTTP_POST,
    [this](AsyncWebServerRequest* req) {
      if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
      if (!req->_tempObject) { req->send(400, "application/json", "{\"error\":\"no body\"}"); return; }
      String* body = (String*)req->_tempObject;
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, *body)) {
        delete body; req->_tempObject = nullptr;
        req->send(400, "application/json", "{\"error\":\"invalid json\"}");
        return;
      }
      bool changed = false;
      if (doc.containsKey("ssid"))  { _ssid  = doc["ssid"].as<String>();  changed = true; }
      if (doc.containsKey("pw"))    { _pw    = doc["pw"].as<String>();    changed = true; }
      if (doc.containsKey("token")) { _token = doc["token"].as<String>(); changed = true; }
      if (changed) {
        _prefs.begin("remote_ap", false);
        _prefs.putString("ssid",  _ssid);
        _prefs.putString("pw",    _pw);
        _prefs.putString("token", _token);
        _prefs.end();
        // If not mid-scan, apply immediately
        if (wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF)
          WiFi.softAP(_ssid.c_str(), _pw.c_str());
      }
      delete body; req->_tempObject = nullptr;
      req->send(200, "application/json", "{\"ok\":true}");
    },
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) req->_tempObject = new String();
      if (req->_tempObject) ((String*)req->_tempObject)->concat((const char*)data, len);
    }
  );
}

// ---------------------------------------------------------------------------

void RemoteServer::main(uint32_t currentTime) {
  static uint32_t last_restart_ms = 0;

  // Restore the AP after WiFiScan has fully released the radio.
  if (!wifi_scan_obj.wifi_initialized &&
      wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF &&
      WiFi.softAPIP() == IPAddress(0, 0, 0, 0) &&
      (currentTime - last_restart_ms > 5000)) {
    last_restart_ms = currentTime;
    this->startAP();
  }
}
