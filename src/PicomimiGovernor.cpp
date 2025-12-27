/*
 * PICOMIMI CPU AUTO POWER GOVERNOR - Implementation v2.3
 * 
 * CPU Load Tracking Strategy:
 * 
 * The fundamental insight: REAL WORK TAKES TIME.
 * - Empty loop = runs in ~10-50us = idle (just loop overhead)
 * - Light work = 100us - 1ms = low-medium load
 * - Heavy work = 1ms+ = high load
 * - Delays = explicitly marked idle time
 * 
 * We measure time between run() calls (user code time) and use thresholds
 * to determine if actual work is happening or if it's just spinning.
 */

#include "PicomimiGovernor.h"

// ============================================================================
// TABLES
// ============================================================================

static const uint32_t RP2040_FREQ[] = { 50000, 100000, 133000, 200000, 250000 };
static const uint32_t RP2040_VOLT[] = { 950, 1000, 1050, 1100, 1150 };
static const uint32_t RP2350_FREQ[] = { 50000, 100000, 150000, 250000, 300000 };
static const uint32_t RP2350_VOLT[] = { 950, 1000, 1050, 1100, 1250 };
static const char* PROFILE_NAMES[] = { "ULTRA_LOW", "POWERSAVE", "BALANCED", "PERFORMANCE", "TURBO" };

// ============================================================================
// LOAD DETECTION THRESHOLDS
// ============================================================================

// User code time thresholds (microseconds)
// These determine how we interpret the time spent in user code
#define IDLE_THRESHOLD_US      100     // < 100us = just loop overhead, no real work
#define LIGHT_WORK_US          500     // 100-500us = light work
#define MEDIUM_WORK_US         2000    // 500us-2ms = medium work
#define HEAVY_WORK_US          10000   // 2-10ms = heavy work
// > 10ms = either very heavy work OR delays (check idle time)

// Scaling thresholds (load %)
#define TURBO_UP     70
#define TURBO_DOWN   55
#define PERF_UP      45
#define PERF_DOWN    30
#define BAL_UP       20
#define BAL_DOWN     12
#define SAVE_DOWN    5
#define ULTRA_DOWN   2

// Timing
#define LOAD_PERIOD_MS       200
#define SCALE_INTERVAL_MS    100
#define TURBO_MAX_MS         10000
#define BOOST_DURATION_MS    300
#define THERMAL_THROTTLE     70.0f
#define THERMAL_CRITICAL     80.0f
#define THERMAL_RELEASE      60.0f
#define LOAD_SMOOTH          0.3f

// ============================================================================
// GLOBAL
// ============================================================================

PicomimiGovernorClass PicomimiGov;

// ============================================================================
// CONSTRUCTOR
// ============================================================================

PicomimiGovernorClass::PicomimiGovernorClass() :
  _init(false), _manual(false), _chip(PICOMIMI_RP2040),
  _profile(PROFILE_BALANCED), _freq_khz(133000), _temp(25.0f),
  _last_run_us(0), _run_start_us(0), _total_loop_time_us(0),
  _total_idle_time_us(0), _period_start_us(0),
  _avg_load(0), _instant_load(0), _last_scale_ms(0),
  _turbo_start_ms(0), _boost_start_ms(0), _override_end_ms(0),
  _turbo_on(false), _throttled(false), _boost_on(false),
  _override_on(false), _override_profile(PROFILE_BALANCED),
  _adc_init(false), _first_run(true), _freq_tbl(nullptr),
  _volt_tbl(nullptr), _cmd("")
{
}

// ============================================================================
// PUBLIC API
// ============================================================================

