# Stückliste (BOM) – Sensormeter WLAN Lite

## Pro Gerät

| Bauteil | Menge | Ca.-Preis | Hinweis |
|---|---|---|---|
| ESP32-WROOM-32 DevKit (generisches Board, 30- oder 38-Pin, CP2102/CH340 USB-UART) | 1 | ~3–6 € | Reines WLAN, kein Ethernet nötig – siehe `entscheidungen.md` |
| DHT22/AM2302, 3-Pin-Modul (mit eingebautem Pull-up) | 1 | ~3–7 € | Data → GPIO4 |
| Jumper-Kabel (female-female, für DHT22) | ~3 | < 1 € | Direktverdrahtung, kein Modulstecker/RJ45 nötig |
| Netzteil 5V, ≥ 1 A (USB) | 1 | – | Analog zur Empfehlung im Sensormeter-Projekt; genaue Herleitung folgt nach Fertigstellung der Firmware (Strombudget hängt u. a. von der finalen WLAN-Sendeleistung ab) |

**Geschätzte Materialkosten pro Gerät: ~6–13 €** (ohne Kabel/Kleinteile,
Preise Marktbeobachtung Stand 2026, schwanken je Händler/Region).

Deutlich günstiger als das Sensormeter-Projekt (WT32-ETH01, Spezialboard
mit Ethernet-PHY) – Haupteinsparung durch das generische Standardboard und
den Wegfall von RJ45-Buchse/Pull-up-Widerständen für die Modularerweiterung.

## Werkzeug (einmalig, nicht pro Gerät)

| Werkzeug | Hinweis |
|---|---|
| USB-Kabel (passend zum Board, meist Micro-USB oder USB-C) | Zum Flashen und Betreiben, oft im Lieferumfang des Boards |
| CP2102/CH340-Treiber (Windows, falls nicht automatisch erkannt) | Für die USB-Serial-Erkennung beim Flashen |

## Nicht Teil dieses Projekts (bewusst)

- Ethernet/LAN8720 (kein Ethernet-PHY auf einem generischen DevKit)
- RJ45-Modularanschluss / zweiter Sensor (siehe `lastenheft.txt`
  Abschnitt 4 – dafür existiert bereits Sensormeter PRO)
- Gehäuse/Grundplatte (noch nicht entworfen, folgt ggf. später)
