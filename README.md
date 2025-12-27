# Pico-Governor
## 🎯 What is this?

**Pico Governor** is a lightweight Arduino library that automatically scales your RP2040/RP2350's CPU frequency based on workload. When your code is idle, the CPU slows down to save power. When you need performance, it ramps up instantly.

Perfect for:
- **Battery-powered projects** — smartwatches, portables, wearables
- **Heat-sensitive builds** — enclosed cases, compact designs
- **Always-on devices** — monitors, sensors, displays

Extracted from [**Picomimi**](https://github.com/abinaash/picomimi), a full microcontroller operating system for RP2040/RP2350. This library extracts just the CPU governor — no OS required, pure Arduino.

---

## ✨ Features

| Feature | Description |
|---------|-------------|
| **Auto Scaling** | CPU frequency adjusts automatically based on load |
| **5 Power Profiles** | Ultra-Low → Powersave → Balanced → Performance → Turbo |
| **Thermal Protection** | Automatic throttling when chip gets hot |
| **WFI Support** | Ultra-low-power Wait-For-Interrupt on RP2350 |
| **Input Boost** | Instant frequency jump on button press for snappy UI |
| **Silent Operation** | Zero serial output by default |
| **Manual Override** | Serial commands for debugging and tuning |

### Frequency Ranges

| Chip | Min | Max | Profiles |
|------|-----|-----|----------|
| **RP2040** | 50 MHz | 250 MHz | 5 levels + voltage scaling |
| **RP2350** | 50 MHz | 300 MHz | 5 levels + voltage scaling + WFI |

---

## 📦 Installation

### Arduino IDE

1. Download the latest `.zip` from [Releases](https://github.com/abinaash/pico-governor/releases)
2. Open Arduino IDE
3. Go to **Sketch → Include Library → Add .ZIP Library...**
4. Select the downloaded `.zip` file
5. Done!

### PlatformIO

```ini
lib_deps = 
    https://github.com/abinaash/pico-governor
```

---

## 🚀 Quick Start

```cpp
#include <PicomimiGovernor.h>

void setup() {
  // Specify your chip — no auto-detection, you know what you're using
  PicomimiGov.begin(PICOMIMI_RP2350);  // or PICOMIMI_RP2040
}

void loop() {
  PicomimiGov.run();  // Call every loop
  
  // Your code here...
  readSensors();
  updateDisplay();
  
  // Use idle() instead of delay() for accurate load tracking
  PicomimiGov.idle(10);  // 10ms idle time
}
```

**That's it.** The governor runs silently in the background, scaling CPU frequency based on your workload.

---

## How It Works

The governor tracks how much time your code spends doing actual work versus idling.

```
┌─────────────────────────────────────────────────────────┐
│                      Loop Cycle                         │
├──────────────────────┬──────────────────────────────────┤
│    Work Time         │         Idle Time                │
│   (your code)        │    (PicomimiGov.idle())          │
├──────────────────────┴──────────────────────────────────┤
│                                                         │
│   CPU Load = Work Time / Total Time × 100%              │
│                                                         │
│   High Load  →  Boost Frequency  →  Fast execution      │
│   Low Load   →  Drop Frequency   →  Save power          │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

### The Key: `idle()`

Use `PicomimiGov.idle(ms)` instead of `delay(ms)`:

```cpp
// ❌ Don't do this — governor can't track idle time accurately
delay(50);

// ✅ Do this — governor knows you're idle
PicomimiGov.idle(50);
```

### Load → Profile Mapping

| CPU Load | Profile | RP2040 Freq | RP2350 Freq |
|----------|---------|-------------|-------------|
| < 2% | Ultra-Low | 50 MHz | 50 MHz + WFI |
| 2-12% | Powersave | 100 MHz | 100 MHz |
| 12-30% | Balanced | 133 MHz | 150 MHz |
| 30-55% | Performance | 200 MHz | 250 MHz |
| > 55% | Turbo | 250 MHz | 300 MHz |

Thresholds have hysteresis to prevent rapid switching.

---

## 📖 API Reference

### Core Functions

```cpp
// Initialize — YOU MUST SPECIFY CHIP TYPE
PicomimiGov.begin(PICOMIMI_RP2350);       // For RP2350
PicomimiGov.begin(PICOMIMI_RP2040);       // For RP2040
PicomimiGov.begin(PICOMIMI_RP2350, true); // With manual mode (serial commands)

// Call every loop — handles everything
PicomimiGov.run();

// Mark idle time (use instead of delay)
PicomimiGov.idle(100);        // 100ms idle
PicomimiGov.idleMicros(500);  // 500µs idle

// Instant boost on user input (button press, touch, etc.)
PicomimiGov.inputBoost();
```

### Status Functions

```cpp
PicomimiGov.getFreqMHz();      // Current frequency in MHz
PicomimiGov.getCPULoad();      // Current load (0-100%)
PicomimiGov.getTemperature();  // Chip temperature in °C
PicomimiGov.getProfile();      // Current PowerProfile enum
PicomimiGov.getProfileName();  // Profile as string ("BALANCED", etc.)
PicomimiGov.isTurbo();         // Is turbo mode active?
PicomimiGov.isThrottled();     // Is thermal throttling active?
```

### Manual Control

```cpp
// Force a specific profile
PicomimiGov.setProfile(PROFILE_TURBO, 30);     // Turbo for 30 seconds
PicomimiGov.setProfile(PROFILE_POWERSAVE, 0);  // Powersave indefinitely

// Convenience functions
PicomimiGov.setTurbo(30);      // Turbo for 30 seconds (default: 30)
PicomimiGov.setPowersave(60);  // Powersave for 60 seconds (default: 60)

// Resume automatic scaling
PicomimiGov.setAuto();
```

### Power Profiles

```cpp
PROFILE_ULTRA_LOW   // 50 MHz  — minimum power, WFI on RP2350
PROFILE_POWERSAVE   // 100 MHz — light tasks
PROFILE_BALANCED    // 133/150 MHz — default
PROFILE_PERFORMANCE // 200/250 MHz — responsive
PROFILE_TURBO       // 250/300 MHz — maximum performance
```

---

## Manual Mode

Enable manual mode for serial debugging and control:

```cpp
void setup() {
  Serial.begin(115200);
  PicomimiGov.begin(PICOMIMI_RP2350, true);  // true = manual mode
}
```

Then open Serial Monitor and type commands:

```
╔══════════════════════════════════════════╗
║  PICOMIMI CPU GOVERNOR                   ║
╚══════════════════════════════════════════╝

Commands:
  status      Show current status
  auto        Resume auto scaling
  turbo 30    Turbo for 30 seconds
  save 3600   Powersave for 1 hour
  balanced    Balanced mode
  perf        Performance mode
  ultra       Ultra-low power
```

### Status Output

```
─── Governor Status ───
Profile:  BALANCED @ 150 MHz
Load:     12.3% (inst: 8.7%)
Temp:     38.2 C
Chip:     RP2350
Mode:     AUTO
```

---

## 🔋 Power Saving Tips

### 1. Use `idle()` liberally

```cpp
void loop() {
  PicomimiGov.run();
  
  if (needsUpdate()) {
    updateDisplay();  // Only when needed
  }
  
  PicomimiGov.idle(50);  // Idle between checks
}
```

### 2. Batch your work

```cpp
// ❌ Frequent small updates = higher average load
void loop() {
  PicomimiGov.run();
  readOneSensor();
  PicomimiGov.idle(10);
}

// ✅ Batch work, then idle longer = lower average load
void loop() {
  PicomimiGov.run();
  readAllSensors();
  processData();
  updateDisplay();
  PicomimiGov.idle(100);  // Longer idle, same refresh rate
}
```

### 3. Use input boost for responsiveness

```cpp
void onButtonPress() {
  PicomimiGov.inputBoost();  // Instant speed for UI
  showMenu();
}
```

---

## 🌡️ Thermal Protection

The governor monitors chip temperature and automatically throttles if needed:

| Temperature | Action |
|-------------|--------|
| < 60°C | Normal operation |
| 70°C | Throttle to Balanced max |
| 80°C | Emergency Powersave |
| < 60°C | Release throttle (hysteresis) |

No configuration needed — it just works.

---

## 🛠️ Technical Details

### Voltage Scaling

The governor adjusts core voltage alongside frequency for stability:

| Profile | RP2040 | RP2350 |
|---------|--------|--------|
| Ultra-Low | 0.95V | 0.95V |
| Powersave | 1.00V | 1.00V |
| Balanced | 1.05V | 1.05V |
| Performance | 1.10V | 1.10V |
| Turbo | 1.15V | 1.25V |

Voltage is raised *before* frequency increase, lowered *after* frequency decrease.

### WFI (Wait For Interrupt)

On RP2350 in Ultra-Low profile with < 2% load, the governor uses `__wfi()` to halt the CPU until the next interrupt. This is the lowest possible power state while remaining responsive.

---

## 🚧 Roadmap

This is a small library today, but has room to grow:

- [ ] Dual-core load balancing
- [ ] Sleep mode integration  
- [ ] Power consumption estimation
- [ ] Custom frequency tables
- [ ] Callback hooks for profile changes

---

## License

MIT License — do whatever you want.

---

## Credits

Extracted from [**Picomimi**](https://github.com/MilkmanAbi/Picomimi) — a complete microcontroller operating system for RP2040/RP2350 featuring kernel, scheduler, memory management, filesystem, GUI, and more.

---