void PicomimiGovernorClass::begin(PicomimiChip chip, bool manual) {
  _chip = chip;
  _manual = manual;
  _setupTables();
  
  adc_init();
  adc_set_temp_sensor_enabled(true);
  _adc_init = true;
  
  _profile = PROFILE_BALANCED;
  _freq_khz = _freq_tbl[PROFILE_BALANCED];
  
  uint64_t now = time_us_64();
  _last_run_us = now;
  _run_start_us = now;
  _period_start_us = now;
  _total_loop_time_us = 0;
  _total_idle_time_us = 0;
  _first_run = true;
  _last_scale_ms = to_ms_since_boot(get_absolute_time());
  
  _init = true;
  
  if (_manual) {
    Serial.println(F("\n╔══════════════════════════════════════════╗"));
    Serial.println(F("║  PICOMIMI CPU GOVERNOR v2.3              ║"));
    Serial.println(F("╚══════════════════════════════════════════╝"));
    Serial.print(F("Chip: "));
    Serial.println(_chip == PICOMIMI_RP2350 ? "RP2350" : "RP2040");
    Serial.println(F("Type 'gov' for commands.\n"));
  }
}

void PicomimiGovernorClass::run() {
  if (!_init) return;
  
  uint64_t now_us = time_us_64();
  
  // Measure user code time (time since last run() completed)
  if (!_first_run) {
    uint64_t user_code_time = now_us - _last_run_us;
    
    // Subtract any explicitly marked idle time
    uint64_t work_time = (user_code_time > _total_idle_time_us) 
                         ? (user_code_time - _total_idle_time_us) 
                         : 0;
    
    _total_loop_time_us += work_time;
  }
  _first_run = false;
  
  // Reset idle accumulator for next iteration
  _total_idle_time_us = 0;
  
  uint32_t now_ms = to_ms_since_boot(get_absolute_time());
  
  _updateLoad();
  
  if (now_ms - _last_scale_ms >= SCALE_INTERVAL_MS) {
    _thermal();
    _timeouts();
    if (!_override_on) _scale();
    _last_scale_ms = now_ms;
  }
  
  if (_chip == PICOMIMI_RP2350 && _profile == PROFILE_ULTRA_LOW && 
      _avg_load < ULTRA_DOWN && !_throttled) {
    _wfi();
  }
  
  if (_manual) _handleSerial();
  
  _last_run_us = time_us_64();
}

void PicomimiGovernorClass::idle(uint32_t ms) {
  _total_idle_time_us += (uint64_t)ms * 1000ULL;
  delay(ms);
}

void PicomimiGovernorClass::idleMicros(uint32_t us) {
  _total_idle_time_us += us;
  delayMicroseconds(us);
}

void PicomimiGovernorClass::inputBoost() {
  if (!_init || _throttled) return;
  _boost_start_ms = to_ms_since_boot(get_absolute_time());
  _boost_on = true;
  if (_profile < PROFILE_PERFORMANCE) _apply(PROFILE_PERFORMANCE);
}

// ============================================================================
// STATUS
// ============================================================================

uint32_t PicomimiGovernorClass::getFreqMHz() { return _freq_khz / 1000; }
float PicomimiGovernorClass::getCPULoad() { return _avg_load; }
float PicomimiGovernorClass::getTemperature() { return _temp; }
PowerProfile PicomimiGovernorClass::getProfile() { return _profile; }
const char* PicomimiGovernorClass::getProfileName() { return PROFILE_NAMES[_profile]; }
bool PicomimiGovernorClass::isTurbo() { return _turbo_on; }
bool PicomimiGovernorClass::isThrottled() { return _throttled; }

// ============================================================================
// MANUAL CONTROL
// ============================================================================

void PicomimiGovernorClass::setProfile(PowerProfile p, uint32_t duration_sec) {
  if (p >= PROFILE_COUNT) return;
  _override_on = true;
  _override_profile = p;
  _override_end_ms = duration_sec > 0 
    ? to_ms_since_boot(get_absolute_time()) + (duration_sec * 1000) : 0;
  _apply(p);
}

void PicomimiGovernorClass::setTurbo(uint32_t s) { setProfile(PROFILE_TURBO, s); }
void PicomimiGovernorClass::setPowersave(uint32_t s) { setProfile(PROFILE_POWERSAVE, s); }
void PicomimiGovernorClass::setAuto() { _override_on = false; _override_end_ms = 0; }

// ============================================================================
// INTERNAL - Setup
// ============================================================================

