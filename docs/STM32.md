# Running LoRaMesher on STM32

LoRaMesher targets ESP32 by default but also builds for STM32 boards through the
[STM32duino](https://github.com/stm32duino/Arduino_Core_STM32) Arduino core. The
mesh protocol, routing, and RadioLib drivers are platform-neutral; only the OS
layer and a couple of HAL details are platform-specific, and those are handled
automatically at compile time.

## Requirements

| Requirement | Why |
|---|---|
| **STM32duino core** (Arduino core for STM32) | Provides `Arduino.h`, SPI, and the toolchain |
| **STM32FreeRTOS** library | STM32 cores do not bundle FreeRTOS (ESP32 does); LoRaMesher needs it |
| **RadioLib** ≥ 7.1.2 | Radio driver |
| **C++20** (`-std=gnu++20`) | The library requires C++20 |
| **Exceptions enabled** (`-fexceptions`) | `Builder::Build()` and config validation throw `std::invalid_argument` |

> A board with enough flash/RAM for FreeRTOS + RadioLib + the routing tables is
> required. Development/testing is done against a Nucleo-F411RE (512 KB flash,
> 128 KB RAM); smaller parts may not fit.

## Building with PlatformIO (recommended)

A ready-made environment is provided in `platformio.ini`:

```ini
[env:stm32]
platform = ststm32
board = nucleo_f411re
framework = arduino
lib_deps =
    ${env.lib_deps}
    stm32duino/STM32duino FreeRTOS
build_unflags = -std=gnu++17
build_flags =
    -std=gnu++20
    -fexceptions
```

```bash
pio run -e stm32
```

Change `board` to your target. The `build_unflags`/`build_flags` lines are what
select C++20 and re-enable exceptions (STM32duino defaults to `gnu++17` with
`-fno-exceptions`).

## Building with the Arduino IDE

The Arduino IDE has **no per-library way to set compiler flags**, and STM32duino
defaults to `-std=gnu++17 -fno-exceptions`. To build LoRaMesher you must raise
the standard and enable exceptions at the core level:

1. Install the **STM32** core via Boards Manager, and the **STM32duino
   FreeRTOS** and **RadioLib** libraries via Library Manager.
2. Create a `platform.local.txt` next to the STM32 core's `platform.txt`
   (e.g. `<sketchbook>/hardware` or the installed core folder under
   `Arduino15/packages/STMicroelectronics/hardware/stm32/<ver>/`) containing:
   ```
   compiler.cpp.std=gnu++20
   compiler.cpp.extra_flags=-fexceptions
   ```
3. Restart the IDE, select your STM32 board, open
   **File → Examples → LoRaMesher → SimpleMesh**, set the LoRa pins for your
   wiring, and compile.

## Platform notes

- **Node address** — auto-addressing reads the STM32 96-bit unique device ID
  (`UID_BASE`) via the HAL, so hardware-derived addresses are stable across
  reboots (same as the ESP32 eFuse-MAC path).
- **SPI pins** — set custom SPI pins through `PinConfig`
  (`cs, rst, dio0, dio1, sck, miso, mosi`); on STM32 they are applied with
  `setSCLK/setMISO/setMOSI` before `SPI.begin()`.
- **Light sleep** — `LightSleep()` falls back to a blocking `delay()` on STM32
  (no low-power entry yet); ESP32 uses `esp_light_sleep_start()`.
