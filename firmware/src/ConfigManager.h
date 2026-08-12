#pragma once

#include <Arduino.h>

// Laufzeitkonfiguration gemaess docs/lastenheft.txt Abschnitt 10 (config.xml
// auf LittleFS), Persistenz per tinyxml2 (vendored, siehe lib/tinyxml2/).
//
// Anders als beim Sensormeter-Projekt: kein <lan>-Abschnitt (kein Ethernet),
// kein <sensors><sensor2>-Abschnitt (genau ein Sensor).
//
// Schema (config.xml):
//
// <config>
//   <network>
//     <wlan dhcp="true" ssid="" psk="" ip="" mask="" gateway="" dns="" pendingTest="false"/>
//   </network>
//   <system>
//     <name>Sensormeter WLAN</name>
//     <password>installer</password>
//   </system>
//   <syslog>
//     <server>0.0.0.0</server>
//   </syslog>
//   <sensor tempOffset="0.0" humOffset="0.0" calibratedTs="0"/>
//   <snmp community="public"/>
//   <mqtt enabled="false" server="" port="1883" user="" password=""/>
//   <branding vendorName=""/>
//   <reboot enabled="false" hour="3" minute="0"/>
// </config>

struct DeviceConfig {
  String systemName = "Sensormeter WLAN Lite";
  String settingsPassword = "installer";

  // Kalibrierkorrektur (fester Grad-/Prozent-Versatz, positiv oder
  // negativ) - falls der DHT22 systematisch von einem Referenzwert
  // abweicht. Wird direkt in SensorManager auf den validierten Rohmesswert
  // angewendet, damit Anzeige, SNMP UND Stundenwerte/CSV immer denselben,
  // bereits korrigierten Wert sehen (siehe docs/entscheidungen.md).
  float sensorTempOffset = 0.0f;
  float sensorHumOffset = 0.0f;
  // Wall-Clock-Zeitpunkt (time(nullptr)), zu dem die Offsets zuletzt
  // TATSAECHLICH geaendert wurden (nicht nur gespeichert - siehe
  // WebServerManager::handleApiConfigPost()). 0 = noch nie kalibriert.
  uint32_t sensorCalibratedTs = 0;

  bool wlanDhcp = true;
  String wlanIp;
  String wlanMask;
  String wlanGateway;
  String wlanDns;  // leer = Gateway als DNS verwenden (siehe NetworkManager::applyWlanConfig)
  String wlanSsid;
  String wlanPsk;
  // Einmal-Flag: nach Eingabe neuer WLAN-Zugangsdaten ueber die
  // Einstellungsseite im Fallback-Access-Point gesetzt, damit
  // NetworkManager den anschliessenden Verbindungsversuch nur kurz statt
  // 5 Minuten abwartet (schnelles Feedback), bevor er wieder auf den
  // Fallback-AP zurueckfaellt. Wird beim naechsten Boot sofort gelesen und
  // geloescht - ueberlebt also nur genau einen Neustart (siehe
  // NetworkManager::begin()).
  bool wlanPendingTest = false;

  String syslogServer = "0.0.0.0";

  String snmpCommunity = "public";

  // Home-Assistant-Anbindung ueber MQTT-Discovery (siehe
  // sensormeter-poe/repo/docs/lastenheft.txt Abschnitt 16 fuer das
  // vollstaendige Feature-Design - hier nur die Sensor-Rolle, kein
  // Relais/Aktor, da dieses Board keinen RJ45-Modularanschluss hat).
  // Topic-Praefix wird NICHT gespeichert, sondern wie der mDNS-Hostname
  // zur Laufzeit aus systemName abgeleitet (NetworkManager::sanitizeHostname).
  bool mqttEnabled = false;
  String mqttServer;
  uint16_t mqttPort = 1883;
  String mqttUser;
  String mqttPassword;

  // Anbieter-Branding (Weisslabel): frei einstellbarer Vendor-Name, erscheint
  // zusaetzlich zum (weiterhin bestehenden) frei editierbaren Systemnamen auf
  // OLED-Slide und Webseiten-Header, sobald gesetzt. Das Logo-Bild selbst
  // wird NICHT hier gespeichert (Binaerdaten gehoeren nicht in die
  // config.xml), sondern separat als Datei auf LittleFS - siehe
  // BrandingManager. Leer = Feature inaktiv (Default), kein
  // Verhaltensunterschied fuer bestehende Installationen.
  String brandingVendorName;

  // Taeglicher automatischer Neustart zu fester Uhrzeit (optional, Default
  // aus) - ueber die Einstellungsseite aktivierbar, z.B. um sich langfristig
  // ansammelnden Speicherfragmentierungs-/Verbindungsproblemen vorzubeugen.
  // Braucht eine per NTP synchronisierte Uhr (siehe RebootManager) - ohne
  // die wird nichts ausgeloest.
  bool rebootScheduleEnabled = false;
  uint8_t rebootHour = 3;    // 0-23
  uint8_t rebootMinute = 0;  // 0-59
};

class ConfigManager {
 public:
  // Laedt config.xml von LittleFS. Fehlt die Datei oder ist sie ungueltig,
  // werden Defaults verwendet und sofort als neue config.xml gespeichert.
  void begin();

  const DeviceConfig& getConfig() const { return _config; }

  // Uebernimmt eine neue Konfiguration und speichert sie sofort (fuer die
  // Einstellungsseite in P5).
  void setConfig(const DeviceConfig& config);

  // XML-Import/-Export. importXml uebernimmt nur bei erfolgreichem Parsen
  // und speichert dann.
  bool importXml(const String& xml);
  String exportXml() const;

  bool save();

 private:
  DeviceConfig _config;
  bool load();
};
