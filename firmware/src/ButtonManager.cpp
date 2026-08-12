#include "ButtonManager.h"

#include "pins.h"

// Haltezeiten - identisch zum frueheren Display-Reset (DisplayManager).
static const unsigned long RESET_HOLD_MS = 3000UL;         // ab hier "scharf" + LED
static const unsigned long RESET_COUNTDOWN_MS = 20000UL;   // Dauer bis "Loslassen zum Bestaetigen"
static const unsigned long LED_BLINK_MS = 150UL;           // Blinktakt in der Countdown-Phase

ButtonManager::ButtonManager(DataManager& dataManager, ConfigManager& configManager)
    : _data(dataManager), _config(configManager) {}

void ButtonManager::setLed(bool on) {
  _ledOn = on;
  digitalWrite(STATUS_LED_PIN, on ? HIGH : LOW);
}

void ButtonManager::begin() {
  pinMode(BUTTON_BOOT_PIN, INPUT_PULLUP);
  pinMode(STATUS_LED_PIN, OUTPUT);
  setLed(false);
}

void ButtonManager::loop() {
  bool pressed = (digitalRead(BUTTON_BOOT_PIN) == LOW);
  unsigned long now = millis();

  // --- Losgelassen ---
  if (!pressed) {
    if (_pressStartMillis != 0) {
      unsigned long heldMs = now - _pressStartMillis;
      _pressStartMillis = 0;
      setLed(false);
      if (heldMs >= RESET_HOLD_MS + RESET_COUNTDOWN_MS) {
        _data.pushLogEntry("Werksreset ueber Taster ausgeloest (nur Einstellungen)", 3);
        setLed(true);  // Bestaetigung, bleibt bis zum Neustart an
        _config.setConfig(DeviceConfig());
        delay(300);
        ESP.restart();
      }
      // sonst: zu frueh losgelassen -> abgebrochen, keine Wirkung
    }
    return;
  }

  // --- Gedrueckt ---
  if (_pressStartMillis == 0) _pressStartMillis = now;
  unsigned long heldMs = now - _pressStartMillis;

  if (heldMs < RESET_HOLD_MS) {
    if (_ledOn) setLed(false);   // normale Haltezeit -> LED aus
    return;
  }

  if (heldMs >= RESET_HOLD_MS + RESET_COUNTDOWN_MS) {
    if (!_ledOn) setLed(true);   // "scharf" -> LED dauerhaft an, wartet aufs Loslassen
    return;
  }

  // Countdown-Phase -> LED blinkt
  if (now - _lastLedToggleMillis >= LED_BLINK_MS) {
    _lastLedToggleMillis = now;
    setLed(!_ledOn);
  }
}
