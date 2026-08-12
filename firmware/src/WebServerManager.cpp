#include "WebServerManager.h"

#include <ArduinoJson.h>
#include <ESP32Ping.h>
#include <LittleFS.h>
#include <Update.h>
#include <esp_timer.h>
#include <time.h>

#if __has_include("config.h")
#include "config.h"
#endif
#ifndef DEVICE_FIRMWARE_VERSION
#define DEVICE_FIRMWARE_VERSION "0.0.0"
#endif

// Nur ein statischer Link fuer den Admin-Browser, kein Geraet-seitiger
// Netzwerkzugriff - daher unproblematisch ohne HTTPS-Client (siehe
// docs/entscheidungen.md, Vorbild Sensormeter-Projekt).
#define GITHUB_REPO_SLUG "peterhagelhof7-cmd/sensormeter-wlan-lite"

namespace {
// Sicherheits-Feature: vor dem Uebernehmen einer neu gesetzten statischen
// WLAN-IP prueft dies per Ping, ob im Netz bereits ein Geraet unter dieser
// Adresse antwortet - falls ja, wird die IP-Vergabe abgelehnt statt eine
// Adresskollision zu riskieren. Ping mit count=1 und der
// Bibliotheks-Standard-Wartezeit von 1s - kurz genug, um den
// Async-Webserver-Handler nicht spuerbar zu blockieren (anders als der
// mehrsekuendige WiFi.scanNetworks()-Blockierfall, siehe
// docs/entscheidungen.md, der zum Watchdog-Reset fuehrte).
bool ipRespondsToPing(const IPAddress& ip) {
  if (ip == IPAddress(0, 0, 0, 0)) return false;
  return Ping.ping(ip, 1);
}

String formatCalibratedTs(uint32_t ts) {
  if (ts == 0) return "noch nie";
  time_t t = static_cast<time_t>(ts);
  struct tm tmv;
  localtime_r(&t, &tmv);
  char buf[20];
  snprintf(buf, sizeof(buf), "%02d.%02d.%04d %02d:%02d", tmv.tm_mday, tmv.tm_mon + 1, tmv.tm_year + 1900,
           tmv.tm_hour, tmv.tm_min);
  return String(buf);
}

// ISO 8601 (YYYY-MM-DD HH:MM:SS) fuer den CSV-Export (handleValuesCsv) -
// bewusst ein anderes Format als formatCalibratedTs() oben (das fuer die
// Web-UI gedacht ist): Tabellenkalkulationen erkennen und sortieren ISO
// 8601 zuverlaessig als Datum, ein roher Unix-Timestamp oder das deutsche
// TT.MM.JJJJ-Format dagegen nicht ohne manuelle Spaltenumwandlung.
String formatCsvTimestamp(uint32_t ts) {
  time_t t = static_cast<time_t>(ts);
  struct tm tmv;
  localtime_r(&t, &tmv);
  char buf[20];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
           tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  return String(buf);
}

// Synthetisiert einen minimalen 1-Bit-Windows-BMP (BITMAPFILEHEADER 14B +
// BITMAPINFOHEADER 40B + 2-Farben-Palette 8B + Pixeldaten) direkt in einen
// vom Aufrufer bereitgestellten Puffer, damit das intern als rohes
// 1bpp-Array gespeicherte Logo per <img> im Browser darstellbar ist, ohne
// eine PNG/JPEG-Bibliothek einzubinden (siehe BrandingManager.h). Bewusst
// ein reiner Byte-Puffer statt einer Arduino-String - eine String duerfte
// zwar embedded Nullbytes technisch vertragen (laengenbasiert, nicht
// strlen-basiert), aber die einzige String-Ueberladung von
// AsyncWebServer::beginResponse() ist als deprecated markiert, die
// Byte-Pointer+Laenge-Variante ist der empfohlene, sichere Weg. Negative
// Hoehe im Header waehlt Top-Down-Zeilenreihenfolge (von allen gaengigen
// Browsern unterstuetzt), damit die in Adafruit-GFX-Reihenfolge
// (oben->unten, MSB zuerst je Zeile) gespeicherten Bytes 1:1 uebernommen
// werden koennen - BMP verlangt sonst Bottom-Up. 128px Breite / 8 = 16 Byte
// je Zeile ist bereits ein Vielfaches von 4 (BMP-Zeilen muessen auf 4 Byte
// ausgerichtet sein), daher kein Padding noetig. Bit=1 -> Palette-Index 1
// (Weiss), Bit=0 -> Index 0 (Schwarz) - passt exakt zur
// SSD1306_WHITE-Konvention von drawBitmap().
constexpr size_t BMP_HEADER_BYTES = 14 + 40 + 8;

void buildLogoBmp(const uint8_t* xbm, size_t xbmLen, int width, int height, uint8_t* out) {
  memset(out, 0, BMP_HEADER_BYTES);

  const uint32_t pixelDataOffset = BMP_HEADER_BYTES;
  const uint32_t fileSize = pixelDataOffset + xbmLen;
  const int32_t negHeight = -height;

  out[0] = 'B';
  out[1] = 'M';
  memcpy(out + 2, &fileSize, 4);
  memcpy(out + 10, &pixelDataOffset, 4);

  const uint32_t headerSize = 40;
  const uint16_t planes = 1, bpp = 1;
  const uint32_t compression = 0, imageSize = xbmLen, ppm = 2835, colors = 2, important = 2;
  memcpy(out + 14, &headerSize, 4);
  memcpy(out + 18, &width, 4);
  memcpy(out + 22, &negHeight, 4);
  memcpy(out + 26, &planes, 2);
  memcpy(out + 28, &bpp, 2);
  memcpy(out + 30, &compression, 4);
  memcpy(out + 34, &imageSize, 4);
  memcpy(out + 38, &ppm, 4);
  memcpy(out + 42, &ppm, 4);
  memcpy(out + 46, &colors, 4);
  memcpy(out + 50, &important, 4);

  // Palette: Index 0 = Schwarz (bereits durch memset genullt), Index 1 =
  // Weiss (je 4 Byte BGRA, Alpha bleibt 0)
  out[58] = 0xFF;
  out[59] = 0xFF;
  out[60] = 0xFF;

  memcpy(out + pixelDataOffset, xbm, xbmLen);
}
}  // namespace

WebServerManager::WebServerManager(DataManager& dataManager, ConfigManager& configManager,
                                    NetworkManager& networkManager, OtaManager& otaManager,
                                    TimeManager& timeManager, BrandingManager& brandingManager)
    : _data(dataManager),
      _config(configManager),
      _network(networkManager),
      _ota(otaManager),
      _time(timeManager),
      _branding(brandingManager),
      _server(80) {}

bool WebServerManager::checkAuth(AsyncWebServerRequest* request) {
  if (!request->authenticate("admin", _config.getConfig().settingsPassword.c_str())) {
    // Fester Benutzername "admin" - Lastenheft definiert nur ein Passwort,
    // keinen Benutzernamen. Realm-Text gibt einen Hinweis im Browser-Dialog.
    request->requestAuthentication("Sensormeter WLAN Lite (Benutzername: admin)");
    return false;
  }
  return true;
}

