/*
 * PICOMIMI GOVERNOR - Manual Mode Example
 * 
 * Pass 'true' to begin() to enable serial commands.
 * 
 * COMMANDS (type in Serial Monitor):
 *   gov           Show help
 *   status        Show current status
 *   auto          Resume auto scaling
 *   turbo 30      Turbo for 30 seconds
 *   save 3600     Powersave for 1 hour
 */

#include <PicomimiGovernor.h>

#define BUTTON_PIN 15

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // true = enable serial commands
  PicomimiGov.begin(PICOMIMI_RP2350, true);
}

void loop() {
  PicomimiGov.run();
  
  // Check button (this is fast, minimal CPU)
  static bool lastBtn = HIGH;
  bool btn = digitalRead(BUTTON_PIN);
  if (btn == LOW && lastBtn == HIGH) {
    PicomimiGov.inputBoost();  // Boost on press
  }
  lastBtn = btn;
  
  // Use idle() instead of delay() for accurate load tracking
  // This tells the governor "I'm not doing work right now"
  PicomimiGov.idle(10);  // 10ms between loops = low load
}
