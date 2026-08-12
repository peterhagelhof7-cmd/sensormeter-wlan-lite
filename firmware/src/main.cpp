// ============================================================================
// Sensormeter WLAN Lite
//
// Display-lose Variante: gleiches Board (ESP32-WROOM-32) + DHT22, aber kein
// OLED mehr - Bedienung/Anzeige ausschliesslich ueber die Weboberflaeche,
// Werksreset ueber den BOOT-Taster mit LED-Feedback (siehe ButtonManager).
//
// Verdrahtet alle Module. ConfigManager laedt/speichert config.xml auf
// LittleFS; NetworkManager bringt WLAN (DHCP/statisch, Fallback-AP) hoch und
// treibt den Boot-Zustandsautomaten aus docs/lastenheft.txt an; TimeManager
// haengt sich mit der NTP-Sync-Kette daran; SensorManager liest DHT22 im
// 60s-Takt; ButtonManager treibt den BOOT-Taster-Werksreset (LED-Feedback);
// WebServerManager stellt Hauptseite, Einstellungsseite, REST-API und
// lokalen OTA-Upload bereit (async, Port 80); SNMPManager beantwortet
// SNMP-v1/v2c-GET-Anfragen read-only (Port 161); SyslogManager sendet bei
// jedem Sensorzyklus einen Statusreport sowie Fehler-Events sofort per UDP
// (Port 514); MqttManager veroeffentlicht bei aktivierter Home-Assistant-
// Anbindung Discovery- und State-Payloads per MQTT (siehe
// sensormeter-poe/repo/docs/lastenheft.txt Abschnitt 16, docs/entscheidungen.md);
// BrandingManager haelt den optionalen Anbieter-Namen/das Logo (Weisslabel),
// das WebServerManager im Seiten-Header zeigt, sobald konfiguriert.
// ============================================================================

#include <Arduino.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>

#include "BrandingManager.h"
#include "ButtonManager.h"
#include "ConfigManager.h"
#include "DataManager.h"
#include "MqttManager.h"
#include "NetworkManager.h"
#include "OtaManager.h"
#include "RebootManager.h"
#include "SNMPManager.h"
#include "SensorManager.h"
#include "StorageManager.h"
#include "SyslogManager.h"
#include "SystemState.h"
#include "TimeManager.h"
#include "WebServerManager.h"

#if __has_include("config.h")
#include "config.h"
#else
#error "config.h fehlt! Kopiere include/config.h.example nach include/config.h."
#endif

// Eingebetteter Marker fuer die OTA-Herkunfts-/Versionspruefung (siehe
// OtaManager.h/.cpp) - wird beim Firmware-Upload auf einem Schwestergeraet
// im Byte-Stream dieser .bin gesucht, um Projekt-Identitaet und Version zu
// pruefen. Ueber den Serial.println() unten referenziert, damit der Linker
// ihn nicht wegoptimiert.
const char kFirmwareIdentityMarker[] = "SM-FW-ID:" FIRMWARE_PROJECT_ID ":" DEVICE_FIRMWARE_VERSION ":SM-FW-END";

// Arduino-ESP32-Standardstack fuer loopTask ist 8192 Byte (siehe
// framework-arduinoespressif32/cores/esp32/main.cpp). Vorsorglich auf
// 16 KB verdoppelt, identisch zu sensormeter und sensormeter-poe (beide
// real mit "Stack canary watchpoint triggered (loopTask)" abgestuerzt) -
// dieses Projekt hat zwar weniger gleichzeitig in loop() laufende
// Manager (kein RelayManager/ContactManager/SensorDetector/zweites
// Display) und ist bislang nie so abgestuerzt, aber die Absicherung
// kostet praktisch nichts und macht alle drei Familienmitglieder mit
// Task-Watchdog konsistent. Siehe docs/entscheidungen.md.
SET_LOOP_TASK_STACK_SIZE(16384);

DataManager dataManager;
ConfigManager configManager;
StorageManager storageManager;
NetworkManager networkManager(dataManager, configManager);
TimeManager timeManager(dataManager, networkManager);
SensorManager sensorManager(dataManager, timeManager, configManager);
BrandingManager brandingManager(configManager);
ButtonManager buttonManager(dataManager, configManager);
OtaManager otaManager;
WebServerManager webServerManager(dataManager, configManager, networkManager, otaManager, timeManager,
                                   brandingManager);
