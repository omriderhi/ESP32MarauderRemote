#pragma once
#ifndef RemoteServer_h
#define RemoteServer_h

#include "configs.h"
#include "ESPAsyncWebServer.h"
#include <WiFi.h>
#include <Preferences.h>

#define REMOTE_SERVER_PORT    8080
#define REMOTE_DEFAULT_SSID   "MarauderAP"
#define REMOTE_DEFAULT_PW     "marauder1"
#define REMOTE_DEFAULT_TOKEN  "marauder"

class RemoteServer {
  private:
    AsyncWebServer* _server;
    Preferences     _prefs;
    String          _ssid;
    String          _pw;
    String          _token;

    void startAP();
    void setupRoutes();
    bool checkAuth(AsyncWebServerRequest* request);

  public:
    void RunSetup();
    void main(uint32_t currentTime);
};

#endif