// ----------------------------------------------------------------------------
// Seiten-Grundgeruest - Design an das Sensormeter-Display-Projekt angepasst
// (Nutzerwunsch): Navy-Banner #0f1f3d, Orange-Akzent #c8622a, warmes Creme
// #f2f0e9 fuer Tabellenkoepfe, Kartenrahmen #e4e1d8, Systemschriftart statt
// generischem sans-serif. Kein Lastenheft-Konflikt: das vorherige
// schwarz/weisse 20pt-Design war eine reine Stilentscheidung aus P5, keine
// dokumentierte Anforderung (siehe docs/entscheidungen.md, identisch zum
// Vorgehen im Sensormeter-Projekt). HTML-Struktur/Klassennamen
// (.block/.row/label/table/...) bewusst unveraendert, nur CSS ersetzt.
// ----------------------------------------------------------------------------
String WebServerManager::buildPageShell(const String& title, const String& bodyContent) const {
  String html;
  html.reserve(bodyContent.length() + 1400);
  html += "<!DOCTYPE html><html lang=\"de\"><head><meta charset=\"UTF-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<title>" + title + "</title><style>";
  html += "*{box-sizing:border-box}";
  html += "body{background:#f7f5f1;color:#1c2430;font-size:15px;text-align:center;"
          "font-family:-apple-system,'Segoe UI',Roboto,'Helvetica Neue',Arial,sans-serif;"
          "margin:0;padding:20px 14px 28px;line-height:1.5;}";
  html += "h1{font-size:22px;background:#0f1f3d;color:#fff;margin:0 auto 18px;padding:18px 20px;"
          "border-radius:6px;max-width:680px;}";
  html += ".block{background:#fff;border:1px solid #e4e1d8;border-radius:6px;padding:14px 20px;"
          "margin:16px auto;max-width:680px;}";
  html += ".block h2{font-size:14px;color:#8f4a1e;margin:0 0 10px;padding-bottom:6px;"
          "border-bottom:2px solid #c8622a;text-transform:uppercase;letter-spacing:.04em;}";
  html += ".row{display:flex;justify-content:space-between;gap:16px;margin:8px 0;text-align:left;font-size:15px;}";
  html += "p.hint{font-size:12.5px;color:#6b6559;text-align:left;margin:6px 0;}";
  html += "button,input[type=submit]{background:#c8622a;color:#fff;border:none;padding:9px 18px;"
          "font-size:14px;font-weight:600;border-radius:4px;cursor:pointer;margin:8px;}";
  html += "button:hover,input[type=submit]:hover{opacity:.9;}";
  html += "table{margin:12px auto;border-collapse:collapse;font-size:13px;}";
  html += "td,th{border:1px solid #e4e1d8;padding:6px 12px;}";
  html += "th{background:#f2f0e9;}";
  html += "input[type=text],input[type=password]{font-size:14px;padding:7px;width:80%;"
          "border:1px solid #d8d4c8;border-radius:4px;}";
  // max-width statt fester width: kurze Selects (z.B. "Automatisch schalten:
  // Ja/Nein") bleiben kompakt, nur lange Options-Texte (z.B. Werksreset-
  // Umfang) werden auf die Breite des umgebenden Labels gedeckelt statt
  // darueber hinauszulaufen - identischer Fund/Fix wie bei sensormeter,
  // siehe docs/entscheidungen.md.
  html += "select{font-size:14px;padding:7px;max-width:100%;"
          "border:1px solid #d8d4c8;border-radius:4px;}";
  html += "label{display:block;margin-top:10px;text-align:left;max-width:420px;margin-left:auto;"
          "margin-right:auto;font-size:13px;}";
  html += "a{color:#8f4a1e;text-decoration:none;}";
  html += "canvas{max-width:100%;background:#fbfaf7;border:1px solid #e4e1d8;border-radius:6px;}";
  html += "#scanResult div{cursor:pointer;padding:5px;font-size:13px;border-radius:3px;}";
  html += "#scanResult div:hover{background:#f2f0e9;}";
  html += ".brand{display:flex;align-items:center;justify-content:center;gap:10px;"
          "margin:0 auto 10px;max-width:680px;font-size:12.5px;color:#6b6559;}";
  html += ".brand img{height:28px;width:auto;}";
  html += "</style></head><body>";
  if (_branding.isActive()) {
    html += "<div class=\"brand\">";
    if (_branding.hasLogo()) {
      html += "<img src=\"/branding/logo.bmp\" alt=\"Logo\">";
    }
    if (_branding.hasVendorName()) {
      html += "<span>" + _branding.vendorName() + "</span>";
    }
    html += "</div>";
  }
  html += bodyContent;
  html += "</body></html>";
  return html;
}

String WebServerManager::buildMainPageBody() const {
  const DeviceConfig& cfg = _config.getConfig();
  SensorReading s = _data.getSensor();

  unsigned long uptimeSec = (unsigned long)(esp_timer_get_time() / 1000000ULL);
  char uptimeBuf[16];
  snprintf(uptimeBuf, sizeof(uptimeBuf), "%02lu:%02lu:%02lu", uptimeSec / 3600, (uptimeSec / 60) % 60, uptimeSec % 60);

  String timeStr = "--:--:--";
  if (_time.isSynced()) {
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    char buf[32];
    strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M:%S", &ti);
    timeStr = buf;
  }

  String html;
  html += "<h1>" + cfg.systemName + "</h1>";

  html += "<div class=\"block\"><h2>System</h2>";
  html += "<div class=\"row\"><span>Zeit</span><span>" + timeStr + "</span></div>";
  html += "<div class=\"row\"><span>Firmware</span><span>" DEVICE_FIRMWARE_VERSION "</span></div>";
  html += "<div class=\"row\"><span>Uptime</span><span>" + String(uptimeBuf) + "</span></div>";
  html += "<div class=\"row\"><span>Freier Heap</span><span>" + String(ESP.getFreeHeap() / 1024) + " kB</span></div>";
  html += "<div class=\"row\"><span>Chip-Temperatur</span><span>" + String(temperatureRead(), 1) + " C</span></div>";
  html += "</div>";

  html += "<div class=\"block\"><h2>Netzwerk</h2>";
  html += "<div class=\"row\"><span>WLAN IP</span><span>" + (_network.isWlanUp() ? _network.getWlanIp().toString() : String("-")) + "</span></div>";
  html += "<div class=\"row\"><span>WLAN SSID</span><span>" + (_network.isWlanUp() ? _network.getWlanSsid() : String("-")) + "</span></div>";
  html += "<div class=\"row\"><span>WLAN RSSI</span><span>" + (_network.isWlanUp() ? String(_network.getWlanRssi()) + " dBm" : String("-")) + "</span></div>";
  html += "<div class=\"row\"><span>Fallback-WLAN</span><span>" + String(_network.isUsingFallbackWlan() ? "aktiv" : "-") + "</span></div>";
  html += "</div>";

  html += "<div class=\"block\"><h2>Sensor</h2>";
  html += "<div class=\"row\"><span>DHT22</span><span>" +
          (s.valid ? String(s.temperature, 1) + " C / " + String(s.humidity, 0) + " %" : String("-")) +
          "</span></div>";
  html += "</div>";

  html += "<div class=\"block\"><h2>3-Tage-Verlauf</h2><canvas id=\"chart\" height=\"200\"></canvas></div>";

  html += "<div class=\"block\"><h2>Letzte Meldungen</h2><table id=\"logtable\"><tr><th>Zeit</th><th>Meldung</th></tr></table></div>";

  html += "<div class=\"block\"><a href=\"/values.csv\"><button>values.csv</button></a>";
  html += "<a href=\"/log.txt\"><button>Log</button></a>";
  if (LittleFS.exists("/log.old.txt")) {
    html += "<a href=\"/log.old.txt\"><button>Log (alt)</button></a>";
  }
  html += "<a href=\"/settings\"><button>Einstellungen</button></a></div>";

  html += "<script src=\"https://cdn.jsdelivr.net/npm/chart.js\"></script><script>";
  html += "fetch('/api/graph').then(r=>r.json()).then(d=>{";
  html += "new Chart(document.getElementById('chart'),{type:'line',data:{labels:d.labels,datasets:[";
  html += "{label:'Temperatur (C)',data:d.temperature,borderColor:'#a63d2e',yAxisID:'y'},";
  html += "{label:'Luftfeuchte (%)',data:d.humidity,borderColor:'#2a5ba0',yAxisID:'y1'}]},";
  html += "options:{scales:{y:{position:'left'},y1:{position:'right',grid:{drawOnChartArea:false}}}}});});";
  html += "fetch('/api/logs').then(r=>r.json()).then(d=>{let t=document.getElementById('logtable');";
  html += "d.entries.forEach(e=>{let r=t.insertRow();r.insertCell(0).innerText=e.time;r.insertCell(1).innerText=e.message;});});";
  html += "setInterval(()=>location.reload(),60000);";
  html += "</script>";

  return html;
}

