/*
 * PICOMIMI GOVERNOR - Basic Example
 * 
 * Governor scales CPU based on work done:
 * - Tight loops with no delay = high load = boost frequency
 * - Loops with delay = idle time = save power
 * 
 * Use PicomimiGov.idle(ms) instead of delay(ms) for accurate tracking.
 */

#include <PicomimiGovernor.h>

void setup() {
  PicomimiGov.begin(PICOMIMI_RP2350);  // or PICOMIMI_RP2040
}

void loop() {
  PicomimiGov.run();
  
  // Your work here
  // ...
  
  // Use idle() for delays - this tells governor you're not working
  PicomimiGov.idle(10);  // 10ms idle = low CPU load
  
  // Using delay() works too, but idle() gives more accurate tracking
}
