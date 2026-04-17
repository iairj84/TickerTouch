#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "../config.h"

namespace WiFiManager {
  extern WebServer server;
  extern DNSServer dnsServer;
  extern bool      portalActive;

  void startCaptivePortal();  // blocks until credentials saved
  void connectSTA();
  void startSettingsServer(); // non-blocking web UI on port 80 while connected
  void handleSettingsServer();// call from loop or task

  bool isConnected();
  const char* getIP();
  const char* portalURL();
}