void PicomimiGovernorClass::_setupTables() {
  _freq_tbl = (_chip == PICOMIMI_RP2350) ? RP2350_FREQ : RP2040_FREQ;
  _volt_tbl = (_chip == PICOMIMI_RP2350) ? RP2350_VOLT : RP2040_VOLT;
}

// ============================================================================
// INTERNAL - Load Calculation
// ============================================================================

void PicomimiGovernorClass::_updateLoad() {
  uint64_t now_us = time_us_64();
  uint64_t period_elapsed = now_us - _period_start_us;
  
  if (period_elapsed < (LOAD_PERIOD_MS * 1000ULL)) return;
  
  // Calculate average work time per loop
  // total_loop_time_us = sum of (user_code_time - idle_time) over the period
  
  // Estimate number of loops (rough)
  // This isn't perfect but gives us average work per iteration
  uint64_t avg_work_per_loop;
  
  if (_total_loop_time_us < IDLE_THRESHOLD_US * 10) {
    // Very little total work - basically idle
    _instant_load = 0.0f;
  } else {
    // Calculate load based on how much work time accumulated
    // Compare against period time
    // load = work_time / period_time * 100, but capped
    
    float work_ratio = (float)_total_loop_time_us / (float)period_elapsed;
    
    // Apply work-time-based scaling
    // The idea: if work_ratio is high AND work time is substantial, it's real load
    // If work_ratio is high but absolute work time is tiny, it's just tight spinning
    
    // Average work per loop iteration
    // Assume ~1000 loops per 200ms period at minimum (tight loop)
    // If total work is < 100ms over 200ms, we're doing real work
    // If total work is close to 200ms, we're in a tight compute loop
    
    if (_total_loop_time_us < 1000) {
      // Less than 1ms total work in 200ms = essentially idle
      _instant_load = 0.0f;
    } else if (_total_loop_time_us < 10000) {
      // 1-10ms total work in 200ms = very light load (0.5-5%)
      _instant_load = ((float)_total_loop_time_us / (float)period_elapsed) * 100.0f;
    } else if (_total_loop_time_us < 50000) {
      // 10-50ms total work = light load (5-25%)
      _instant_load = ((float)_total_loop_time_us / (float)period_elapsed) * 100.0f;
    } else if (_total_loop_time_us < 100000) {
      // 50-100ms total work = medium load (25-50%)
      _instant_load = ((float)_total_loop_time_us / (float)period_elapsed) * 100.0f;
    } else {
      // 100ms+ total work = high load
      _instant_load = ((float)_total_loop_time_us / (float)period_elapsed) * 100.0f;
    }
  }
  
  // Clamp
  if (_instant_load < 0) _instant_load = 0;
  if (_instant_load > 100) _instant_load = 100;
  
  // Smooth
  _avg_load = (_avg_load * (1.0f - LOAD_SMOOTH)) + (_instant_load * LOAD_SMOOTH);
  
  // Reset
  _period_start_us = now_us;
  _total_loop_time_us = 0;
}

// ============================================================================
// INTERNAL - Scaling
// ============================================================================

void PicomimiGovernorClass::_scale() {
  if (_boost_on && _profile >= PROFILE_PERFORMANCE) return;
  
  PowerProfile target = _profile;
  float load = _avg_load;
  bool can_up = !_throttled;
  
  if (can_up) {
    if (load >= TURBO_UP && _profile < PROFILE_TURBO) target = PROFILE_TURBO;
    else if (load >= PERF_UP && _profile < PROFILE_PERFORMANCE) target = PROFILE_PERFORMANCE;
    else if (load >= BAL_UP && _profile < PROFILE_BALANCED) target = PROFILE_BALANCED;
  }
  
  if (_profile == PROFILE_TURBO && load < TURBO_DOWN) target = PROFILE_PERFORMANCE;
  else if (_profile == PROFILE_PERFORMANCE && load < PERF_DOWN) target = PROFILE_BALANCED;
  else if (_profile == PROFILE_BALANCED && load < BAL_DOWN) target = PROFILE_POWERSAVE;
  else if (_profile == PROFILE_POWERSAVE && load < SAVE_DOWN) target = PROFILE_ULTRA_LOW;
  
  if (_throttled && target > PROFILE_BALANCED) target = PROFILE_BALANCED;
  
  if (target != _profile) _apply(target);
}