SNMPManager snmpManager(dataManager, configManager, networkManager);
SyslogManager syslogManager(dataManager, configManager, networkManager, timeManager);
MqttManager mqttManager(dataManager, configManager, networkManager);
RebootManager rebootManager(dataManager, configManager, timeManager);

// Serial-Kommandozeile fuer den Fall, dass das Geraet nur per USB, aber
// nicht per Netzwerk erreichbar ist (z.B. andere statische IP als das
// Bediengeraet, kein Routing dazwischen, oder falsche/unbekannte
// WLAN-Zugangsdaten). Bewusst dasselbe Vertrauensmodell wie der
// bestehende BOOT-Taster-Werksreset (physischer USB-Zugriff =
// vertrauenswuerdig, kein Web-Passwort noetig) - anders als dieser aber
// NICHT pauschal destruktiv: nur "reset"/"reset all" loeschen etwas,
// alle anderen Kommandos aendern gezielt nur die WLAN-/IP-Felder.
// Reiner Zeilen-Reader ueber Serial.available()/read() - diese Firmware
// hatte urspruenglich ueberhaupt keinen Serial-Eingabepfad.
//
// Kommandos (jeweils + Enter):
//   dhcp                          WLAN auf DHCP umstellen, statische
//                                  IP/Maske/Gateway/DNS loeschen, neu starten
//   ip <ip> <maske> <gateway> [dns]
//                                  statische IP setzen, neu starten. Anders
//                                  als die Einstellungsseite OHNE
//                                  Ping-Kollisionspruefung - bewusst einfach
//                                  gehalten, siehe docs/entscheidungen.md
//   wifi <ssid> <passwort>        neue WLAN-Zugangsdaten setzen, neu starten
//                                  (setzt wlanPendingTest, damit der erste
//                                  Verbindungsversuch nur kurz statt 5 Min.
//                                  abgewartet wird, bevor auf den
//                                  Fallback-AP zurueckgefallen wird)
//   status                        aktuellen Zustand ausgeben (WLAN, IP,
//                                  Signal, Sensor, Heap, Laufzeit) - liest
//                                  nur, aendert nichts, kein Neustart
//   dump                          aktuelle config.xml als XML ausgeben,
//                                  eingerahmt von BEGIN/END-Markern
//   upload                        wartet auf eingefuegte XML-Zeilen (z.B.
//                                  Ausgabe von "dump" zurueckgepastet),
//                                  Abschluss mit einer Zeile
//                                  "-----END CONFIG-----"; bei gueltigem
//                                  XML wird gespeichert und neu gestartet,
//                                  bei ungueltigem XML passiert nichts
//   reset                         Werksreset nur der Einstellungen, neu
//                                  starten (7-Tage-Verlauf bleibt erhalten)
//   reset all                     Werksreset der Einstellungen UND Loeschen
//                                  des 7-Tage-Verlaufs, neu starten
void handleSerialCommands() {
  static String line;
  static bool uploadMode = false;
  static String uploadBuffer;

  while (Serial.available()) {
    char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c != '\n') {
      line += c;
      continue;
    }
    line.trim();

    if (uploadMode) {
      if (line == "-----END CONFIG-----") {
        uploadMode = false;
        if (configManager.importXml(uploadBuffer)) {
          configManager.save();
          Serial.println("[SERIAL] Konfiguration importiert, starte neu...");
          delay(300);
          ESP.restart();
        } else {
          Serial.println("[SERIAL] Import fehlgeschlagen: ungueltiges XML, keine Aenderung.");
        }
        uploadBuffer = "";
      } else if (line != "-----BEGIN CONFIG-----") {
        // BEGIN-Marker wird ignoriert (nicht Teil des XML), damit sich
        // eine "dump"-Ausgabe unveraendert zurueckpasten laesst.
        uploadBuffer += line;
        uploadBuffer += "\n";
      }
      line = "";
      continue;
    }

    String cmd = line;
    String args;
    int sp = line.indexOf(' ');
    if (sp >= 0) {
      cmd = line.substring(0, sp);
      args = line.substring(sp + 1);
      args.trim();
    }

    if (cmd.equalsIgnoreCase("dhcp")) {
      DeviceConfig cfg = configManager.getConfig();
      cfg.wlanDhcp = true;
      cfg.wlanIp = "";
      cfg.wlanMask = "";
      cfg.wlanGateway = "";
      cfg.wlanDns = "";
      configManager.setConfig(cfg);
      Serial.println("[SERIAL] WLAN auf DHCP umgestellt, starte neu...");
      delay(300);
      ESP.restart();

    } else if (cmd.equalsIgnoreCase("ip")) {
      String parts[4];
      int count = 0;
      String rest = args;
      while (rest.length() > 0 && count < 4) {
        int sp2 = rest.indexOf(' ');
        if (sp2 < 0) {
          parts[count++] = rest;
          rest = "";
        } else {
          parts[count++] = rest.substring(0, sp2);
          rest = rest.substring(sp2 + 1);
          rest.trim();
        }
      }
      IPAddress probe;
      if (count < 3 || !probe.fromString(parts[0]) || !probe.fromString(parts[1]) ||
          !probe.fromString(parts[2])) {
        Serial.println("[SERIAL] Nutzung: ip <adresse> <maske> <gateway> [dns]");
      } else {
        DeviceConfig cfg = configManager.getConfig();
        cfg.wlanDhcp = false;
        cfg.wlanIp = parts[0];
        cfg.wlanMask = parts[1];
        cfg.wlanGateway = parts[2];
        cfg.wlanDns = (count >= 4) ? parts[3] : "";
        configManager.setConfig(cfg);
        Serial.println("[SERIAL] Statische IP gesetzt, starte neu...");
        delay(300);
        ESP.restart();
      }

    } else if (cmd.equalsIgnoreCase("wifi")) {
      int sp3 = args.indexOf(' ');
      if (sp3 < 0 || args.substring(0, sp3).length() == 0) {
        Serial.println("[SERIAL] Nutzung: wifi <ssid> <passwort>");
      } else {
        DeviceConfig cfg = configManager.getConfig();
        cfg.wlanSsid = args.substring(0, sp3);
        cfg.wlanPsk = args.substring(sp3 + 1);
        cfg.wlanPsk.trim();
        cfg.wlanPendingTest = true;
        configManager.setConfig(cfg);
        Serial.println("[SERIAL] WLAN-Zugangsdaten gesetzt, starte neu...");
        delay(300);
        ESP.restart();
      }

    } else if (cmd.equalsIgnoreCase("status")) {
      DeviceConfig cfg = configManager.getConfig();
      SensorReading sensor = dataManager.getSensor();
      Serial.println("[SERIAL] --- Status ---");
      Serial.print("Zustand: ");
      Serial.println(toString(dataManager.getSystemState()));
      Serial.print("WLAN: ");
      if (networkManager.isUsingFallbackWlan()) {
        Serial.println("Fallback-Access-Point \"installer\"");
      } else if (networkManager.isWlanUp()) {
        Serial.print("verbunden mit ");
        Serial.println(cfg.wlanSsid);
      } else {
        Serial.println("nicht verbunden");
      }
      Serial.print("IP: ");
      Serial.println(networkManager.getWlanIp());
      Serial.print("Modus: ");
      Serial.println(cfg.wlanDhcp ? "DHCP" : "statisch");
      Serial.print("Signal: ");
      Serial.print(networkManager.getWlanRssi());
      Serial.println(" dBm");
      Serial.print("Sensor: ");
      if (sensor.valid) {
        Serial.print(sensor.temperature, 1);
        Serial.print(" C / ");
        Serial.print(sensor.humidity, 1);
        Serial.println(" %");
      } else {
        Serial.println("kein gueltiger Messwert");
      }
      Serial.print("Freier Heap: ");
      Serial.print(ESP.getFreeHeap() / 1024);
      Serial.println(" kB");
      Serial.print("Laufzeit: ");
      Serial.print((unsigned long)(esp_timer_get_time() / 1000000ULL));
      Serial.println(" s");
      Serial.println("[SERIAL] --- Ende Status ---");

    } else if (cmd.equalsIgnoreCase("dump")) {
      Serial.println("-----BEGIN CONFIG-----");
      Serial.println(configManager.exportXml());
      Serial.println("-----END CONFIG-----");

    } else if (cmd.equalsIgnoreCase("upload")) {
      uploadMode = true;
      uploadBuffer = "";
      Serial.println(
          "[SERIAL] Warte auf XML-Zeilen, Abschluss mit einer Zeile \"-----END CONFIG-----\"");

    } else if (cmd.equalsIgnoreCase("reset")) {
      bool full = args.equalsIgnoreCase("all");
      configManager.setConfig(DeviceConfig());
      if (full) {
        LittleFS.remove("/history.csv");
        Serial.println("[SERIAL] Werksreset: Einstellungen und Verlauf geloescht, starte neu...");
      } else {
        Serial.println("[SERIAL] Werksreset: Einstellungen auf Standard zurueckgesetzt, starte neu...");
      }
      delay(300);
      ESP.restart();
    }

    line = "";
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.print("=== Sensormeter WLAN Lite ");
  Serial.print(DEVICE_FIRMWARE_VERSION);
  Serial.println(" ===");
  Serial.println(kFirmwareIdentityMarker);
  Serial.println("[SERIAL] Kommandos: dhcp, ip, wifi, status, dump, upload, reset[ all] (+ Enter)");

  dataManager.begin();
  dataManager.setSystemState(SystemState::BOOT);

  storageManager.begin();
  dataManager.loadRingbuffer();
  configManager.begin();
  timeManager.begin();
  sensorManager.begin();
  brandingManager.begin();
  buttonManager.begin();
  syslogManager.begin();
  mqttManager.begin();

  networkManager.begin();     // setzt Zustand auf INIT, dann WLAN_CHECK
  webServerManager.begin();   // async - kein eigener loop()-Aufruf noetig
  snmpManager.begin();

  // Task-Watchdog-Timer (TWDT), siehe docs/entscheidungen.md "Task-
  // Watchdog (TWDT)": ESP-IDF haelt ihn per Default am Laufen, aber ohne
  // Panic-Reaktion (nur eine Logzeile bei einem Timeout, die niemand
  // ueberwacht). esp_task_wdt_init() konfiguriert einen bereits
  // laufenden TWDT laut esp_task_wdt.h einfach um, deshalb kein
  // Sonderfall fuer "schon initialisiert" noetig (anders als auf
  // Arduino-ESP32 3.x, siehe sensormeter-poe). Bewusst erst hier am Ende
  // von setup() statt ganz am Anfang angemeldet - die vorangehenden
  // *.begin()-Aufrufe (v.a. ein etwaiger LittleFS-Erststart-Format) sind
  // einmalig und duerfen laenger dauern, ohne dass das als Hang
  // gewertet wird; ab jetzt (loop()) sind alle Zyklen kurz und
  // beschraenkt. 10s statt der ESP-BMC-Vorgabe von 5s, weil loop() hier
  // sehr viele Manager synchron durchlaeuft, u.a. einen MQTT-Reconnect-
  // Versuch mit potenziell mehrsekuendigem TCP-Connect-Timeout - 5s
  // waere ohne echten Hang zu knapp. Nur der Haupt-Loop (loopTask, hier
  // via NULL) wird angemeldet - kein anderer Task im Projekt hat einen
  // kurzen, begrenzten Zyklus.
  esp_task_wdt_init(10, true);
  esp_task_wdt_add(NULL);
  // s. OtaManager.cpp: damit ein OTA-Upload den Haupt-Loop-Task gezielt aus
  // diesem Watchdog aus-/eintragen kann, statt ihn faelschlich panic'en zu
  // lassen, obwohl nur Update.write() (im AsyncTCP-Task) blockiert.
  otaManager.setMainLoopTaskHandle(xTaskGetCurrentTaskHandle());
}

void loop() {
  handleSerialCommands();
  networkManager.loop();
  timeManager.loop();
  sensorManager.loop();
  buttonManager.loop();
  snmpManager.loop();
  syslogManager.loop();
  mqttManager.loop();
  rebootManager.loop();

  // Einmaliger mDNS-Start, sobald eine WLAN-IP vorliegt (auch im Fallback-
  // Netz "installer") - vor RUN_NORMAL ist noch keine IP vergeben.
  static bool mdnsStarted = false;
  if (!mdnsStarted && networkManager.isWlanUp()) {
    String hostname = NetworkManager::sanitizeHostname(configManager.getConfig().systemName);
    if (MDNS.begin(hostname.c_str())) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("[NET] mDNS gestartet: http://%s.local/\n", hostname.c_str());
    } else {
      Serial.println("[NET] mDNS-Start fehlgeschlagen");
    }
    mdnsStarted = true;
  }

  otaManager.checkStalled();
  esp_task_wdt_reset();
  delay(50);
}
