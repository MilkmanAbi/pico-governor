/*
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║  PICOMIMI CPU AUTO POWER GOVERNOR v2.3                                    ║
 * ║  Automatic CPU Frequency Scaling for RP2040/RP2350                        ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  Author: Abinaash (from Picomimi-AxisOS)                                  ║
 * ║  License: MIT                                                             ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 *
 * USAGE:
 *   #include <PicomimiGovernor.h>
 *   
 *   void setup() {
 *     PicomimiGov.begin(PICOMIMI_RP2350);  // or PICOMIMI_RP2040
 *   }
 *   
 *   void loop() {
 *     PicomimiGov.run();
 *     
 *     doYourWork();
 *     PicomimiGov.idle(10);  // <-- use idle() instead of delay()
 *   }
 *
 * HOW IT WORKS:
 *   - Measures time spent in user code vs idle time
 *   - Fast loops with no idle() = high load = boost frequency
 *   - Loops with idle() calls = low load = save power
 *
 * MANUAL MODE:
 *   PicomimiGov.begin(PICOMIMI_RP2350, true);  // true = enable serial commands
 *   // Type "gov" in Serial Monitor
 */

#ifndef PICOMIMI_GOVERNOR_H
#define PICOMIMI_GOVERNOR_H

#include <Arduino.h>
#include <hardware/clocks.h>
#include <hardware/vreg.h>
#include <hardware/adc.h>
#include <hardware/sync.h>
#include <pico/time.h>

// ============================================================================
// CHIP SELECTION
// ============================================================================

enum PicomimiChip : uint8_t {
  PICOMIMI_RP2040 = 0,   // 50-250 MHz
  PICOMIMI_RP2350 = 1    // 50-300 MHz + WFI
};

// ============================================================================
// POWER PROFILES
// ============================================================================

enum PowerProfile : uint8_t {
  PROFILE_ULTRA_LOW = 0,   // 50MHz
  PROFILE_POWERSAVE = 1,   // 100MHz
  PROFILE_BALANCED  = 2,   // 133/150MHz
  PROFILE_PERFORMANCE = 3, // 200/250MHz
  PROFILE_TURBO     = 4,   // 250/300MHz
  PROFILE_COUNT     = 5
};

// ============================================================================
// GOVERNOR CLASS
// ============================================================================

class PicomimiGovernorClass {
public:
  PicomimiGovernorClass();
  
  // ===== CORE API =====
  void begin(PicomimiChip chip, bool manual = false);
  void run();
  void inputBoost();
  
  /**
   * Call this instead of delay() - counts as idle time
   * Optional but improves accuracy for very short delays
   */
  void idle(uint32_t ms);
  void idleMicros(uint32_t us);
  
  // ===== STATUS =====
  uint32_t getFreqMHz();
  float getCPULoad();
  float getTemperature();
  PowerProfile getProfile();
  const char* getProfileName();
  bool isTurbo();
  bool isThrottled();
  
  // ===== MANUAL CONTROL =====
  void setProfile(PowerProfile p, uint32_t duration_sec = 0);
  void setTurbo(uint32_t duration_sec = 30);
  void setPowersave(uint32_t duration_sec = 60);
  void setAuto();
  
private:
  bool _init;
  bool _manual;
  PicomimiChip _chip;
  PowerProfile _profile;
  uint32_t _freq_khz;
  float _temp;
  
  // Loop timing based CPU tracking
  uint64_t _last_run_us;
  uint64_t _run_start_us;
  uint64_t _total_loop_time_us;
  uint64_t _total_idle_time_us;
  uint64_t _period_start_us;
  float _avg_load;
  float _instant_load;
  
  // Scaling
  uint64_t _last_scale_ms;
  
  // Timers
  uint32_t _turbo_start_ms;
  uint32_t _boost_start_ms;
  uint32_t _override_end_ms;
  
  // Flags
  bool _turbo_on;
  bool _throttled;
  bool _boost_on;
  bool _override_on;
  PowerProfile _override_profile;
  bool _adc_init;
  bool _first_run;
  
  // Tables
  const uint32_t* _freq_tbl;
  const uint32_t* _volt_tbl;
  
  // Serial
  String _cmd;
  
  // Internal
  void _setupTables();
  void _updateLoad();
  void _scale();
  void _apply(PowerProfile p);
  void _setFreq(uint32_t khz, uint32_t mv);
  void _thermal();
  void _timeouts();
  void _wfi();
  vreg_voltage _toVreg(uint32_t mv);
  void _handleSerial();
  void _printHelp();
  void _printStatus();
};

extern PicomimiGovernorClass PicomimiGov;

#endif