void PicomimiGovernorClass::_apply(PowerProfile p) {
  if (p >= PROFILE_COUNT) return;
  _setFreq(_freq_tbl[p], _volt_tbl[p]);
  _profile = p;
  
  if (p == PROFILE_TURBO && !_turbo_on) {
    _turbo_start_ms = to_ms_since_boot(get_absolute_time());
    _turbo_on = true;
  } else if (p != PROFILE_TURBO) {
    _turbo_on = false;
  }
}

void PicomimiGovernorClass::_setFreq(uint32_t khz, uint32_t mv) {
  vreg_voltage vr = _toVreg(mv);
  
  if (khz > _freq_khz) {
    vreg_set_voltage(vr);
    busy_wait_us(150);
  }
  
  if (!set_sys_clock_khz(khz, true)) {
    set_sys_clock_khz(133000, true);
    _freq_khz = 133000;
    return;
  }
  
  if (khz < _freq_khz) vreg_set_voltage(vr);
  _freq_khz = khz;
}

// ============================================================================
// INTERNAL - Thermal
// ============================================================================

void PicomimiGovernorClass::_thermal() {
  adc_select_input(4);
  uint16_t raw = adc_read();
  _temp = 27.0f - (raw * (3.3f / 4096.0f) - 0.706f) / 0.001721f;
  
  if (_temp >= THERMAL_CRITICAL) {
    _throttled = true;
    if (_profile > PROFILE_POWERSAVE) _apply(PROFILE_POWERSAVE);
  } else if (_temp >= THERMAL_THROTTLE && !_throttled) {
    _throttled = true;
    if (_profile > PROFILE_BALANCED) _apply(PROFILE_BALANCED);
  } else if (_temp < THERMAL_RELEASE) {
    _throttled = false;
  }
}

// ============================================================================
// INTERNAL - Timeouts
// ============================================================================

void PicomimiGovernorClass::_timeouts() {
  uint32_t now = to_ms_since_boot(get_absolute_time());
  
  if (_turbo_on && (now - _turbo_start_ms >= TURBO_MAX_MS)) {
    _turbo_on = false;
    if (_profile == PROFILE_TURBO) _apply(PROFILE_PERFORMANCE);
  }
  
  if (_boost_on && (now - _boost_start_ms >= BOOST_DURATION_MS)) _boost_on = false;
  
  if (_override_on && _override_end_ms > 0 && now >= _override_end_ms) {
    _override_on = false;
    _override_end_ms = 0;
    if (_manual) Serial.println(F("[GOV] Override expired"));
  }
}

// ============================================================================
// INTERNAL - WFI & Voltage
// ============================================================================

void PicomimiGovernorClass::_wfi() { __wfi(); }

vreg_voltage PicomimiGovernorClass::_toVreg(uint32_t mv) {
  if (mv <= 850) return VREG_VOLTAGE_0_85;
  if (mv <= 900) return VREG_VOLTAGE_0_90;
  if (mv <= 950) return VREG_VOLTAGE_0_95;
  if (mv <= 1000) return VREG_VOLTAGE_1_00;
  if (mv <= 1050) return VREG_VOLTAGE_1_05;
  if (mv <= 1100) return VREG_VOLTAGE_1_10;
  if (mv <= 1150) return VREG_VOLTAGE_1_15;
  if (mv <= 1200) return VREG_VOLTAGE_1_20;
  if (mv <= 1250) return VREG_VOLTAGE_1_25;
  return VREG_VOLTAGE_1_30;
}

// ============================================================================
// MANUAL MODE
// ============================================================================

