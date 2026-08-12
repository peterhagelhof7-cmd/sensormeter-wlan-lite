#pragma once

#include <Arduino.h>
#include "ConfigManager.h"
#include "DataManager.h"

// BOOT-Taster (GPIO0, aktiv LOW) als Werksreset-Weg OHNE Display
// (Sensormeter WLAN Lite hat keine Anzeige mehr - das Feedback laeuft ueber
// die Onboard-LED an GPIO2, siehe pins.h STATUS_LED_PIN).
//
// Ablauf, identisch zum frueheren Display-Weg:
//   - < 3 s halten: nichts (LED aus). Ein kurzer Tipp hat keine Funktion mehr
//     (das fruehere "naechste Seite" entfaellt mit dem Display).
//   - >= 3 s halten: "scharf"-Phase - die LED blinkt (Countdown 20 s laeuft).
//   - >= 3 s + 20 s halten: LED leuchtet dauerhaft ("Loslassen zum Bestaetigen").
//   - LOSLASSEN nach >= 3 s + 20 s: Werksreset NUR der Einstellungen
//     (config.xml auf Defaults, der Verlauf/history.csv bleibt erhalten),
//     dann Neustart.
//
// Der Reset wird bewusst erst BEIM tatsaechlichen LOSLASSEN nach Ablauf der
// vollen Haltezeit ausgeloest, nicht schon waehrend des Haltens - Fail-Safe
// gegen einen verklemmten/defekten Taster, der sonst von selbst (ohne echtes
// Loslassen-Ereignis) einen Reset ausloesen koennte. Wird die Taste vorher
// losgelassen, ist der Vorgang abgebrochen (LED aus). Funktioniert in jedem
// Systemzustand (auch Boot/Fallback), da als Recovery-Weg ganz ohne
// Netzwerkzugriff gedacht.
class ButtonManager {
 public:
  ButtonManager(DataManager& dataManager, ConfigManager& configManager);

  void begin();
  void loop();

 private:
  DataManager& _data;
  ConfigManager& _config;

  // 0 = nicht gedrueckt, sonst millis() seit Beginn des durchgehenden Drucks.
  unsigned long _pressStartMillis = 0;
  unsigned long _lastLedToggleMillis = 0;
  bool _ledOn = false;

  void setLed(bool on);
};
