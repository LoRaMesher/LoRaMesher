<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/logo-dark.png">
    <source media="(prefers-color-scheme: light)" srcset="docs/assets/logo-light.png">
    <img src="docs/assets/logo-light.png" alt="LoRaMesher logo" width="180" />
  </picture>
</p>

<h1 align="center">LoRaMesher</h1>

[![CI](https://github.com/LoRaMesher/LoRaMesher/actions/workflows/test.yml/badge.svg)](https://github.com/LoRaMesher/LoRaMesher/actions/workflows/test.yml)
[![Format](https://github.com/LoRaMesher/LoRaMesher/actions/workflows/format-check.yml/badge.svg)](https://github.com/LoRaMesher/LoRaMesher/actions/workflows/format-check.yml)
[![Coverage](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/jaimi5/b11f5ac09eb514904c15bbc9db6004b7/raw/loramesher_coverage.json)](https://github.com/LoRaMesher/LoRaMesher/actions/workflows/test.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![ESP32](https://img.shields.io/badge/Platform-ESP32-green.svg)](https://www.espressif.com/en/products/socs/esp32)
[![Native](https://img.shields.io/badge/Platform-Native%20(Linux%2FmacOS)-blue.svg)](#testing--analysis)
[![RadioLib](https://img.shields.io/badge/RadioLib-7.x-orange.svg)](https://github.com/jgromes/RadioLib)
[![Discord](https://img.shields.io/badge/Discord-Join-5865F2?logo=discord&logoColor=white)](https://discord.gg/mWmKpuQx)

A C++20 mesh networking library for LoRa nodes, built on a TDMA-based distance-vector routing protocol. Uses [RadioLib](https://github.com/jgromes/RadioLib) for radio communication and FreeRTOS for task scheduling.

> **Coming from 0.0.x?** The 1.0.0 release is a full rewrite — see [MIGRATION.md](MIGRATION.md) for the porting guide. Per-version changes live in [CHANGELOG.md](CHANGELOG.md); the wire protocol is documented in [PROTOCOL_SPEC.md](PROTOCOL_SPEC.md).

---

## Table of Contents

- [Features](#features)
- [Quick Start](#quick-start)
- [API Usage](#api-usage)
  - [Initialization](#initialization)
  - [Receiving Packets](#receiving-packets)
  - [Sending Packets](#sending-packets)
  - [Timing-Aware Sending (TDMA)](#timing-aware-sending-tdma)
  - [Diagnostics & Advanced](#diagnostics--advanced)
  - [Deployment Tips](#deployment-tips)
- [Configuration](#configuration)
- [Testing & Analysis](#testing--analysis)
  - [PlatformIO (recommended)](#platformio-recommended)
  - [CMake](#cmake)
  - [ASAN / UBSAN](#addresssanitizer--undefinedbehaviorsanitizer)
  - [TSAN](#threadsanitizer)
  - [Coverage](#profiling--code-coverage-llvm)
  - [XRay Profiling](#function-profiling-llvm-xray)
  - [Static Analysis](#static-analysis-clang-tidy)
- [Contributing](#contributing)
- [Protocol Design](#protocol-design)
- [Citation](#citation)
- [License](#license)

---

## Features

- **Mesh routing** — distance-vector protocol with automatic route discovery and maintenance
- **TDMA superframe** — deterministic slot scheduling; nodes sleep when not transmitting
- **Auto join / network formation** — nodes discover nearby networks or create a new one
- **Network manager election** — distributed NM election with configurable priority
- **Runtime role changes** — promote a node to `NETWORK_MANAGER` or demote it back to `NODE_ONLY` at runtime via `SetNodeRole()`
- **Capability-aware discovery** — find the closest gateway or any node matching a capability bitmap (`GetClosestGateway`, `GetClosestNodeByCapability`)
- **Multi-module support** — SX1262, SX1268, SX1276, SX1278, SX1280 via RadioLib
- **Desktop simulation** — full native test environment with hardware mocks, no hardware required
- **Broadcast messaging** — TTL-based flooding with per-node de-duplication
- **Memory-safe** — tested under ASAN, UBSAN, and TSAN; no heap allocations per packet on ESP32
- **LLVM coverage** — instrumented test environment with `llvm-cov` HTML/text reports

---

## Quick Start

1. Install [Visual Studio Code](https://code.visualstudio.com/) and the [PlatformIO extension](https://platformio.org/install/ide?install=vscode).
2. Clone this repository.
3. Open PlatformIO Home → **Projects** → **Add Existing** → pick the example below that matches your use case.
4. Build with PlatformIO (⌘/Ctrl+Alt+B).
5. Upload to your LoRa board (tested on TTGO T-Beam v1.1).

| Example | Use when |
|---|---|
| `examples/simple_example` | First time with the library — minimal Builder + callback flow |
| `examples/queued_receive_example` | RX should be handled in a separate FreeRTOS task instead of inside the callback |
| `examples/battery_optimized_example` | Battery-powered nodes that sleep between TDMA slots |

> **STM32:** the library also builds for STM32 boards (via the STM32duino core +
> STM32FreeRTOS). Use the `stm32` PlatformIO environment (`pio run -e stm32`) —
> see [docs/STM32.md](docs/STM32.md) for requirements and Arduino-IDE setup.

---

## API Usage

### Initialization

LoRaMesher is configured via three config objects assembled with a fluent `Builder`:

```cpp
#include "loramesher.hpp"
using namespace loramesher;

// 1. Hardware pins (TTGO T-Beam v1.x — see presets below for other boards)
PinConfig pins(/*cs=*/18, /*rst=*/23, /*dio0=*/26, /*dio1=*/33);

// 2. Radio parameters: type, frequency (MHz), SF, BW (kHz), CR, power (dBm),
//    sync word, CRC, preamble length
RadioConfig radio(RadioType::kSx1276, 869.9F, /*sf=*/7, /*bw=*/125.0,
                  /*cr=*/7, /*power=*/6, /*sync=*/20, /*crc=*/true,
                  /*preamble=*/8);

// 3. Mesh protocol — defaults are sensible; override per-field if needed
LoRaMeshProtocolConfig protocol;

// 4. Build and start
std::unique_ptr<LoraMesher> mesher = LoraMesher::Builder()
    .withPinConfig(pins)
    .withRadioConfig(radio)
    .withLoRaMeshProtocol(protocol)
    .Build();

Result r = mesher->Start();
if (!r) {
    // r.GetErrorMessage() describes the failure
}
```

`Start()` is `[[nodiscard]] Result`; always check it. `Stop()` halts all tasks and releases resources — there is no `resume`, rebuild a new instance to restart.

**Common board presets:**

| Board | `PinConfig` | Radio | Notes |
|---|---|---|---|
| TTGO T-Beam v1.x | `(18, 23, 26, 33)` | `kSx1276` | |
| TTGO LoRa32 v1 | `(18, 14, 26, 33)` | `kSx1278` | |
| LILYGO T3 S3 v1.x | `(7, 8, 9, 33)` | `kSx1278` | |
| Heltec WiFi LoRa | `(18, 14, 26, 35)` | `kSx1276` | |
| Heltec WiFi LoRa V3 | `(8, 12, 14, 13, 9, 11, 10)` | `kSx1262` | Add `radio.setTcxoVoltage(1.8F);` |

> *Be aware of the radio frequency regulations in your region.*

---

### Receiving Packets

Register a callback that fires when data arrives:

```cpp
void OnDataReceived(AddressType source, const std::vector<uint8_t>& data) {
    // source = sender address
    // data   = raw payload bytes
}

mesher->SetDataCallback(OnDataReceived);
```

> ⚠ The callback runs in the protocol context. Keep the body small or hand the payload off to your own task — long-running work here will stall protocol processing.

For a ready-made pattern that pushes incoming payloads onto a queue and drains them from a dedicated FreeRTOS task, see `examples/queued_receive_example/`.

---

### Sending Packets

```cpp
std::vector<uint8_t> payload = {0x01, 0x02, 0x03};

// Unicast — routed via the mesh routing table
Result r = mesher->Send(destinationAddress, payload);
if (!r) { /* r.GetErrorMessage() */ }

// Broadcast — reaches all nodes via TTL-based flooding
Result rb = mesher->SendBroadcast(payload);
if (!rb) { /* ... */ }
```

Both `Send` and `SendBroadcast` are `[[nodiscard]]` — always check the returned `Result`. Common failure modes (node not yet synchronized, no TX slot allocated, unknown destination) come back as distinct error codes; use [`IsReadyToSend()`](#diagnostics--advanced) to probe before sending.

---

### Timing-Aware Sending (TDMA)

LoRaMesher is TDMA-based — each node has assigned TX slots within each superframe. Sending just before your slot avoids waiting an extra superframe:

```cpp
// Wait until just before the next TX slot (default 200 ms guard time)
uint32_t wait_ms = mesher->GetTimeUntilNextDataSlot();
vTaskDelay(pdMS_TO_TICKS(wait_ms));
Result r = mesher->Send(dst, payload);
if (!r) { /* r.GetErrorMessage() */ }
```

Custom guard time:

```cpp
uint32_t wait_ms = mesher->GetTimeUntilNextDataSlot(/*guard_time_ms=*/100);
```

Multiple TX slots per superframe:

```cpp
uint8_t slots = mesher->GetDataSlotsPerSuperframe();
for (uint8_t i = 0; i < slots; i++) {
    vTaskDelay(pdMS_TO_TICKS(mesher->GetTimeUntilNextDataSlot()));
    Result r = mesher->Send(dst, payloads[i]);
    if (!r) { /* handle error */ }
}
```

Both functions return `0` when the node has not yet joined a network — pair them with `IsReadyToSend()` (see [Diagnostics & Advanced](#diagnostics--advanced)) for a robust send loop.

---

### Diagnostics & Advanced

The methods below are public on `LoraMesher` and useful once the basic flow is working.

**Node identity**

```cpp
AddressType me = mesher->GetNodeAddress();

// Available before Build() — useful for role-based wiring driven by address
AddressType derived = LoraMesher::GenerateAddressFromHardware();
```

**Routing table & network status**

```cpp
std::vector<RouteEntry> routes = mesher->GetRoutingTable();
NetworkStatus status = mesher->GetNetworkStatus();
// status fields: current_state, network_manager, current_slot,
//                connected_nodes, is_synchronized, time_since_last_sync_ms
```

`RouteEntry` fields: `destination`, `next_hop`, `hop_count`, `link_quality`, `last_seen_ms`, `is_valid`, `capabilities`, `is_network_manager`, `last_rssi`, `last_snr`.

**Send readiness**

```cpp
if (Result r = mesher->IsReadyToSend(); !r) {
    // Not synchronized, no TX slot allocated, or wrong protocol state.
}
if (Result r = mesher->IsReadyToSend(dst); !r) {
    // Same checks plus self-send rejection and route lookup.
}
```

A non-Success result for the destination overload does not preclude `Send()` from succeeding — direct delivery to a one-hop neighbor is attempted as a best-effort fallback.

**Runtime role changes**

```cpp
Result r = mesher->SetNodeRole(NodeRole::NETWORK_MANAGER);  // queued, applied at safe point
NodeRole current = mesher->GetNodeRole();
```

Transitions:
- `NODE_ONLY`/`AUTO` → `NETWORK_MANAGER` before joining: node creates a network immediately.
- `NODE_ONLY`/`AUTO` → `NETWORK_MANAGER` while joined: broadcasts NM_CLAIM; incumbent NM yields.
- `NETWORK_MANAGER` → `NODE_ONLY`/`AUTO` while NM: surrenders and triggers a re-election (~5 superframes of disruption).

**Capability-based discovery**

```cpp
mesher->SetNodeCapabilities(NodeCapabilities::GATEWAY);

auto gw   = mesher->GetClosestGateway();                          // std::optional<RouteEntry>
auto sens = mesher->GetClosestNodeByCapability(NodeCapabilities::SENSOR);
```

**Queue introspection**

```cpp
size_t pending_tx = mesher->GetTxQueueSize();
size_t pending_rx = mesher->GetRxQueueSize();
```

---

### Deployment Tips

#### Pre-designate a Network Manager for faster network formation

By default every node boots in `NodeRole::AUTO` and listens for ~30 s (`DEFAULT_DISCOVERY_TIMEOUT_MS`, with up to ±5 s of jitter) before deciding no network is reachable and creating its own. Pre-designating exactly one node as `NETWORK_MANAGER` skips that wait — the NM creates the network at boot and emits `SYNC_BEACON` immediately, so peers join in seconds instead of minutes.

```cpp
LoRaMeshProtocolConfig protocol;
protocol.setNodeRole(NodeRole::NETWORK_MANAGER);   // one node per network

auto mesher = LoraMesher::Builder()
    .withPinConfig(pins)
    .withRadioConfig(radio)
    .withLoRaMeshProtocol(protocol)
    .Build();
```

All other nodes can stay on the default (`NodeRole::AUTO`) or use `NodeRole::NODE_ONLY` if they should *never* create a network. Avoid configuring two NMs in the same area — when they meet, the merge protocol forces one to step down, costing ~5 superframes of disruption (the same trade-off is documented for runtime `SetNodeRole()` demotions).

**Approximate time from boot to a fully joined node** (at SF7 / BW 125 kHz with the default 10-slot, 10 s discovery-phase superframes):

| Setup | NM up | Peer joined |
|---|---|---|
| All nodes `AUTO` (default) | ~30 s (discovery timeout + jitter) | NM time + 1–3 superframes ≈ **40–60 s** |
| One node `NETWORK_MANAGER`, peers `AUTO` / `NODE_ONLY` | **0 s** (network exists at boot) | 1–3 superframes after first beacon ≈ **10–30 s** |

Once the network is operational, the NM scales slot duration to actual time-on-air — expect ~200 ms slots at SF7/BW125 and ~550 ms slots at SF10/BW125 (the formula is `ceil_50(ToA(max_packet_size) + guard + 50 ms margin)`). See [PROTOCOL_SPEC.md](PROTOCOL_SPEC.md) §5 and §6 for the full timing rules.

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

### Spreading factor and `max_packet_size`

LoRaMesher selects a default `max_packet_size` from the active spreading factor and bandwidth so that time-on-air — and therefore TDMA slot duration — stays within practical bounds at high SF. At BW 125 kHz:

| SF | Default `max_packet_size` |
|----|---------------------------|
| SF7 / SF8 | 242 bytes |
| SF9 | 115 bytes |
| SF10 / SF11 / SF12 | 51 bytes |

The table is scaled up by ×2 at BW 250 kHz and ×4 at BW 500 kHz, clamped to the 255-byte PHY ceiling. See `RadioConfig::GetMaxPacketSizeForSf(sf, bw_khz)` for the authoritative helper.

You can override the default explicitly — your value is preserved, and a warning is logged only if it exceeds the SF-safe cap:

```cpp
RadioConfig radio;
radio.setSpreadingFactor(10);
radio.setBandwidth(125.0f);

LoRaMeshProtocolConfig protocol;
protocol.setMaxPacketSize(200);  // keep 200 B at SF10 — logs a warning

auto mesher = LoraMesher::Builder()
    .withRadioConfig(radio)
    .withLoRaMeshProtocol(protocol)
    .Build();
```

If you don't call `setMaxPacketSize()`, the SF-derived default is applied at protocol configuration time.

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

## Contributing

### One-Time Setup

Enable the shared pre-commit hook (runs `clang-format-19` on staged files):

```bash
git config core.hooksPath .githooks
```

This catches formatting issues locally before they reach CI. Install `clang-format-19` to match the CI version:

```bash
# Ubuntu/Debian
sudo apt-get install clang-format-19
```

### Code Style

The project uses the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) with customizations defined in `.clang-format`.

Format all modified files:

```bash
# Format specific files
clang-format-19 -i src/path/to/file.cpp

# Format all source files
find src -name '*.cpp' -o -name '*.hpp' | xargs clang-format-19 -i
```

For deeper static analysis, run `clang-tidy` (see [Static Analysis](#static-analysis-clang-tidy) above).

### Testing Before Pushing

Run the full tests before pushing, they take several minutes but should pass before opening a PR:

```bash
pio test -e test_native -v"
```

### Conventions

- **C++20** standard required
- **4 spaces** indentation, no tabs
- **PascalCase** for classes and functions, **UPPER_CASE** for constants and enums
- **Trailing underscore** for private members (`config_`)
- **Doxygen comments** for public APIs
- Keep comments generic — describe *what* the code does, not *why* a specific change was made

---

## Protocol Design

LoRaMesher is a distance-vector mesh protocol layered over a TDMA superframe. Each node holds a slot allocation table and only transmits during its assigned slots; outside of those, it either listens for control traffic or sleeps. A single elected *Network Manager* distributes routing updates and slot allocations; election, joining, and recovery from manager loss are all driven by the protocol's six-state machine (Initialization → Discovery → Joining → Normal Operation → Network Manager → Fault Recovery).

For the full specification — state-machine transitions, message wire formats, routing algorithm, synchronization timing, discovery/joining flow, error handling, and performance characteristics — see [**PROTOCOL_SPEC.md**](PROTOCOL_SPEC.md). Sequence and superframe diagrams live under [`docs/`](docs/).

---

## Citation

If you use LoRaMesher in academic work, please cite one or both of:

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

```bibtex
@article{SOLE2026108404,
  title   = {Large and reliable data transfer service for LoRa mesh network applications},
  author  = {Solé, J. Miquel and Pueyo Centelles, R. and Freitag, F. and Meseguer, R. and Baig, R.},
  journal = {Computer Communications},
  volume  = {248},
  year    = {2026},
  doi     = {10.1016/j.comcom.2025.108404}
}
```

- [IEEE Access (2022) — Implementation of a LoRa Mesh Library](https://ieeexplore.ieee.org/document/9930341)
- [Computer Communications (2026) — Large and reliable data transfer service for LoRa mesh network applications](https://doi.org/10.1016/j.comcom.2025.108404)

---

## License

MIT — see [LICENSE](LICENSE).