void PicomimiGovernorClass::_handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      _cmd.trim();
      _cmd.toLowerCase();
      if (_cmd.length() > 0) {
        if (_cmd == "gov" || _cmd == "help" || _cmd == "?") _printHelp();
        else if (_cmd == "status" || _cmd == "s") _printStatus();
        else if (_cmd == "auto" || _cmd == "a") { setAuto(); Serial.println(F("[GOV] Auto")); }
        else if (_cmd.startsWith("turbo")) {
          uint32_t sec = 30;
          int sp = _cmd.indexOf(' ');
          if (sp > 0) sec = _cmd.substring(sp + 1).toInt();
          setTurbo(sec > 3600 ? 3600 : sec);
          Serial.print(F("[GOV] TURBO ")); Serial.print(sec); Serial.println(F("s"));
        }
        else if (_cmd.startsWith("save") || _cmd.startsWith("power")) {
          uint32_t sec = 60;
          int sp = _cmd.indexOf(' ');
          if (sp > 0) sec = _cmd.substring(sp + 1).toInt();
          setPowersave(sec);
          Serial.print(F("[GOV] POWERSAVE ")); Serial.print(sec); Serial.println(F("s"));
        }
        else if (_cmd.startsWith("bal")) {
          uint32_t sec = 0;
          int sp = _cmd.indexOf(' ');
          if (sp > 0) sec = _cmd.substring(sp + 1).toInt();
          setProfile(PROFILE_BALANCED, sec);
          Serial.println(F("[GOV] BALANCED"));
        }
        else if (_cmd.startsWith("perf")) {
          uint32_t sec = 0;
          int sp = _cmd.indexOf(' ');
          if (sp > 0) sec = _cmd.substring(sp + 1).toInt();
          setProfile(PROFILE_PERFORMANCE, sec);
          Serial.println(F("[GOV] PERFORMANCE"));
        }
        else if (_cmd.startsWith("ultra") || _cmd.startsWith("low")) {
          setProfile(PROFILE_ULTRA_LOW, 0);
          Serial.println(F("[GOV] ULTRA_LOW"));
        }
        else Serial.println(F("[GOV] Unknown. Type 'gov'"));
      }
      _cmd = "";
    } else _cmd += c;
  }
}

void PicomimiGovernorClass::_printHelp() {
  Serial.println(F("\n╔══════════════════════════════════════════╗"));
  Serial.println(F("║  PICOMIMI CPU GOVERNOR                   ║"));
  Serial.println(F("╚══════════════════════════════════════════╝\n"));
  Serial.println(F("Commands:"));
  Serial.println(F("  status      Show status"));
  Serial.println(F("  auto        Auto scaling"));
  Serial.println(F("  turbo [s]   Turbo for N sec"));
  Serial.println(F("  save [s]    Powersave for N sec"));
  Serial.println(F("  balanced    Balanced mode"));
  Serial.println(F("  perf        Performance mode"));
  Serial.println(F("  ultra       Ultra-low power\n"));
  Serial.println(F("Tip: Use PicomimiGov.idle(ms) instead of delay()"));
  Serial.println(F("     for accurate load tracking.\n"));
}

void PicomimiGovernorClass::_printStatus() {
  Serial.println(F("\n─── Governor Status ───"));
  Serial.print(F("Profile:  ")); Serial.print(PROFILE_NAMES[_profile]);
  Serial.print(F(" @ ")); Serial.print(_freq_khz / 1000); Serial.println(F(" MHz"));
  Serial.print(F("Load:     ")); Serial.print(_avg_load, 1);
  Serial.print(F("% (inst: ")); Serial.print(_instant_load, 1); Serial.println(F("%)"));
  Serial.print(F("Temp:     ")); Serial.print(_temp, 1); Serial.println(F(" C"));
  Serial.print(F("Chip:     ")); Serial.println(_chip == PICOMIMI_RP2350 ? "RP2350" : "RP2040");
  Serial.print(F("Mode:     "));
  if (_override_on) {
    Serial.print(F("MANUAL"));
    if (_override_end_ms > 0) {
      Serial.print(F(" (")); 
      Serial.print((_override_end_ms - to_ms_since_boot(get_absolute_time())) / 1000);
      Serial.print(F("s)"));
    }
  } else Serial.print(F("AUTO"));
  Serial.println();
  if (_turbo_on) Serial.println(F("          TURBO ACTIVE"));
  if (_throttled) Serial.println(F("          THERMAL THROTTLED"));
  Serial.println();
}