String WebServerManager::buildSettingsPageBody() const {
  const DeviceConfig& cfg = _config.getConfig();

  String html;
  html += "<h1>Einstellungen</h1>";
  html += "<form method=\"POST\" action=\"/api/config\">";

  html += "<div class=\"block\"><h2>System</h2>";
  html += "<label>Systemname<input type=\"text\" name=\"systemName\" value=\"" + cfg.systemName + "\"></label>";
  html += "<label>Neues Passwort (leer = unveraendert)<input type=\"password\" name=\"newPassword\"></label>";
  html += "</div>";

  html += "<div class=\"block\"><h2>WLAN</h2>";
  html += "<label>SSID<input type=\"text\" name=\"wlanSsid\" id=\"wlanSsid\" value=\"" + cfg.wlanSsid + "\"></label>";
  html += "<button type=\"button\" onclick=\"scanWifi()\">SSIDs suchen (bis 20s)</button><div id=\"scanResult\"></div>";
  html += "<label>PSK<input type=\"password\" name=\"wlanPsk\" id=\"wlanPsk\" value=\"" + cfg.wlanPsk + "\"></label>";
  html += "<button type=\"button\" onclick=\"connectWifi()\">Verbinden &amp; testen (Neustart)</button> "
          "<span id=\"connectStatus\"></span>";
  html += "<p class=\"hint\">Speichert nur SSID/PSK, startet sofort neu und probiert die Verbindung fuer 30s - "
          "gelingt es nicht, faellt das Geraet automatisch zurueck auf den eigenen Access-Point \"installer\".</p>";
  html += "</div>";

  html += "<div class=\"block\"><h2>IP-Einstellungen</h2>";
  html += "<label>Modus<select name=\"wlanIpMode\" id=\"wlanIpMode\" onchange=\"toggleStaticIpFields()\">"
          "<option value=\"dhcp\"" +
          String(cfg.wlanDhcp ? " selected" : "") +
          ">Automatisch (DHCP)</option>"
          "<option value=\"static\"" +
          String(cfg.wlanDhcp ? "" : " selected") +
          ">Statisch</option>"
          "</select></label>";
  html += "<div id=\"staticIpFields\">";
  html += "<label>IP<input type=\"text\" name=\"wlanIp\" id=\"wlanIp\" value=\"" + cfg.wlanIp + "\"></label>";
  html += "<label>Netzmaske<input type=\"text\" name=\"wlanMask\" id=\"wlanMask\" value=\"" + cfg.wlanMask + "\"></label>";
  html += "<label>Gateway<input type=\"text\" name=\"wlanGateway\" id=\"wlanGateway\" value=\"" + cfg.wlanGateway + "\"></label>";
  html += "<label>DNS-Server (leer = Gateway verwenden)<input type=\"text\" name=\"wlanDns\" id=\"wlanDns\" value=\"" +
          cfg.wlanDns + "\"></label>";
  html += "</div>";
  html += "<button type=\"button\" onclick=\"applyNetwork()\">IP-Einstellungen uebernehmen &amp; neu starten</button> "
          "<span id=\"applyNetworkStatus\"></span>";
  html += "<p class=\"hint\">Prueft vor der Uebernahme, ob die Verbindung tatsaechlich moeglich ist - bei "
          "statischer IP per Ping (Adresse darf nicht bereits belegt sein), bei DHCP durch einen echten "
          "Verbindungsversuch (nur bei erhaltener Lease wird uebernommen). Erst bei Erfolg werden die "
          "Netzwerkfelder gespeichert und das Geraet neu gestartet - anders als beim allgemeinen "
          "\"Speichern (LittleFS)\"-Button unten, der ungeprueft speichert und keinen Neustart ausloest.</p>";
  html += "</div>";

  html += "<div class=\"block\"><h2>Sensor</h2>";
  html += "<label>Korrektur Temperatur (&deg;C)<input type=\"text\" name=\"sensorTempOffset\" value=\"" +
          String(cfg.sensorTempOffset, 1) + "\"></label>";
  html += "<label>Korrektur Feuchte (%)<input type=\"text\" name=\"sensorHumOffset\" value=\"" +
          String(cfg.sensorHumOffset, 1) + "\"></label>";
  html += "<div class=\"row\"><span>Zuletzt kalibriert</span><span>" +
          formatCalibratedTs(cfg.sensorCalibratedTs) + "</span></div>";
  html += "</div>";

  html += "<div class=\"block\"><h2>Syslog</h2>";
  html += "<label>Syslog-Server-IP<input type=\"text\" name=\"syslogServer\" value=\"" + cfg.syslogServer + "\"></label>";
  html += "</div>";

  html += "<div class=\"block\"><h2>SNMP</h2>";
  html += "<label>Community<input type=\"text\" name=\"snmpCommunity\" value=\"" + cfg.snmpCommunity + "\"></label>";
  html += "</div>";

  html += "<div class=\"block\"><h2>MQTT (Home Assistant)</h2>";
  html += "<label><input type=\"checkbox\" name=\"mqttEnabled\" " +
          String(cfg.mqttEnabled ? "checked" : "") + "> Aktiv</label>";
  html += "<label>Broker-Adresse<input type=\"text\" name=\"mqttServer\" value=\"" + cfg.mqttServer + "\"></label>";
  html += "<label>Port<input type=\"text\" name=\"mqttPort\" value=\"" + String(cfg.mqttPort) + "\"></label>";
  html += "<label>Benutzername<input type=\"text\" name=\"mqttUser\" value=\"" + cfg.mqttUser + "\"></label>";
  html += "<label>Passwort<input type=\"password\" name=\"mqttPassword\" value=\"" + cfg.mqttPassword + "\"></label>";
  html += "<p class=\"hint\">Meldet das Geraet per MQTT-Discovery bei Home Assistant an (Sensoren Temperatur/"
          "Luftfeuchte). Bleibt inaktiv, solange keine Broker-Adresse eingetragen ist.</p>";
  html += "</div>";

  html += "<div class=\"block\"><h2>Anbieter-Branding</h2>";
  html += "<label>Anbietername<input type=\"text\" name=\"brandingVendorName\" value=\"" +
          cfg.brandingVendorName + "\"></label>";
  html += "<p class=\"hint\">Erscheint zusaetzlich zum Systemnamen auf einer eigenen OLED-Seite und im "
          "Kopfbereich der Weboberfläche, sobald Name und/oder Logo gesetzt sind.</p>";
  html += "</div>";

  html += "<div class=\"block\"><h2>Automatischer Neustart</h2>";
  html += "<label><input type=\"checkbox\" name=\"rebootScheduleEnabled\" " +
          String(cfg.rebootScheduleEnabled ? "checked" : "") + "> Taeglichen automatischen Neustart aktivieren</label>";
  char rebootTimeValue[6];
  snprintf(rebootTimeValue, sizeof(rebootTimeValue), "%02u:%02u", cfg.rebootHour, cfg.rebootMinute);
  html += "<label>Uhrzeit<input type=\"time\" name=\"rebootTime\" value=\"" + String(rebootTimeValue) + "\"></label>";
  html += "<p class=\"hint\">Startet das Geraet einmal taeglich zur gewaehlten Uhrzeit automatisch neu - "
          "braucht eine per NTP synchronisierte Uhr, ohne die wird nichts ausgeloest.</p>";
  html += "</div>";

  html += "<div class=\"block\"><input type=\"submit\" value=\"Speichern (LittleFS)\"></div>";
  html += "</form>";

  html += "<div class=\"block\"><h2>Logo</h2>";
  if (_branding.hasLogo()) {
    html += "<p class=\"row\"><img src=\"/branding/logo.bmp\" alt=\"Logo\" style=\"height:48px;\"></p>";
    html += "<form method=\"POST\" action=\"/api/branding/logo/delete\" "
            "onsubmit=\"return confirm('Logo wirklich entfernen?')\">"
            "<input type=\"submit\" value=\"Logo entfernen\"></form>";
  }
  html += "<form method=\"POST\" action=\"/api/branding/logo\" enctype=\"multipart/form-data\">";
  html += "<input type=\"file\" name=\"file\" accept=\".bin\"><input type=\"submit\" value=\"Logo hochladen\">";
  html += "</form>";
  html += "<p class=\"hint\">Erwartet eine vorkonvertierte Rohdatei: 128x64 Pixel, 1 Bit pro Pixel, "
          "MSB-zuerst je Zeile, genau 1024 Byte (kein PNG/JPEG) - jede andere Groesse wird abgelehnt.</p>";
  html += "</div>";

  html += "<div class=\"block\"><h2>Konfiguration</h2>";
  html += "<a href=\"/api/config/export\"><button type=\"button\">XML Export</button></a>";
  html += "<form method=\"POST\" action=\"/api/config/import\" enctype=\"multipart/form-data\">";
  html += "<input type=\"file\" name=\"file\" accept=\".xml\"><input type=\"submit\" value=\"XML Import\">";
  html += "</form>";
  html += "<form method=\"POST\" action=\"/api/factory-reset\" id=\"resetForm\" onsubmit=\"return confirmReset()\">";
  html += "<label>Werksreset - was zuruecksetzen?"
          "<select name=\"scope\" id=\"resetScope\">"
          "<option value=\"all\">Alles (Einstellungen + Messwerte + Branding)</option>"
          "<option value=\"config\">Nur Konfiguration (WLAN, Passwort, Kalibrierung, Syslog, SNMP, MQTT - Branding bleibt erhalten)</option>"
          "<option value=\"values\">Nur Messwerte (7-Tage-Verlauf)</option>"
          "<option value=\"branding\">Nur Anbieter-Branding (Name + Logo)</option>"
          "</select></label>";
  html += "<input type=\"submit\" value=\"Werksreset durchfuehren\"></form>";
  html += "</div>";

  html += "<div class=\"block\"><h2>Firmware</h2>";
  html += "<form method=\"POST\" action=\"/api/ota/upload\" enctype=\"multipart/form-data\">";
  html += "<label><input type=\"checkbox\" name=\"otaForceDowngrade\"> Downgrade erzwingen (aeltere Version zulassen)</label>";
  html += "<input type=\"file\" name=\"file\" accept=\".bin\"><input type=\"submit\" value=\".bin hochladen\">";
  html += "</form>";
  html += "<p class=\"hint\">Die .bin muss zu diesem Projekt gehoeren (Herkunfts-/Versionspruefung, siehe "
          "docs/entscheidungen.md) - falsche oder aeltere Firmware wird sonst abgelehnt.</p>";
  html += "<a href=\"https://github.com/" GITHUB_REPO_SLUG "/releases\" target=\"_blank\"><button type=\"button\">Releases auf GitHub</button></a>";
  html += "</div>";

  html += "<div class=\"block\"><form method=\"POST\" action=\"/api/reboot\" onsubmit=\"return confirm('Wirklich neu starten?')\">";
  html += "<input type=\"submit\" value=\"Reboot\"></form></div>";

  html += "<script>";
  // Nicht-blockierender Scan: pollt /api/wifi/scan alle 1,5s (bis zu ~20s),
  // bis der Server "done" meldet - siehe handleApiWifiScan()/Klassenkommentar.
  html += "function scanWifi(){"
          "document.getElementById('scanResult').innerText='Suche laeuft...';"
          "let tries=0;"
          "const poll=()=>{fetch('/api/wifi/scan').then(r=>r.json()).then(d=>{"
          "if(d.status==='done'){"
          "document.getElementById('scanResult').innerHTML=d.networks.length?d.networks.map(n=>"
          "`<div onclick=\"document.getElementById('wlanSsid').value='${n.ssid}'\">${n.ssid} (${n.rssi} dBm)</div>`).join(''):"
          "'Keine Netzwerke gefunden.';"
          "}else if(tries++<13){setTimeout(poll,1500);}"
          "else{document.getElementById('scanResult').innerText='Timeout bei der Suche.';}"
          "});};"
          "poll();}";
  html += "function connectWifi(){"
          "const body=new URLSearchParams({wlanSsid:document.getElementById('wlanSsid').value,"
          "wlanPsk:document.getElementById('wlanPsk').value});"
          "document.getElementById('connectStatus').innerText='Verbinde, Geraet startet neu...';"
          "fetch('/api/wifi/connect',{method:'POST',body});}";
  html += "function applyNetwork(){"
          "const body=new URLSearchParams({dhcp:document.getElementById('wlanIpMode').value==='dhcp'?'1':'0',"
          "ip:document.getElementById('wlanIp').value,mask:document.getElementById('wlanMask').value,"
          "gateway:document.getElementById('wlanGateway').value,dns:document.getElementById('wlanDns').value});"
          "document.getElementById('applyNetworkStatus').innerText='Pruefe Erreichbarkeit (bis zu 8s)...';"
          "fetch('/api/network/apply',{method:'POST',body}).then(r=>r.text()).then(t=>{"
          "document.getElementById('applyNetworkStatus').innerText=t;"
          "}).catch(()=>{document.getElementById('applyNetworkStatus').innerText='Fehler bei der Anfrage.';});}";
  // Blendet die statischen IP-Felder nur bei Modus "Statisch" ein - Aufruf
  // sowohl bei Auswahlwechsel als auch einmal beim Laden der Seite, damit der
  // Anfangszustand zum gespeicherten wlanDhcp-Wert passt.
  html += "function toggleStaticIpFields(){"
          "document.getElementById('staticIpFields').style.display="
          "document.getElementById('wlanIpMode').value==='static'?'':'none';}"
          "toggleStaticIpFields();";
  // Bestaetigungstext richtet sich nach dem gewaehlten Reset-Umfang, damit
  // klar ist, was konkret verloren geht, bevor der POST abgeschickt wird.
  html += "function confirmReset(){"
          "var s=document.getElementById('resetScope').value;"
          "var m={"
          "all:'Wirklich ALLES zuruecksetzen (Einstellungen, Messwerte UND Branding)? Das laesst sich nicht rueckgaengig machen.',"
          "config:'Wirklich die Konfiguration zuruecksetzen (WLAN-Zugangsdaten, Passwort, Kalibrierung etc. gehen verloren, Branding bleibt erhalten)?',"
          "values:'Wirklich den gespeicherten 7-Tage-Verlauf loeschen? Das laesst sich nicht rueckgaengig machen.',"
          "branding:'Wirklich das Anbieter-Branding (Name + Logo) entfernen?'"
          "};"
          "return confirm(m[s]||'Wirklich zuruecksetzen?');}";
  html += "</script>";

  return html;
}

