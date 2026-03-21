# Battery-Optimized LoRaMesher Example

This example extends the simple example with **power management** for TTGO T-Beam boards. It demonstrates how to use LoraMesher's sleep/wake callbacks to minimize power consumption.

# ⚠️ WARNING ⚠️

``` 
⚠️ As said by XPowerLib: Please do not run the example without knowing the external load voltage of the PMU, it may burn your external load, please check the voltage setting before running the example, if there is any loss, please bear it by yourself ⚠️
``` 

## What This Example Adds

Compared to `simple_example`, this example includes:

- **Automatic PMU detection** - Supports both AXP192 (T-Beam v1.0/v1.1) and AXP2101 (T-Beam v1.2+)
- **Sleep callbacks** - Disable peripherals before sleep
- **Wake callbacks** - Re-initialize hardware after wake
- **Battery monitoring** - Enable voltage measurement via PMU

## T-Beam Power Architecture

```
                    ┌─────────────┐
                    │   Battery   │
                    └──────┬──────┘
                           │
                    ┌──────▼──────┐
                    │  PMU Chip   │
                    │ AXP192/2101 │
                    └──────┬──────┘
                           │
         ┌─────────────────┼─────────────────┐
         │                 │                 │
    ┌────▼────┐      ┌─────▼─────┐     ┌─────▼─────┐
    │  ESP32  │      │   LoRa    │     │    GPS    │
    │ (DCDC1) │      │(LDO2/ALDO2)│    │(LDO3/ALDO3)│
    └─────────┘      └───────────┘     └───────────┘
```

### Power Channel Mapping

| Function | AXP192 (v1.0/v1.1) | AXP2101 (v1.2+) |
|----------|-------------------|-----------------|
| ESP32    | DCDC3 (protected) | DCDC1 (protected) |
| LoRa     | LDO2 @ 3.3V      | ALDO2 @ 3.3V    |
| GPS      | LDO3 @ 3.3V      | ALDO3 @ 3.3V    |
| OLED     | DCDC1 @ 3.3V     | —               |

## Sleep/Wake Callbacks

LoraMesher calls your callbacks when entering and exiting sleep:

```cpp
SleepResult OnSleep(const SleepContext& ctx) {
    if (!InitDevices::prepareSleep()) {
        Serial.println("Error: Failed to prepare sleep");
        return power::SleepResult{false};  // veto: peripheral state unknown
    }
    // After returning true, the protocol puts the radio and MCU to sleep.
    // OnWakeUp will be called before the next active slot.
    return power::SleepResult{true};
}

void OnWakeUp(PowerState previous_state) {
    InitDevices::init();  // Re-enable power to peripherals
}
```

Register callbacks when building LoraMesher:

```cpp
mesher = LoraMesher::Builder()
             .withPrepareSleepCallback(OnSleep)
             .withWakeUpCallback(OnWakeUp)
             // ... other config
             .Build();
```

## Battery Monitoring

Access battery information through the PMU (defined in `initDevices.cpp`):

```cpp
extern XPowersLibInterface* PMU;

// Battery voltage in millivolts
uint16_t voltage = PMU->getBattVoltage();

// Charging status
bool charging = PMU->isCharging();

// Battery percentage (if supported)
uint8_t percent = PMU->getBatteryPercent();
```

## Files

| File | Description |
|------|-------------|
| `main.cpp` | LoraMesher setup with power callbacks |
| `initDevices.h` | PMU interface declaration |
| `initDevices.cpp` | PMU detection and power channel configuration |

## Building

```bash
pio run -e ttgo-t-beam
pio run -e ttgo-t-beam -t upload
pio device monitor
```

## Expected Output

```
PMU: AXP2101 initialized
Routes: 2
  0x1A2B via 0x1A2B (1 hops)
  0x3C4D via 0x1A2B (2 hops)
Network: nodes=2, manager=0x3ADF, sync=yes
Sent to 0x1A2B
```

## Power Consumption Tips

1. **Reduce TX power** - Use minimum power needed for your range
2. **Increase hello interval** - Longer intervals reduce radio activity
3. **Disable GPS** - If not needed, call `PMU->disablePowerOutput(XPOWERS_LDO3)` or `ALDO3`
4. **MCU light sleep** - The protocol automatically puts the MCU into light sleep during SLEEP slots (ESP32 only). Use `OnSleep` to power down peripherals before sleep.
