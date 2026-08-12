#include "SNMPManager.h"

#include <esp_timer.h>
#include <math.h>

#if __has_include("config.h")
#include "config.h"
#endif
#ifndef DEVICE_FIRMWARE_VERSION
#define DEVICE_FIRMWARE_VERSION "0.0.0"
#endif

// Basis-OID + Struktur laut docs/lastenheft.txt Abschnitt 7
static const char* OID_SYSTEM_NAME = ".1.3.6.1.4.1.99999.1.1.0";
static const char* OID_FIRMWARE = ".1.3.6.1.4.1.99999.1.2.0";
static const char* OID_SYSTEM_TYPE = ".1.3.6.1.4.1.99999.1.3.0";

// .2.1 (LAN-IP) bleibt bewusst frei/unbeantwortet - dieses Geraet hat kein
// LAN-Interface. WLAN-IP/RSSI liegen bewusst auf .2.2/.2.3 statt .2.1/.2.2,
// damit die Basis-OID-Struktur exakt zu Sensormeter/Sensormeter PoE passt
// (dort .2.1=LAN-IP, .2.2=WLAN-IP, .2.3=WLAN-RSSI) - ein Sensormeter
// Display kann dadurch alle drei Varianten mit identischen Offsets
// abfragen, siehe docs/entscheidungen.md.
static const char* OID_WLAN_IP = ".1.3.6.1.4.1.99999.2.2.0";
static const char* OID_WLAN_RSSI = ".1.3.6.1.4.1.99999.2.3.0";

static const char* OID_SENSOR1_NAME = ".1.3.6.1.4.1.99999.3.1.0";
static const char* OID_SENSOR1_TEMP = ".1.3.6.1.4.1.99999.3.2.0";  // Grad C x10
static const char* OID_SENSOR1_HUM = ".1.3.6.1.4.1.99999.3.3.0";   // % x10

// .4 (Sensor 2) bleibt bewusst frei/unbeantwortet, siehe lastenheft.txt.

static const char* OID_UPTIME = ".1.3.6.1.4.1.99999.5.1.0";  // TimeTicks (1/100s)
static const char* OID_HEAP = ".1.3.6.1.4.1.99999.5.2.0";    // Bytes

// "polling optimized (no continuous refresh)" (Pflichtenheft 7)
static const unsigned long REFRESH_INTERVAL_MS = 5UL * 1000UL;

SNMPManager::SNMPManager(DataManager& dataManager, ConfigManager& configManager, NetworkManager& networkManager)
    : _data(dataManager), _config(configManager), _network(networkManager) {}

void SNMPManager::refreshValues() {
  const DeviceConfig& cfg = _config.getConfig();

  strncpy(_systemName, cfg.systemName.c_str(), sizeof(_systemName) - 1);
  strncpy(_wlanIp, _network.isWlanUp() ? _network.getWlanIp().toString().c_str() : "0.0.0.0", sizeof(_wlanIp) - 1);

  _wlanRssi = _network.isWlanUp() ? _network.getWlanRssi() : 0;

  SensorReading s = _data.getSensor();
  _temperatureX10 = s.valid ? (int)round(s.temperature * 10) : 0;
  _humidityX10 = s.valid ? (int)round(s.humidity * 10) : 0;

  _uptimeTicks = (uint32_t)(esp_timer_get_time() / 10000ULL);  // Zentisekunden
  _freeHeap = ESP.getFreeHeap();
}

void SNMPManager::begin() {
  const DeviceConfig& cfg = _config.getConfig();
  _agent.setReadOnlyCommunity(cfg.snmpCommunity.c_str());
  _agent.setReadWriteCommunity(cfg.snmpCommunity.c_str());
  _agent.setUDP(&_udp);

  refreshValues();

  _agent.addReadWriteStringHandler(OID_SYSTEM_NAME, &_systemNamePtr, sizeof(_systemName), false);
  _agent.addReadOnlyStaticStringHandler(OID_FIRMWARE, std::string(DEVICE_FIRMWARE_VERSION));
  _agent.addReadOnlyStaticStringHandler(OID_SYSTEM_TYPE, std::string("Sensormeter WLAN Lite"));

  _agent.addReadWriteStringHandler(OID_WLAN_IP, &_wlanIpPtr, sizeof(_wlanIp), false);
  _agent.addIntegerHandler(OID_WLAN_RSSI, &_wlanRssi, false);

  _agent.addReadOnlyStaticStringHandler(OID_SENSOR1_NAME, std::string("DHT22"));
  _agent.addIntegerHandler(OID_SENSOR1_TEMP, &_temperatureX10, false);
  _agent.addIntegerHandler(OID_SENSOR1_HUM, &_humidityX10, false);

  _agent.addTimestampHandler(OID_UPTIME, &_uptimeTicks, false);
  _agent.addGaugeHandler(OID_HEAP, &_freeHeap);

  _agent.sortHandlers();
  _agent.begin();

  Serial.println("[SNMP] Agent gestartet (v1/v2c read-only, Port 161)");
}

void SNMPManager::loop() {
  _agent.loop();

  if (millis() - _lastRefreshMillis >= REFRESH_INTERVAL_MS) {
    _lastRefreshMillis = millis();
    refreshValues();
  }
}