// ----------------------------------------------------------------------------
// Seiten
// ----------------------------------------------------------------------------
void WebServerManager::handleRoot(AsyncWebServerRequest* request) {
  request->send(200, "text/html", buildPageShell(_config.getConfig().systemName, buildMainPageBody()));
}

void WebServerManager::handleSettingsPage(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;
  request->send(200, "text/html", buildPageShell("Einstellungen", buildSettingsPageBody()));
}

void WebServerManager::handleValuesCsv(AsyncWebServerRequest* request) {
  HourValue buffer[DataManager::RINGBUFFER_SIZE];
  size_t count = _data.getRingbuffer(buffer, DataManager::RINGBUFFER_SIZE);

  String csv = "timestamp,temperature,humidity\n";
  for (size_t i = 0; i < count; i++) {
    csv += formatCsvTimestamp(buffer[i].timestamp) + "," + String(buffer[i].temperature, 1) + "," +
           String(buffer[i].humidity, 1) + "\n";
  }

  AsyncWebServerResponse* response = request->beginResponse(200, "text/csv", csv);
  response->addHeader("Content-Disposition", "attachment; filename=values.csv");
  request->send(response);
}

void WebServerManager::handleLogFile(AsyncWebServerRequest* request, const char* path) {
  if (!LittleFS.exists(path)) {
    request->send(404, "text/plain", "Keine Logdatei vorhanden.");
    return;
  }
  // text/plain statt "attachment"-Header, bewusst wie /values.csv: im
  // Browser direkt anzeigbar UND per Strg+S/Rechtsklick herunterladbar -
  // kein separater "View"- und "Download"-Weg noetig.
  request->send(LittleFS, path, "text/plain");
}

