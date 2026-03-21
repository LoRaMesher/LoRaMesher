# LoRaMesher

[![CI](https://github.com/LoRaMesher/LoRaMesher/actions/workflows/test.yml/badge.svg)](https://github.com/LoRaMesher/LoRaMesher/actions/workflows/test.yml)
[![Format](https://github.com/LoRaMesher/LoRaMesher/actions/workflows/format-check.yml/badge.svg)](https://github.com/LoRaMesher/LoRaMesher/actions/workflows/format-check.yml)
[![Coverage](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/jaimi5/b11f5ac09eb514904c15bbc9db6004b7/raw/loramesher_coverage.json)](https://github.com/LoRaMesher/LoRaMesher/actions/workflows/test.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![ESP32](https://img.shields.io/badge/Platform-ESP32-green.svg)](https://www.espressif.com/en/products/socs/esp32)
[![Native](https://img.shields.io/badge/Platform-Native%20(Linux%2FmacOS)-blue.svg)](#testing--analysis)
[![RadioLib](https://img.shields.io/badge/RadioLib-7.x-orange.svg)](https://github.com/jgromes/RadioLib)

A C++20 mesh networking library for LoRa nodes, built on a TDMA-based distance-vector routing protocol. Uses [RadioLib](https://github.com/jgromes/RadioLib) for radio communication and FreeRTOS for task scheduling.

---

## Table of Contents

- [Features](#features)
- [Quick Start](#quick-start)
- [API Usage](#api-usage)
  - [Initialization](#initialization)
  - [Receiving Packets](#receiving-packets)
  - [Sending Packets](#sending-packets)
  - [Timing-Aware Sending (TDMA)](#timing-aware-sending-tdma)
- [Configuration](#configuration)
- [Testing & Analysis](#testing--analysis)
  - [PlatformIO (recommended)](#platformio-recommended)
  - [CMake](#cmake)
  - [ASAN / UBSAN](#addresssanitizer--undefinedbehaviorsanitizer)
  - [TSAN](#threadsanitizer)
  - [Coverage](#profiling--code-coverage-llvm)
  - [XRay Profiling](#function-profiling-llvm-xray)
  - [Static Analysis](#static-analysis-clang-tidy)
- [Protocol Design](#protocol-design)
- [Citation](#citation)
- [License](#license)

---

## Features

- **Mesh routing** — distance-vector protocol with automatic route discovery and maintenance
- **TDMA superframe** — deterministic slot scheduling; nodes sleep when not transmitting
- **Auto join / network formation** — nodes discover nearby networks or create a new one
- **Network manager election** — distributed NM election with configurable priority
- **Multi-module support** — SX1262, SX1268, SX1276, SX1278, SX1280 via RadioLib
- **Desktop simulation** — full native test environment with hardware mocks, no hardware required
- **Memory-safe** — tested under ASAN, UBSAN, and TSAN; no heap allocations per packet on ESP32
- **LLVM coverage** — instrumented test environment with `llvm-cov` HTML/text reports

---

## Quick Start

1. Install [Visual Studio Code](https://code.visualstudio.com/) and the [PlatformIO extension](https://platformio.org/install/ide?install=vscode).
2. Clone this repository.
3. Open PlatformIO Home → **Projects** → **Add Existing** → select `examples/counter-example`.
4. Build with PlatformIO (⌘/Ctrl+Alt+B).
5. Upload to your LoRa board (tested on TTGO T-Beam v1.1).

> See `examples/` for additional examples including a queued-receive and a ping-pong mode.

---

## API Usage

### Initialization

```cpp
#include "loramesher.h"

// Build configuration
LoraMesherConfig config;
config.loraCS  = 18;   // TTGO T-Beam v1.1 pins
config.loraRst = 23;
config.loraIrq = 26;
config.loraIo1 = 33;
config.module  = LoraMesher::LoraModules::SX1276_MOD;

// Start the mesh
LoraMesher& mesh = LoraMesher::getInstance();
mesh.begin(config);
mesh.start();

// Pause / resume at any time
mesh.standby();
mesh.start();
```

> *Be aware of the radio frequency regulations in your region.*

---

### Receiving Packets

Register a FreeRTOS task that blocks until the library signals a new packet:

```cpp
struct SensorData {
    uint32_t counter = 0;
};

void onReceive(void*) {
    for (;;) {
        ulTaskNotifyTake(pdPASS, portMAX_DELAY); // block until packet arrives

        while (mesh.getReceivedQueueSize() > 0) {
            AppPacket<SensorData>* pkt = mesh.getNextAppPacket<SensorData>();

            SensorData* data = pkt->payload;
            size_t count = pkt->getPayloadLength(); // number of SensorData elements
            for (size_t i = 0; i < count; i++) {
                Serial.printf("Counter: %u\n", data[i].counter);
            }

            mesh.deletePacket(pkt); // always free the packet
        }
    }
}

// Register the task
TaskHandle_t rxTask = nullptr;
xTaskCreate(onReceive, "RX", 4096, nullptr, 2, &rxTask);
mesh.setReceiveAppDataTaskHandle(rxTask);
```

**AppPacket fields:**

| Field | Type | Description |
|---|---|---|
| `src` | `uint16_t` | Source node address |
| `dst` | `uint16_t` | Destination address (local or `kBroadcastAddress`) |
| `payloadSize` | `uint32_t` | Payload size in bytes |
| `payload` | `T[]` | Typed payload array |

---

### Sending Packets

```cpp
SensorData reading { .counter = 42 };

// Unicast
mesh.createPacketAndSend(destinationAddress, &reading, /*count=*/1);

// Broadcast
mesh.createPacketAndSend(kBroadcastAddress, &reading, 1);

// Reliable (acknowledged, larger payloads)
mesh.sendReliable(destinationAddress, &reading, 1);
```

---

### Timing-Aware Sending (TDMA)

LoRaMesher is TDMA-based — each node has assigned TX slots within each superframe. Sending just before your slot avoids waiting an extra superframe:

```cpp
// Wait until just before the next TX slot (default 200 ms guard time)
uint32_t wait_ms = mesh.GetTimeUntilNextDataSlot();
vTaskDelay(pdMS_TO_TICKS(wait_ms));
mesh.createPacketAndSend(dst, &reading, 1);
```

Custom guard time:

```cpp
uint32_t wait_ms = mesh.GetTimeUntilNextDataSlot(/*guard_time_ms=*/100);
```

Multiple TX slots per superframe:

```cpp
uint8_t slots = mesh.GetDataSlotsPerSuperframe();
for (uint8_t i = 0; i < slots; i++) {
    vTaskDelay(pdMS_TO_TICKS(mesh.GetTimeUntilNextDataSlot()));
    mesh.createPacketAndSend(dst, &readings[i], 1);
}
```

Both functions return `0` when the node has not yet joined a network.

---

## Configuration

Add these defines before including `loramesher.h` (or in `platformio.ini` as `build_flags`):

```cpp
// Log level: 0=DEBUG  1=INFO  2=WARNING  3=ERROR  4=NO_LOG (default)
#define LORAMESHER_LOG_LEVEL 1

// Enable extra internal debug logs (task monitoring, state transitions)
// #define DEBUG

// Disable ANSI color codes in log output (useful when saving to flash)
// #define LOGGER_DISABLE_COLORS

// Adjust the log message buffer size (default: 128)
#define LOGGER_BUFFER_SIZE 256
```

---

## Testing & Analysis

### PlatformIO (recommended)

All tests run on the host — no hardware required.

```bash
# Run all tests (ASAN + UBSAN enabled)
pio test -e test_native -v

# Filter to a specific suite
pio test -e test_native -v -f "protocols/lora_mesh/services/test_routing"

# List available test names
pio test -e test_native --list-tests
```

> Run as a background task — integration tests take several minutes.

### CMake

```bash
mkdir build && cd build
cmake .. -DBUILD_DESKTOP=ON
cmake --build . --target loramesher_lib   # library only
cmake --build . --target build_all_tests  # build tests
cmake --build . --target run_all_tests    # build + run
ctest                                     # alternative runner
```

---

### AddressSanitizer + UndefinedBehaviorSanitizer

The `test_native` environment enables ASAN and UBSAN automatically.

```bash
pio test -e test_native -v
```

What to look for:
- `ERROR: AddressSanitizer:` — heap/stack corruption, use-after-free
- `runtime error:` — undefined behavior (signed overflow, null deref, etc.)
- `ERROR: LeakSanitizer:` — unexpected memory leaks (intentional ones are suppressed)

---

### ThreadSanitizer

TSAN is mutually exclusive with ASAN; use the dedicated environment:

```bash
pio test -e test_native_tsan -v
```

What to look for: `WARNING: ThreadSanitizer: data race`

---

### Profiling / Code Coverage (LLVM)

The `test_native_profile` environment compiles with `-fprofile-instr-generate -fcoverage-mapping`. A convenience script is provided:

```bash
bash scripts/run_coverage.sh
# Opens coverage/index.html when done
```

Or run steps manually:

#### 1. Run instrumented tests

```bash
LLVM_PROFILE_FILE="$(pwd)/.pio/coverage/%e-%p.profraw" \
    pio test -e test_native_profile -v
```

#### 2. Merge raw profiles

```bash
llvm-profdata-18 merge -sparse .pio/coverage/*.profraw \
    -o .pio/coverage/loramesher.profdata
```

#### 3. Text summary

```bash
llvm-cov-18 report .pio/build/test_native_profile/program \
    -instr-profile=.pio/coverage/loramesher.profdata \
    --ignore-filename-regex='(test/|googletest|\.pio)'
```

#### 4. HTML report

```bash
llvm-cov-18 show .pio/build/test_native_profile/program \
    -instr-profile=.pio/coverage/loramesher.profdata \
    -format=html -output-dir=coverage/ \
    --ignore-filename-regex='(test/|googletest|\.pio)'
# Open coverage/index.html
```

> Coverage % is also visible in the **GitHub Actions run summary** for every CI push.

---

### Function Profiling (LLVM XRay)

The `test_native_xray` environment compiles with LLVM XRay instrumentation to produce per-function timing reports (call counts, median/min/max latency).

#### 1. Run instrumented tests

```bash
XRAY_OPTIONS="patch_premain=true xray_mode=xray-basic verbosity=1" \
    pio test -e test_native_xray -v -f "protocols/lora_mesh/services/test_routing_unit"
```

#### 2. Analyze results

```bash
# Top 20 slowest functions by median time
llvm-xray-18 account xray-log.* -sort=med -sortorder=dsc -top=20 \
    -instr_map=.pio/build/test_native_xray/program

# Top 20 by total cumulative time
llvm-xray-18 account xray-log.* -sort=sum -sortorder=dsc -top=20 \
    -instr_map=.pio/build/test_native_xray/program

# Library functions only (exclude tests, STL, googletest)
llvm-xray-18 account xray-log.* -sort=med -sortorder=dsc --format=csv \
    -instr_map=.pio/build/test_native_xray/program \
    | sed -n '1p; /"loramesher::/p' | grep -v 'loramesher::test::' | head -20
```

> XRay log files (`xray-log.*`) are written to the project root by default.
> If too many arguments is shown, multiple `xray-log.*` are in the project root.

---

### Static Analysis (clang-tidy)

```bash
mkdir -p build && cd build
cmake .. -DBUILD_DESKTOP=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cd ..

run-clang-tidy -p build/ src/          # all sources
clang-tidy -p build/ src/protocols/lora_mesh/services/network_service.cpp  # single file
```

Checks enabled: `clang-analyzer-*`, `bugprone-*`, `cppcoreguidelines-owning-memory`, `concurrency-mt-unsafe`, and others (see `.clang-tidy`).

---

## Protocol Design

### State Machine

| State | Description |
|---|---|
| **Initialization** | System startup and configuration |
| **Discovery** | Listen for a full superframe; join if a network is heard, else create one |
| **Joining** | Send routing table during the discovery slot |
| **Normal Operation** | TDMA TX/RX/sleep based on assigned slots |
| **Network Manager** | Distribute routing updates and link quality; set next superframe schedule |
| **Fault Recovery** | Re-join or re-create network after connection loss |

### Superframe Structure

- **Control slots** — sync beacon, routing updates, slot allocation (all nodes listen)
- **Data slots** — ordered TX/RX per routing table; sleeping nodes stay off-air
- **Discovery slots** — CSMA/CA slots for new nodes to announce themselves

### Packet Types

`SYNC_BEACON` · `ROUTING_TABLE` · `NM_CLAIM` · `JOIN_REQUEST` · `JOIN_RESPONSE` · `SLOT_ALLOCATION` · `DATA` · `KEEP_ALIVE` · `FAULT_RECOVERY`

---

## Citation

If you use LoRaMesher in academic work, please cite:

```bibtex
@ARTICLE{9930341,
  author={Solé, Joan Miquel and Centelles, Roger Pueyo and Freitag, Felix and Meseguer, Roc},
  journal={IEEE Access},
  title={Implementation of a LoRa Mesh Library},
  year={2022},
  volume={10},
  pages={113158-113171},
  doi={10.1109/ACCESS.2022.3217215}
}
```

Open access paper: [IEEE Access — Implementation of a LoRa Mesh Library](https://ieeexplore.ieee.org/document/9930341)

---

## License

MIT — see [LICENSE](LICENSE).
