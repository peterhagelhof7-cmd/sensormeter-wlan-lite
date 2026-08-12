#pragma once

// Pinbelegung Sensormeter WLAN Lite (generisches ESP32-WROOM-32 DevKit).
// Display-lose Variante: kein OLED/I2C mehr (die frueheren I2C-Pins 21/22
// sind wieder frei). DHT22 als einziger Sensor, BOOT-Taster + Onboard-LED
// als display-loser Werksreset-Weg.

// --- DHT22 (genau ein Sensor, kein RJ45/Modulstecker) ---
#define DHT_PIN 4
#define DHT_TYPE DHT22

// --- Taster: der bereits auf jedem ESP32-DevKit vorhandene BOOT-Knopf,
// kein zusaetzliches Bauteil noetig. Nach dem Booten ganz normal als
// Eingang lesbar (aktiv LOW, interner Pullup) - nur waehrend eines Resets
// beeinflusst sein Zustand den Bootmodus, siehe ButtonManager.h. ---
#define BUTTON_BOOT_PIN 0

// --- Status-LED: die auf den meisten ESP32-DevKits vorhandene Onboard-LED
// an GPIO2. GPIO2 ist ein Boot-Strapping-Pin, wird hier aber erst NACH dem
// Boot als Ausgang getrieben (Reset-Feedback im ButtonManager) - das ist
// unkritisch. Fehlt auf einem Board die LED, hat das nur zur Folge, dass es
// kein sichtbares Reset-Feedback gibt (Funktion bleibt). ---
#define STATUS_LED_PIN 2

// Bewusst vermiedene Pins fuer NEUE Peripherie (Boot-Strapping bzw. intern
// am Flash): GPIO0 (siehe oben, hier als bestehender Taster erlaubt), GPIO2,
// GPIO5, GPIO12, GPIO15 (Strapping), GPIO6-11 (internes Flash-SPI, auf
// DevKits nicht herausgefuehrt). Siehe entscheidungen.md.