// ----------------------------------------------------------------------------
// REST-API (Pflichtenheft: /api/status, /api/sensors, /api/network,
// /api/logs, /api/config)
// ----------------------------------------------------------------------------
void WebServerManager::handleApiStatus(AsyncWebServerRequest* request) {
  const DeviceConfig& cfg = _config.getConfig();

  JsonDocument doc;
  doc["systemName"] = cfg.systemName;
  doc["firmwareVersion"] = DEVICE_FIRMWARE_VERSION;
  doc["uptimeSeconds"] = (unsigned long)(esp_timer_get_time() / 1000000ULL);
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["chipTemperatureC"] = temperatureRead();
  doc["timeSynced"] = _time.isSynced();
  if (_time.isSynced()) doc["time"] = (unsigned long)time(nullptr);

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

void WebServerManager::handleApiSensors(AsyncWebServerRequest* request) {
  SensorReading s = _data.getSensor();

  JsonDocument doc;
  JsonObject sensor = doc["sensor"].to<JsonObject>();
  sensor["name"] = "DHT22";
  sensor["valid"] = s.valid;
  sensor["temperature"] = s.temperature;
  sensor["humidity"] = s.humidity;

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

void WebServerManager::handleApiNetwork(AsyncWebServerRequest* request) {
  JsonDocument doc;
  doc["wlanUp"] = _network.isWlanUp();
  doc["wlanIp"] = _network.getWlanIp().toString();
  doc["wlanGateway"] = _network.getWlanGateway().toString();
  doc["wlanDns"] = _network.getWlanDns().toString();
  doc["wlanMac"] = _network.getWlanMac();
  doc["wlanSsid"] = _network.getWlanSsid();
  doc["wlanRssi"] = _network.getWlanRssi();
  doc["usingFallbackWlan"] = _network.isUsingFallbackWlan();

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

void WebServerManager::handleApiLogs(AsyncWebServerRequest* request) {
  LogEntry entries[DataManager::LOG_CAPACITY];
  size_t count = _data.getLogEntries(entries, DataManager::LOG_CAPACITY);

  JsonDocument doc;
  JsonArray arr = doc["entries"].to<JsonArray>();
  for (size_t i = 0; i < count; i++) {
    JsonObject o = arr.add<JsonObject>();
    char buf[24];
    struct tm ti;
    localtime_r(&entries[i].timestamp, &ti);
    strftime(buf, sizeof(buf), "%d.%m. %H:%M:%S", &ti);
    o["time"] = buf;
    o["message"] = entries[i].message;
  }

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

void WebServerManager::handleApiGraph(AsyncWebServerRequest* request) {
  // Startseiten-Graph zeigt bewusst nur die letzten 3 Tage (nicht den vollen
  // 7-Tage-Ringpuffer) und darin nur die Messpunkte 00/06/12/18 Uhr - sonst
  // sind bei stuendlicher Aufloesung ueber mehrere Tage weder Achse noch
  // Legende lesbar. tm_wday folgt der C-Konvention (0=Sonntag).
  static const char* const kWochentagDE[7] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
  static const size_t kTageVergangenheit = 3;
  static const size_t kMaxEintraege = kTageVergangenheit * 24;

  HourValue buffer[DataManager::RINGBUFFER_SIZE];
  size_t total = _data.getRingbuffer(buffer, DataManager::RINGBUFFER_SIZE);
  // getRingbuffer() liefert immer aeltestes-zuerst ab Puffer-Anfang - bei
  // < kMaxEintraege angeforderten Eintraegen wuerden das die AELTESTEN statt
  // der neuesten 3 Tage sein. Deshalb hier alles holen und selbst den
  // hinteren (juengsten) Ausschnitt nehmen.
  size_t start = (total > kMaxEintraege) ? (total - kMaxEintraege) : 0;

  JsonDocument doc;
  JsonArray labels = doc["labels"].to<JsonArray>();
  JsonArray temps = doc["temperature"].to<JsonArray>();
  JsonArray hums = doc["humidity"].to<JsonArray>();

  for (size_t i = start; i < total; i++) {
    struct tm ti;
    localtime_r(&buffer[i].timestamp, &ti);
    if (ti.tm_hour != 0 && ti.tm_hour != 6 && ti.tm_hour != 12 && ti.tm_hour != 18) continue;
    char buf[12];
    snprintf(buf, sizeof(buf), "%s %02d:00", kWochentagDE[ti.tm_wday], ti.tm_hour);
    labels.add(String(buf));
    temps.add(buffer[i].temperature);
    hums.add(buffer[i].humidity);
  }

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

void WebServerManager::handleApiConfigGet(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;
  const DeviceConfig& cfg = _config.getConfig();

  JsonDocument doc;
  doc["systemName"] = cfg.systemName;
  doc["wlanDhcp"] = cfg.wlanDhcp;
  doc["wlanSsid"] = cfg.wlanSsid;
  doc["wlanIp"] = cfg.wlanIp;
  doc["wlanMask"] = cfg.wlanMask;
  doc["wlanGateway"] = cfg.wlanGateway;
  doc["wlanDns"] = cfg.wlanDns;
  doc["sensorTempOffset"] = cfg.sensorTempOffset;
  doc["sensorHumOffset"] = cfg.sensorHumOffset;
  doc["sensorCalibratedTs"] = cfg.sensorCalibratedTs;
  doc["syslogServer"] = cfg.syslogServer;
  doc["snmpCommunity"] = cfg.snmpCommunity;
  doc["mqttEnabled"] = cfg.mqttEnabled;
  doc["mqttServer"] = cfg.mqttServer;
  doc["mqttPort"] = cfg.mqttPort;
  doc["mqttUser"] = cfg.mqttUser;
  doc["mqttPassword"] = cfg.mqttPassword;
  doc["brandingVendorName"] = cfg.brandingVendorName;
  doc["brandingHasLogo"] = _branding.hasLogo();
  doc["rebootScheduleEnabled"] = cfg.rebootScheduleEnabled;
  doc["rebootHour"] = cfg.rebootHour;
  doc["rebootMinute"] = cfg.rebootMinute;

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

void WebServerManager::handleApiConfigPost(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;

  DeviceConfig cfg = _config.getConfig();

  if (request->hasParam("systemName", true)) cfg.systemName = request->getParam("systemName", true)->value();
  if (request->hasParam("newPassword", true)) {
    String pw = request->getParam("newPassword", true)->value();
    if (pw.length() > 0) cfg.settingsPassword = pw;
  }

  if (request->hasParam("wlanIpMode", true)) cfg.wlanDhcp = request->getParam("wlanIpMode", true)->value() == "dhcp";
  if (request->hasParam("wlanSsid", true)) cfg.wlanSsid = request->getParam("wlanSsid", true)->value();
  if (request->hasParam("wlanPsk", true)) cfg.wlanPsk = request->getParam("wlanPsk", true)->value();
  if (request->hasParam("wlanIp", true)) cfg.wlanIp = request->getParam("wlanIp", true)->value();
  if (request->hasParam("wlanMask", true)) cfg.wlanMask = request->getParam("wlanMask", true)->value();
  if (request->hasParam("wlanGateway", true)) cfg.wlanGateway = request->getParam("wlanGateway", true)->value();
  if (request->hasParam("wlanDns", true)) cfg.wlanDns = request->getParam("wlanDns", true)->value();

  // Alte Offsets merken, um "zuletzt kalibriert" NUR bei einer tatsaechlichen
  // Aenderung zu aktualisieren - dieses Formular wird bei jedem Speichern der
  // Einstellungsseite abgeschickt, nicht nur bei einer Kalibrierung.
  float oldSensorTempOffset = cfg.sensorTempOffset;
  float oldSensorHumOffset = cfg.sensorHumOffset;

  if (request->hasParam("sensorTempOffset", true)) {
    cfg.sensorTempOffset = request->getParam("sensorTempOffset", true)->value().toFloat();
  }
  if (request->hasParam("sensorHumOffset", true)) {
    cfg.sensorHumOffset = request->getParam("sensorHumOffset", true)->value().toFloat();
  }

  if (cfg.sensorTempOffset != oldSensorTempOffset || cfg.sensorHumOffset != oldSensorHumOffset) {
    cfg.sensorCalibratedTs = static_cast<uint32_t>(time(nullptr));
  }

  if (request->hasParam("syslogServer", true)) cfg.syslogServer = request->getParam("syslogServer", true)->value();

  if (request->hasParam("snmpCommunity", true)) {
    String community = request->getParam("snmpCommunity", true)->value();
    if (community.length() > 0) cfg.snmpCommunity = community;
  }

  cfg.mqttEnabled = request->hasParam("mqttEnabled", true);
  if (request->hasParam("mqttServer", true)) cfg.mqttServer = request->getParam("mqttServer", true)->value();
  if (request->hasParam("mqttPort", true)) {
    cfg.mqttPort = static_cast<uint16_t>(request->getParam("mqttPort", true)->value().toInt());
  }
  if (request->hasParam("mqttUser", true)) cfg.mqttUser = request->getParam("mqttUser", true)->value();
  if (request->hasParam("mqttPassword", true)) cfg.mqttPassword = request->getParam("mqttPassword", true)->value();

  if (request->hasParam("brandingVendorName", true)) {
    cfg.brandingVendorName = request->getParam("brandingVendorName", true)->value();
  }

  cfg.rebootScheduleEnabled = request->hasParam("rebootScheduleEnabled", true);
  if (request->hasParam("rebootTime", true)) {
    // <input type="time"> liefert "HH:MM" (Browser validiert das Format
    // bereits) - trotzdem defensiv parsen statt blind zu vertrauen, falls
    // der Request nicht ueber das Formular kommt.
    String t = request->getParam("rebootTime", true)->value();
    int colon = t.indexOf(':');
    if (colon > 0) {
      int hour = t.substring(0, colon).toInt();
      int minute = t.substring(colon + 1).toInt();
      if (hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59) {
        cfg.rebootHour = static_cast<uint8_t>(hour);
        cfg.rebootMinute = static_cast<uint8_t>(minute);
      }
    }
  }

  // Kollisions-Check: nur wenn DHCP aus ist UND sich die statische IP
  // gegenueber der aktuell aktiven Adresse tatsaechlich aendert - vermeidet
  // einen Ping bei jedem Speichern unveraenderter Netzwerkeinstellungen
  // (dieses Formular deckt alle Einstellungsblocks auf einmal ab).
  String ipConflictError;
  IPAddress newWlanIp;
  if (!cfg.wlanDhcp && newWlanIp.fromString(cfg.wlanIp) && newWlanIp != _network.getWlanIp() &&
      ipRespondsToPing(newWlanIp)) {
    ipConflictError = "WLAN-IP " + cfg.wlanIp + " ist bereits belegt (ein Geraet antwortet auf Ping).";
  }
  if (!ipConflictError.isEmpty()) {
    _data.pushLogEntry(ipConflictError + " Einstellungen NICHT uebernommen.", 3);
    String body = "<h1>IP-Adresse belegt</h1><p>" + ipConflictError +
                  "</p><p>Alle Einstellungen dieser Seite wurden <b>nicht</b> uebernommen - bitte eine andere "
                  "Adresse waehlen und erneut speichern.</p><p><a href=\"/settings\">Zurueck zu den "
                  "Einstellungen</a></p>";
    request->send(409, "text/html", buildPageShell("IP belegt", body));
    return;
  }

  _config.setConfig(cfg);
  _data.pushLogEntry("Einstellungen gespeichert (Reboot noetig fuer Netzwerk-/SNMP-Aenderungen)");

  request->redirect("/settings");
}

void WebServerManager::handleApiConfigExport(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;
  String xml = _config.exportXml();
  AsyncWebServerResponse* response = request->beginResponse(200, "application/xml", xml);
  response->addHeader("Content-Disposition", "attachment; filename=config.xml");
  request->send(response);
}

void WebServerManager::handleApiConfigImportUpload(AsyncWebServerRequest* request, const String& filename,
                                                    size_t index, uint8_t* data, size_t len, bool final) {
  if (!checkAuth(request)) return;

  if (index == 0) _importBuffer = "";
  for (size_t i = 0; i < len; i++) _importBuffer += (char)data[i];

  if (final) {
    if (_config.importXml(_importBuffer)) {
      _config.save();
      _data.pushLogEntry("Konfiguration importiert (Reboot empfohlen)");
    } else {
      _data.pushLogEntry("Konfigurationsimport fehlgeschlagen (ungueltiges XML)", 3);
    }
  }
}

void WebServerManager::handleApiReboot(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;
  request->send(200, "text/plain", "Geraet startet neu...");
  _data.pushLogEntry("Reboot ueber Einstellungsseite ausgeloest");
  delay(500);
  ESP.restart();
}

void WebServerManager::handleApiWifiScan(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;

  // Nicht-blockierend (siehe Klassenkommentar): WiFi.scanComplete() liefert
  // WIFI_SCAN_FAILED (-2), solange kein Scan laeuft oder noch keiner
  // gestartet wurde -> in diesem Fall einen neuen asynchronen Scan anstossen
  // und sofort mit "started" antworten. Die Seite pollt diesen Endpunkt
  // anschliessend alle ~1,5s (bis zu ~20s), bis "done" mit den Ergebnissen
  // zurueckkommt. WiFi.scanNetworks(true) aktiviert dafuer intern kurz STA
  // zusaetzlich zum laufenden Fallback-AP (WIFI_MODE_AP_STA), ohne den AP
  // selbst zu unterbrechen.
  int result = WiFi.scanComplete();

  if (result == WIFI_SCAN_RUNNING) {
    request->send(200, "application/json", "{\"status\":\"running\"}");
    return;
  }
  if (result == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true);
    request->send(200, "application/json", "{\"status\":\"started\"}");
    return;
  }

  JsonDocument doc;
  doc["status"] = "done";
  JsonArray networks = doc["networks"].to<JsonArray>();
  for (int i = 0; i < result; i++) {
    JsonObject o = networks.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
  }
  WiFi.scanDelete();

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

void WebServerManager::handleApiWifiConnect(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;

  DeviceConfig cfg = _config.getConfig();
  if (request->hasParam("wlanSsid", true)) cfg.wlanSsid = request->getParam("wlanSsid", true)->value();
  if (request->hasParam("wlanPsk", true)) cfg.wlanPsk = request->getParam("wlanPsk", true)->value();
  cfg.wlanPendingTest = true;
  _config.setConfig(cfg);
  _data.pushLogEntry("Neues WLAN \"" + cfg.wlanSsid + "\" gespeichert, starte neu zum Verbindungstest");

  request->send(200, "text/plain", "Gespeichert, Geraet startet neu ...");
  delay(500);
  ESP.restart();
}

void WebServerManager::handleApiFactoryReset(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;

  String scope = request->hasParam("scope", true) ? request->getParam("scope", true)->value() : "all";

  if (scope == "values") {
    // Nur der 7-Tage-Verlauf - config.xml bleibt komplett unangetastet.
    LittleFS.remove("/history.csv");
    _data.pushLogEntry("Werksreset: Messwerte (7-Tage-Verlauf) geloescht", 3);

  } else if (scope == "config") {
    // Alle DeviceConfig-Felder auf Standard, AUSSER brandingVendorName -
    // Branding hat mit "branding" jetzt einen eigenen Reset-Umfang und soll
    // von einem reinen Konfigurations-Reset nicht mit betroffen sein.
    String keepBrandingName = _config.getConfig().brandingVendorName;
    DeviceConfig fresh;
    fresh.brandingVendorName = keepBrandingName;
    _config.setConfig(fresh);
    _data.pushLogEntry("Werksreset: Konfiguration auf Standardwerte zurueckgesetzt (Branding erhalten)", 3);

  } else if (scope == "branding") {
    // Nur Anbietername + Logo-Datei - alle uebrigen Einstellungen (WLAN,
    // Passwort, Kalibrierung, Syslog, SNMP, MQTT) bleiben unangetastet.
    DeviceConfig cfg = _config.getConfig();
    cfg.brandingVendorName = "";
    _config.setConfig(cfg);
    _branding.deleteLogo();
    _data.pushLogEntry("Werksreset: Anbieter-Branding entfernt", 3);

  } else {
    // scope=="all" oder fehlender/unbekannter Wert - vollstaendiger Reset
    // als sicherste Default-Annahme (deckt auch alte Web-UI-Caches ab, die
    // noch das frühere scope="settings" senden könnten).
    _config.setConfig(DeviceConfig());
    LittleFS.remove("/history.csv");
    _branding.deleteLogo();
    _data.pushLogEntry("Werksreset: Alles zurueckgesetzt (Einstellungen, Messwerte, Branding)", 3);
  }

  request->send(200, "text/plain", "Zurueckgesetzt, Geraet startet neu ...");
  delay(500);
  ESP.restart();
}

void WebServerManager::handleApiNetworkApply(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;

  // Bibliotheks-/ESP-IDF-Standardwert fuer die Wartezeit auf eine DHCP-
  // Lease. Laenger als der 1s-Ping-Timeout, da eine vollstaendige
  // DHCP-Verhandlung (Discover/Offer/Request/Ack) mehr Umlaeufe braucht -
  // noch nicht auf echter Hardware verifiziert, ob das den Async-Webserver-
  // Handler zu lange blockiert (siehe die WiFi.scanNetworks()-Erfahrung,
  // docs/entscheidungen.md) - bei Auffaelligkeiten (Reboot waehrend des
  // Tests) hier zuerst nachsehen.
  static const unsigned long DHCP_TEST_TIMEOUT_MS = 8000;

  bool dhcp = request->hasParam("dhcp", true) && request->getParam("dhcp", true)->value() == "1";
  DeviceConfig cfg = _config.getConfig();

  if (dhcp) {
    // Live-Test auf der bestehenden Verbindung: WiFi.config() mit
    // Nulladressen erzwingt einen neuen DHCP-Lauf, OHNE die WLAN-Assoziation
    // zu trennen (reiner L3-Vorgang). Erst bei tatsaechlich erhaltener
    // Lease (IP != 0.0.0.0) gilt der Test als erfolgreich.
    IPAddress zero(0, 0, 0, 0);
    WiFi.config(zero, zero, zero);
    unsigned long start = millis();
    bool gotLease = false;
    while (millis() - start < DHCP_TEST_TIMEOUT_MS) {
      if (WiFi.localIP() != zero) {
        gotLease = true;
        break;
      }
      delay(100);
    }
    if (!gotLease) {
      // Zuletzt gespeicherte Konfiguration live wiederherstellen, damit die
      // laufende Verbindung (inkl. dieser HTTP-Antwort) nicht im
      // DHCP-Test-Zwischenzustand haengen bleibt.
      _network.reapplyWlanConfig();
      _data.pushLogEntry("WLAN: kein DHCP-Lease erhalten - Einstellungen NICHT uebernommen.", 3);
      request->send(409, "text/plain",
                     "Kein DHCP-Server im Netz gefunden (keine Lease erhalten) - Einstellungen NICHT "
                     "uebernommen.");
      return;
    }
    cfg.wlanDhcp = true;
  } else {
    IPAddress newIp;
    if (!request->hasParam("ip", true) || !newIp.fromString(request->getParam("ip", true)->value())) {
      request->send(400, "text/plain", "Ungueltige IP-Adresse.");
      return;
    }
    if (newIp != _network.getWlanIp() && ipRespondsToPing(newIp)) {
      _data.pushLogEntry("WLAN-IP " + newIp.toString() + " ist bereits belegt - Einstellungen NICHT uebernommen.",
                          3);
      request->send(409, "text/plain",
                     "IP " + newIp.toString() +
                         " ist bereits belegt (ein Geraet antwortet auf Ping) - Einstellungen NICHT uebernommen.");
      return;
    }
    cfg.wlanDhcp = false;
    cfg.wlanIp = newIp.toString();
    if (request->hasParam("mask", true)) cfg.wlanMask = request->getParam("mask", true)->value();
    if (request->hasParam("gateway", true)) cfg.wlanGateway = request->getParam("gateway", true)->value();
    if (request->hasParam("dns", true)) cfg.wlanDns = request->getParam("dns", true)->value();
  }

  _config.setConfig(cfg);
  _data.pushLogEntry("WLAN-Netzwerkeinstellungen geprueft und uebernommen, starte neu");
  request->send(200, "text/plain", "Geprueft und uebernommen, Geraet startet neu ...");
  delay(500);
  ESP.restart();
}

void WebServerManager::handleApiBrandingLogoUpload(AsyncWebServerRequest* request, const String& filename,
                                                    size_t index, uint8_t* data, size_t len, bool final) {
  if (!checkAuth(request)) return;

  if (index == 0) _brandingUploadOk = _branding.beginLogoUpload();
  if (_brandingUploadOk) {
    _brandingUploadOk = _branding.writeLogoUploadChunk(data, len);
  }
  if (final) {
    _brandingUploadOk = _brandingUploadOk && _branding.endLogoUpload();
  }
}

void WebServerManager::handleBrandingLogoBmp(AsyncWebServerRequest* request) {
  static uint8_t logoBuf[BrandingManager::LOGO_BYTES];
  if (!_branding.loadLogo(logoBuf, sizeof(logoBuf))) {
    request->send(404, "text/plain", "Kein Logo hinterlegt");
    return;
  }

  static uint8_t bmpBuf[BMP_HEADER_BYTES + BrandingManager::LOGO_BYTES];
  buildLogoBmp(logoBuf, sizeof(logoBuf), BrandingManager::LOGO_WIDTH, BrandingManager::LOGO_HEIGHT, bmpBuf);

  AsyncWebServerResponse* response = request->beginResponse(200, "image/bmp", bmpBuf, sizeof(bmpBuf));
  response->addHeader("Cache-Control", "no-cache");
  request->send(response);
}

void WebServerManager::handleApiBrandingLogoDelete(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;
  _branding.deleteLogo();
  _data.pushLogEntry("Anbieter-Logo entfernt");
  request->redirect("/settings");
}

// ----------------------------------------------------------------------------
void WebServerManager::begin() {
  _server.on("/", HTTP_GET, [this](AsyncWebServerRequest* r) { handleRoot(r); });
  _server.on("/settings", HTTP_GET, [this](AsyncWebServerRequest* r) { handleSettingsPage(r); });
  _server.on("/values.csv", HTTP_GET, [this](AsyncWebServerRequest* r) { handleValuesCsv(r); });
  _server.on("/log.txt", HTTP_GET, [this](AsyncWebServerRequest* r) { handleLogFile(r, "/log.txt"); });
  _server.on("/log.old.txt", HTTP_GET, [this](AsyncWebServerRequest* r) { handleLogFile(r, "/log.old.txt"); });

  _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* r) { handleApiStatus(r); });
  _server.on("/api/sensors", HTTP_GET, [this](AsyncWebServerRequest* r) { handleApiSensors(r); });
  _server.on("/api/network", HTTP_GET, [this](AsyncWebServerRequest* r) { handleApiNetwork(r); });
  _server.on("/api/logs", HTTP_GET, [this](AsyncWebServerRequest* r) { handleApiLogs(r); });
  _server.on("/api/graph", HTTP_GET, [this](AsyncWebServerRequest* r) { handleApiGraph(r); });

  _server.on("/api/config", HTTP_GET, [this](AsyncWebServerRequest* r) { handleApiConfigGet(r); });
  _server.on("/api/config", HTTP_POST, [this](AsyncWebServerRequest* r) { handleApiConfigPost(r); });
  _server.on("/api/config/export", HTTP_GET, [this](AsyncWebServerRequest* r) { handleApiConfigExport(r); });

  _server.on(
      "/api/config/import", HTTP_POST,
      [this](AsyncWebServerRequest* r) {
        if (checkAuth(r)) r->redirect("/settings");
      },
      [this](AsyncWebServerRequest* r, String filename, size_t index, uint8_t* data, size_t len, bool final) {
        handleApiConfigImportUpload(r, filename, index, data, len, final);
      });

  _server.on("/api/reboot", HTTP_POST, [this](AsyncWebServerRequest* r) { handleApiReboot(r); });
  _server.on("/api/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest* r) { handleApiWifiScan(r); });
  _server.on("/api/wifi/connect", HTTP_POST, [this](AsyncWebServerRequest* r) { handleApiWifiConnect(r); });
  _server.on("/api/factory-reset", HTTP_POST, [this](AsyncWebServerRequest* r) { handleApiFactoryReset(r); });
  _server.on("/api/network/apply", HTTP_POST, [this](AsyncWebServerRequest* r) { handleApiNetworkApply(r); });

  _server.on(
      "/api/ota/upload", HTTP_POST,
      [this](AsyncWebServerRequest* r) {
        if (!checkAuth(r)) return;
        if (_otaSuccess) {
          r->send(200, "text/plain", "Update erfolgreich, Geraet startet neu...");
          _data.pushLogEntry("OTA (lokaler Upload) erfolgreich, Neustart");
          delay(500);
          ESP.restart();
        } else if (!_otaInProgress) {
          _data.pushLogEntry("OTA (lokaler Upload) fehlgeschlagen (Schreibfehler)", 3);
          r->send(500, "text/plain", "Update fehlgeschlagen (Schreibfehler)");
        } else if (!_ota.markerFound()) {
          _data.pushLogEntry("OTA abgelehnt: kein Firmware-Erkennungsmerkmal gefunden", 3);
          r->send(400, "text/plain", "Update abgelehnt: die Datei enthaelt kein gueltiges Firmware-Erkennungsmerkmal.");
        } else if (!_ota.identityMatches()) {
          _data.pushLogEntry("OTA abgelehnt: .bin gehoert zu einem anderen Projekt", 3);
          r->send(400, "text/plain", "Update abgelehnt: die hochgeladene Firmware stammt von einem anderen Projekt.");
        } else if (!_ota.versionAllowed()) {
          _data.pushLogEntry("OTA abgelehnt: aeltere Firmware-Version", 3);
          r->send(400, "text/plain",
                  "Update abgelehnt: die hochgeladene Version ist aelter als die laufende "
                  "(Downgrade nicht aktiviert).");
        } else {
          _data.pushLogEntry("OTA (lokaler Upload) fehlgeschlagen", 3);
          r->send(500, "text/plain", "Update fehlgeschlagen");
        }
      },
      [this](AsyncWebServerRequest* r, String filename, size_t index, uint8_t* data, size_t len, bool final) {
        if (!checkAuth(r)) return;
        if (index == 0) {
          // Checkbox steht im Formular VOR dem Datei-Feld, damit sie hier
          // schon geparst ist - ESPAsyncWebServer parst Multipart-Felder in
          // Reihenfolge des Request-Bodys, ein Feld nach dem Datei-Input
          // waere an dieser Stelle (erster Chunk der Datei) noch nicht
          // verfuegbar.
          //
          // r->contentLength() statt UPDATE_SIZE_UNKNOWN: ohne bekannte
          // Groesse legt Update.begin() die GESAMTE OTA-Partitionsgroesse
          // als zu loeschenden Bereich zugrunde (siehe Updater.cpp: "size ==
          // UPDATE_SIZE_UNKNOWN -> size = partition->size") statt nur der
          // tatsaechlich benoetigten Groesse - ein langer blockierender
          // Flash-Loeschvorgang gleich zu Beginn des Uploads, der auf
          // echter Hardware reproduzierbar einen kurzen WLAN-Abriss
          // ausgeloest hat (Interrupts/Cache waehrend Flash-Operationen
          // kurz deaktiviert) und dabei die laufende TCP-Verbindung des
          // Uploads gekappt hat - siehe docs/entscheidungen.md. Die
          // Content-Length des Multipart-Bodys ist geringfuegig groesser
          // als die reine .bin (Boundary-/Header-Overhead), das ist fuer
          // die Groessenpruefung in Update.begin() unproblematisch.
          _ota.setAllowDowngrade(r->hasParam("otaForceDowngrade", true));
          _otaInProgress = _ota.beginLocalUpdate(r->contentLength());
          _otaSuccess = false;
        }
        if (_otaInProgress) {
          _otaInProgress = _ota.writeLocalUpdateChunk(data, len);
        }
        if (final && _otaInProgress) {
          _otaSuccess = _ota.endLocalUpdate();
        }
      });

  _server.on("/branding/logo.bmp", HTTP_GET, [this](AsyncWebServerRequest* r) { handleBrandingLogoBmp(r); });
  _server.on("/api/branding/logo/delete", HTTP_POST,
             [this](AsyncWebServerRequest* r) { handleApiBrandingLogoDelete(r); });
  _server.on(
      "/api/branding/logo", HTTP_POST,
      [this](AsyncWebServerRequest* r) {
        if (!checkAuth(r)) return;
        if (_brandingUploadOk) {
          _data.pushLogEntry("Anbieter-Logo hochgeladen");
        } else {
          _data.pushLogEntry("Logo-Upload fehlgeschlagen (falsches Format/Groesse?)", 3);
        }
        r->redirect("/settings");
      },
      [this](AsyncWebServerRequest* r, String filename, size_t index, uint8_t* data, size_t len, bool final) {
        handleApiBrandingLogoUpload(r, filename, index, data, len, final);
      });

  _server.begin();
  Serial.println("[WEB] Server gestartet auf Port 80");
}
