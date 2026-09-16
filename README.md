# Sensormeter WLAN Lite

**▶ [Web-Flasher der Sensormeter-Familie](https://peterhagelhof7-cmd.github.io/sensormeter-family/)** — Firmware aller Geräte direkt im Browser flashen.

> **Display-lose Variante** des Sensormeter-WLAN-Projekts: gleiches Board (ESP32-WROOM-32)
> und DHT22, aber **ohne OLED-Anzeige**. Bedienung und Anzeige laufen ausschließlich über die
> Weboberfläche; der Werksreset über den BOOT-Taster bleibt (Feedback jetzt über die Onboard-LED).

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/projektfamilie-dark.png">
  <source media="(prefers-color-scheme: light)" srcset="docs/projektfamilie-light.png">
  <img alt="Sensormeter Projektfamilie: Sensormeter (LAN), Sensormeter WLAN (WLAN), Sensormeter PoE (LAN+PoE) und Sensormeter Display (Touchscreen), verbunden über gemeinsame Architektur und SNMP" src="docs/projektfamilie-light.png">
</picture>

ESP32-basierter Umweltsensor (Temperatur/Luftfeuchte, DHT22) auf einem
generischen, günstigen ESP32-WROOM-32-DevKit (reines WLAN, kein
Ethernet). Stellt die Werte über eine
Weboberfläche, SNMP und Syslog bereit und meldet sich optional per
MQTT-Discovery selbstständig bei Home Assistant an. Unterstützt zudem
optionales Anbieter-Branding (Weisslabel): freier Anbietername plus
Logo im Webseiten-Header.
Bewusst reduzierte,
kostengünstigere Variante des
[Sensormeter](https://github.com/peterhagelhof7-cmd/sensormeter)-Projekts
(WT32-ETH01): genau ein interner Sensor, kein Modulstecker/RJ45, keine
Erweiterbarkeit — wer zwei Sensoren oder eine Modularerweiterung braucht,
nutzt weiterhin Sensormeter bzw. Sensormeter PRO.

[**One-Pager (HTML)**](docs/sensormeter-wlan-onepager.html) — kompakte Projektübersicht auf einer Seite.

**Schwesterprojekte:**
[Sensormeter](https://github.com/peterhagelhof7-cmd/sensormeter) (WT32-ETH01, Ethernet + bis zu 2 Sensoren) ·
[Sensormeter Display](https://github.com/peterhagelhof7-cmd/sensormeter-display) (ESP32-Touchdisplay, fragt Sensormeter-Geräte per SNMP ab)

## Dokumentation

| Datei | Inhalt |
|---|---|
| [Schnellstart](https://github.com/peterhagelhof7-cmd/sensormeter-family/blob/main/docs/schnellstart.pdf) (im `sensormeter-family`-Repo) | Gerät in unter 10 Minuten ans Laufen bringen: Strom, Erststart, Netzwerk, erste Anmeldung — für Sensormeter, Sensormeter WLAN und Sensormeter PoE gemeinsam |
| [docs/sensormeter-wlan-onepager.html](docs/sensormeter-wlan-onepager.html) | One-Pager: Projektübersicht, Architektur, Kennzahlen auf einer Seite |
| [docs/projektfamilie.html](docs/projektfamilie.html) | Architekturskizze: wie die vier Sensormeter-Projekte zusammenhängen |
| [docs/lastenheft.txt](docs/lastenheft.txt) | Fachliche Anforderungen: Webseite, Einstellungen, SNMP-OIDs, Netzwerklogik, Zustandsmodell |
| [docs/pflichtenheft.txt](docs/pflichtenheft.txt) | Technische Umsetzung: FreeRTOS-Tasks, Softwaremodule, Speicherlayout, Fehlerbehandlung |
| [docs/implementierungsplan.html](docs/implementierungsplan.html) | Visueller Implementierungsplan P0–P7 (lokal im Browser öffnen) |
| [docs/stueckliste.md](docs/stueckliste.md) | Bauteile pro Gerät + Preisschätzung |
| [docs/entscheidungen.md](docs/entscheidungen.md) | Entscheidungsprotokoll: Boardwahl, Pinbelegung, OTA-Partitionierung, SNMP-Kompatibilität, bekannte Abweichungen |
| [docs/verdrahtungsplan.html](docs/verdrahtungsplan.html) | Verdrahtung (DHT22): interaktives Schema - Klick auf einen Draht hebt ihn hervor und zeigt Start-/Zielpin |
| [docs/admin-guide.html](docs/admin-guide.html) | Admin-Guide: Inbetriebnahme, Weboberfläche, SNMP/Syslog/MQTT/Branding, Serial-Kommandozeile |
| [docs/PRTG.md](docs/PRTG.md) | PRTG-Integration: OIDs, Geräte-Template-Import, Sensor-Übersicht |
| [docs/prtg-template-sensormeter-wlan.odt](docs/prtg-template-sensormeter-wlan.odt) | Fertiges PRTG-Geräte-Template für Auto-Discovery |
| [docs/ZABBIX.md](docs/ZABBIX.md) | Zabbix-Integration: OIDs, Template-Import, Host-Einrichtung, Trigger |
| [docs/zabbix-template-sensormeter-wlan.yaml](docs/zabbix-template-sensormeter-wlan.yaml) | Fertiges Zabbix-Template |
| [docs/sensormeter-wlan.mib](docs/sensormeter-wlan.mib) | Tool-unabhängige SNMPv2-SMI-MIB (10 OIDs) für SNMP-Tools jenseits von Zabbix, z. B. Checkmk, PRTG generic SNMP, snmpwalk/snmptranslate |
| [scripts/flash.ps1](scripts/flash.ps1) | PowerShell-Skript (fragt zuerst nach Projekt: Sensormeter/WLAN/Display/PoE): Abhängigkeiten installieren, Repo holen, bauen, flashen |
| [scripts/flash.sh](scripts/flash.sh) | Bash-Pendant zu `flash.ps1` für macOS (nur Apple Silicon) und Linux, nur Flashen |
| [scripts/README.md](scripts/README.md) | Ausführliche Doku zu `flash.ps1`/`flash.sh` und `convert-logo.ps1` (Nutzung, Parameter, Beispiele) |

Dieses Projekt hat (im Unterschied zu den beiden Schwesterprojekten) keine
vorgegebene Materialsammlung – Lastenheft, Pflichtenheft und BOM wurden
komplett neu entworfen, angelehnt an die bewährte Architektur des
Sensormeter-Projekts.

## Hardware

- Generisches ESP32-WROOM-32-DevKit (30- oder 38-Pin, reines WLAN)
- DHT22/AM2302, 3-Pin-Modul, an GPIO4
- **Kein OLED-Display** (Lite-Variante) — die frühere I2C-Anzeige entfällt,
  die Pins GPIO21/22 sind wieder frei
- Kein zusätzliches Bauteil für die Bedienung nötig: der ohnehin auf jedem
  DevKit vorhandene **BOOT-Taster** (GPIO0) löst durch langes Halten
  (3s + 20s Countdown, Auslösung erst beim Loslassen als Fail-Safe) einen
  Werksreset der Einstellungen aus. Feedback über die **Onboard-LED (GPIO2)**:
  blinkt während des Countdowns, leuchtet dauerhaft bei „Loslassen zum
  Bestätigen". (Das frühere Durchblättern per kurzem Tipp entfällt mit dem
  Display.) Der zweite Taster (**EN**) ist reiner Hardware-Reset und lässt
  sich softwareseitig nicht nutzen.

## Firmware

`firmware/` ist ein PlatformIO-Projekt (Board `esp32dev`, Framework Arduino).

**Version:** `0.9.4` (Beta) — Versionsschema siehe
[docs/entscheidungen.md](docs/entscheidungen.md#versionierung).

Fertiges Binary für das lokale OTA-Update (kein PlatformIO nötig):
[Releases](https://github.com/peterhagelhof7-cmd/sensormeter-wlan-lite/releases).
Hinweis: Ein Wechsel von der früheren Nicht-Lite-Firmware auf Lite geht wegen
der geänderten OTA-Projekt-ID **nur seriell** (OTA lehnt den Projektwechsel ab).

Aktueller Stand: **Lite-Umbau (Display entfernt) code-vollständig** — die
Nicht-Lite-Vorgängerversion lief über mehrere Test-/Update-Zyklen stabil auf
echter Hardware (DHT22, WLAN inkl. Fallback-AP, Taster, Webserver, SNMP,
Syslog verifiziert); die Lite-Variante ist auf HW noch zu verifizieren, siehe
[docs/implementierungsplan.html](docs/implementierungsplan.html) und
[docs/entscheidungen.md](docs/entscheidungen.md). MQTT/Home-Assistant-
Anbindung ist geflasht und bootet sauber (deaktiviert per Default), aber
noch nicht gegen einen echten Broker/Home-Assistant-Instanz
durchgetestet — siehe `docs/entscheidungen.md`.

**Qualitätskontrolle läuft**: zuletzt (2026-07-31) einen OTA-Update-Bug
gefunden und behoben: Uploads über die Einstellungsseite konnten mitten
im Transfer zu einem harten Reboot führen (Task-Watchdog-Starvation
während der Flash-Schreibvorgänge) — auf echter Hardware per Serial
geflasht und verifiziert (4/4 erfolgreiche OTA-Versuche danach).
Außerdem den Startseiten-Graph auf einen lesbaren 3-Tage-Ausschnitt
(4 Messpunkte/Tag, Wochentag-Beschriftung statt nur Uhrzeit)
umgestellt. Davor (2026-07-18) einen bei Sensormeter gefundenen
Chunkgrößen-Bug im OTA-Marker-Scan übernommen und behoben — siehe
`docs/entscheidungen.md`.

Am schnellsten per PowerShell-Skript einrichten (installiert Python/Git/
PlatformIO bei Bedarf automatisch, klont/aktualisiert das Repo, baut und
flasht):

```
scripts\flash.ps1 -Project wlan
```

Details siehe [docs/admin-guide.html](docs/admin-guide.html). Manuelle
Alternative ohne Skript:

```
cd firmware
cp include/config.h.example include/config.h
pio run              # bauen
pio run --target upload   # flashen
pio device monitor   # seriellen Log ansehen (115200 Baud)
```

Enthalten (P0–P7, siehe [docs/implementierungsplan.html](docs/implementierungsplan.html)):
- `DataManager`: zentrale, mutex-geschützte Datenhaltung (Sensorwert,
  Systemstatus, 7-Tage-Ringpuffer, Log) — direkt vom Sensormeter-Projekt
  übernommenes, bewährtes Muster; Ringpuffer wird bei jedem Stundenwert
  nach `/history.csv` auf LittleFS persistiert, übersteht also einen
  Neustart
- `NetworkManager`: Boot-Zustandsautomat (BOOT → INIT → WLAN_CHECK →
  RUN_NORMAL/FALLBACK_MODE), WLAN-Verbindungsaufbau; im Fallback-Fall
  spannt das Gerät einen eigenen Access Point auf (SSID/PSK `installer`,
  DHCP, nur eigene IP + Subnetzmaske), statt einem bestehenden Netz
  beizutreten; mDNS unter `<systemname>.local`
- `TimeManager`: NTP-Sync (de.pool.ntp.org, CET/CEST), erster Versuch 60s
  nach Boot, danach alle 5h, zusätzlich bei WLAN-Reconnect; ohne Erfolg
  Wiederholung alle 5 Minuten, kein Einfluss auf den Systemzustand
- `ConfigManager`: `config.xml` auf LittleFS (tinyxml2, vendored),
  Laden/Speichern mit Default-Fallback, XML-Import/-Export, sicheres
  Schreiben über `.tmp`-Datei + Rename; Werksreset (nur Einstellungen oder
  Einstellungen + Verlaufsdaten) über die Einstellungsseite
- `StorageManager`: LittleFS-Mount
- `SensorManager`: DHT22-Abfrage alle 60s mit Plausibilitätsprüfung, konfigurierbare
  Kalibrierkorrektur (°C/%, wirkt auf Web-Anzeige, SNMP und CSV gleichermaßen),
  Zeitpunkt der letzten Kalibrierung wird persistent mitgeführt
- `ButtonManager`: BOOT-Taster-Werksreset ohne Display — 3s + 20s Countdown,
  Auslösung erst beim Loslassen (Fail-Safe gegen verklemmten Taster), Feedback
  über die Onboard-LED (GPIO2): Blinken während des Countdowns, Dauerlicht bei
  „Loslassen zum Bestätigen"
- `WebServerManager`/`OtaManager`: Hauptseite, passwortgeschützte
  Einstellungsseite (inkl. Sensor-Kalibrierkorrektur, nicht-blockierendem
  WLAN-Scan mit Direktverbindung-und-Test, Werksreset), REST-API, lokales
  OTA per `.bin`-Upload, Design an das Sensormeter-Display-Projekt angepasst
- `SNMPManager`: SNMP v1/v2c read-only unter `.1.3.6.1.4.1.99999.x`
- `SyslogManager`: Statusreport je Sensorzyklus + sofortige Fehler-Events per UDP 514
- `MqttManager`: optionale Home-Assistant-Anbindung per MQTT-Discovery
  (Sensor-Rolle, Temperatur/Luftfeuchte) — deaktiviert, solange kein
  Broker konfiguriert ist; siehe `docs/entscheidungen.md`
- `BrandingManager`: optionales Anbieter-Branding (Weisslabel) - freier
  Anbietername plus Logo (128x64, 1bpp, per Web-Upload), Anzeige im
  Web-Header, kein PNG/JPEG-Decoder eingebunden
  (Logo wird als minimaler BMP on-the-fly ausgeliefert); siehe
  `docs/entscheidungen.md`
- Serial-Kommandozeile (115200 Baud, + Enter) für den Fall, dass das Gerät
  nur per USB, aber nicht per Netzwerk erreichbar ist: `dhcp` (WLAN auf
  DHCP umstellen), `ip <adresse> <maske> <gateway> [dns]` (statische IP
  setzen), `wifi <ssid> <passwort>` (neue WLAN-Zugangsdaten), `status`
  (aktueller Zustand, rein lesend), `dump`/`upload` (config.xml als XML
  ausgeben/wiedereinspielen - dieselbe Logik wie der Web-XML-Export/
  -Import), `reset`/`reset all` (Werksreset wie über die
  Einstellungsseite). Gleiches Vertrauensmodell wie der
  BOOT-Taster-Werksreset (physischer USB-Zugriff = vertrauenswürdig, kein
  Passwort nötig); alle Kommandos außer `reset`/`reset all` nicht
  destruktiv; siehe `docs/entscheidungen.md`

Partitionstabelle bereits verifiziert (`gen_esp32part.py`): das
Standardschema für `esp32dev` bringt `ota_0`/`ota_1` von Haus aus mit,
keine eigene `partitions.csv` nötig (siehe `docs/entscheidungen.md`).

## Zusammenhang mit den Schwesterprojekten

- SNMP-OID-Struktur bewusst identisch zur Basis von Sensormeter und
  Sensormeter PoE (`.1.3.6.1.4.1.99999.x`), nur ohne LAN-IP- und
  Sensor-2-Zweig (keine passende Hardware vorhanden) — das
  Sensormeter-Display-Projekt kann damit Geräte aus allen drei
  Produktlinien mit identischen OID-Offsets ohne Codeänderung abfragen.
- Fallback-WLAN-Konvention `installer`/`installer` wie beim
  Sensormeter-Projekt übernommen — hier bereits als echter, selbst
  aufgespannter Access Point umgesetzt (DHCP, nur eigene IP + Subnetzmaske);
  das Sensormeter-Projekt (WT32-ETH01) joint stattdessen noch ein
  bestehendes Netz mit diesem Namen, siehe `docs/entscheidungen.md`.

## Über dieses Projekt

Repo-Struktur und Dokumentation entstehen in Zusammenarbeit mit
[Claude](https://claude.com/claude-code) (Anthropic) als KI-Coding-Assistent.
