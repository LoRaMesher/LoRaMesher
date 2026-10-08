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

> **Upgrading?** 2.0.0 changes the wire format (flash every node together) and 1.0.0 was a full rewrite of 0.0.x — see [MIGRATION.md](MIGRATION.md) for both upgrade paths. Per-version changes live in [CHANGELOG.md](CHANGELOG.md); the wire protocol is documented in [PROTOCOL_SPEC.md](PROTOCOL_SPEC.md).

---

## Table of Contents

- [Features](#features)
- [Quick Start](#quick-start)
- [API Usage](#api-usage)
  - [Initialization](#initialization)
  - [Receiving Packets](#receiving-packets)
  - [Sending Packets](#sending-packets)
  - [Reliable and Group Messaging](#reliable-and-group-messaging)
  - [Timing-Aware Sending (TDMA)](#timing-aware-sending-tdma)
  - [Restarts: OTA Updates and Deep Sleep](#restarts-ota-updates-and-deep-sleep)
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
  - [Network Stress Test](#network-stress-test)
  - [Node Reboot Tests](#node-reboot-tests)
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
- **Warm restart** — keep the network id, member slots and sequence numbers across OTA reboots and deep sleep (`SaveState()`)
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
| `examples/reliable_example` | Messages must be acknowledged — `SendReliable()` with delivery outcomes |
| `examples/group_example` | One message to many nodes — `JoinGroup()` / `SendGroup()` with member acknowledgements |

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

To also get the destination (this node, a group or broadcast), the sender's sequence number and the hops travelled, register `SetDataCallbackEx()`. It reports the same messages as one `ReceivedData`; when both callbacks are set, both fire:

```cpp
mesher->SetDataCallbackEx([](const ReceivedData& message) {
    // message.source, message.dest, message.seq, message.hops
    // message.payload is valid only during the callback
});
```

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

### Reliable and Group Messaging

`SendReliable()` asks the destination to acknowledge the message and retransmits until it does or the retries run out. `SendGroup()` floods one message to every member of a group (`0x8000`–`0xFFFE`), optionally collecting the members' acknowledgements. Both return a `MessageId` (`id.source == 0` means the send was rejected), and `SetDeliveryCallback()` later reports what happened to it:

```cpp
mesher->SetDeliveryCallback([](const LoraMesher::DeliveryResult& result) {
    // Delivered:         result.by acknowledged, result.rtt_ms round trip
    //                    (once per member for an acknowledged group send)
    // Failed:            no acknowledgement after all retries
    // GroupWindowClosed: group window ended, result.ack_count members answered
});

LoraMesher::MessageId id = mesher->SendReliable(dst, payload, ReliableOptions{});

mesher->JoinGroup(0x8001);
GroupSendOptions group_options;
group_options.request_acks = true;
LoraMesher::MessageId gid = mesher->SendGroup(0x8001, payload, group_options);
```

The three callbacks report different things:

| Callback | Reports |
|---|---|
| `SetDataCallback(source, data)` | Messages received by this node |
| `SetDataCallbackEx(const ReceivedData&)` | The same messages, with destination, sequence number and hops |
| `SetDeliveryCallback(const DeliveryResult&)` | What happened to this node's `SendReliable()` / acknowledged `SendGroup()` |

**Traffic budget.** Each node sends at most `GetDataSlotsPerSuperframe()` packets per superframe, and those slots also carry the messages it forwards and the ACKs it returns. A reliable message over `h` hops costs about `2h` transmissions; a group message costs about one transmission per node, plus the members' ACKs. Signs of overload are a `MessageId` with `source == 0`, `kQueueFull`, and `Failed` outcomes. Do not send at a fixed rate: send the next message after the previous outcome arrives and `GetTxQueueSize()` is 0, as the examples do.

See `examples/reliable_example` and `examples/group_example`.

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

### Restarts: OTA Updates and Deep Sleep

A reset normally makes a node start from scratch: a rebooted network manager forms a new network,
and every node restarts its message sequence numbers, so neighbours drop its first messages as
duplicates. With a state store the node saves a small snapshot (at most a few hundred bytes) right
before the reset and resumes in the same network afterwards:

```cpp
#include "loramesher.hpp"

auto mesher = LoraMesher::Builder()
    .withPinConfig(pins)
    .withRadioConfig(radio)
    .withLoRaMeshProtocol(protocol)
    // RtcStateStore survives deep sleep and esp_restart(); NvsStateStore
    // (flash) also survives a firmware update and power loss
    .withStateStore(std::make_shared<storage::NvsStateStore>())
    .Build();
mesher->Start();  // restores and erases the snapshot, if any

// Before an OTA reboot or deep sleep:
mesher->SaveState();  // before Stop(), which clears the state
esp_restart();        // or esp_deep_sleep_start()
```

| Store | Survives | Notes |
|---|---|---|
| `RtcStateStore` | deep sleep, `esp_restart()` | No flash wear; a new firmware image may move it, so use NVS for OTA |
| `NvsStateStore` | OTA update, any reset, power loss | One flash write per save; save before planned resets only |
| `MemoryStateStore` | protocol restart in the same process | Native builds and tests |

What a restart restores:

- **Network manager**: listens 1.5 superframes for its network (and joins a successor if one was
  elected meanwhile), then resumes the network under the same id. Members keep their control
  slots; slots of members that never come back are released after `node_timeout_ms`.
- **Every node**: continues its message sequence numbers, so nothing is dropped as a duplicate.
- **Members**: rejoin automatically; after a reset of the whole network they wait longer before
  forming a network of their own.

Members wait at least two superframes after losing the manager's beacons before electing a
successor, so a short reboot of the manager does not hand its role to another node. Routes,
timing and slot tables are learned again after every restart. See `PROTOCOL_SPEC.md` §6.5.

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

All other nodes can stay on the default (`NodeRole::AUTO`) or use `NodeRole::NODE_ONLY` if they should *never* create a network. Avoid configuring two NMs in the same area — automatic merging of two networks is currently disabled (see `PROTOCOL_SPEC.md` §10.6.9), so their networks stay separate.

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

#### Faster local runs (optional)

The commands above work unchanged. Most of the time goes into building, not running: each of
the 27 suites is a separate binary, and on WSL with the repo on `/mnt/<drive>` every suite
recompiles all of `src/` (~100 s per suite, while most suites run in seconds). Optional
settings that cut this down (measured on WSL2, repo on `/mnt/d`):

| Setting | Effect |
|---|---|
| `ccache` on `PATH` (`sudo apt install ccache`) | Used automatically by `scripts/extra_script.py`; ~99% of the recompiles become cache hits: ~100 s → ~47 s per suite |
| `export PLATFORMIO_BUILD_DIR=$HOME/.cache/loramesher-pio` | Build output on the Linux filesystem, where objects are reused between suites: 39–48 s per suite with ccache |
| `export LORAMESHER_TEST_LOG_DIR=/tmp/loramesher-test-logs` | Per-test log files (`<Suite>_<Test>.log`, default `./test_logs`) go to a fast location |

**Windows:** if the repo lives on a Windows drive, running PlatformIO natively on Windows (with
an MSYS2 `clang64` toolchain on `PATH`) avoids the WSL file-system penalty. `test_native`
(ASAN/UBSAN) and `test_native_profile` both build and pass there; `test_integration` took 28 s
for build + run versus ~121 s from WSL on `/mnt/d`. When switching between WSL and Windows, give
each its own `PLATFORMIO_BUILD_DIR` and `PLATFORMIO_LIBDEPS_DIR`. `test_native_tsan` and
`test_native_xray` are Linux-only; CI runs on Linux.

Failing integration tests print `Reproduce with LORAMESHER_TEST_SEED=N`; set that variable to
replay the same deterministic run.

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

### Network Stress Test

`test/protocols/lora_mesh/services/test_network_stress/` runs a simulated multi-hop mesh under mixed traffic and reports delivery, latency, queue depth and TDMA schedule alignment.

**Topology:** clusters of 5 nodes (one head + 4 leaves in a star) whose heads form a backbone line; the Network Manager is the centre head. **Traffic:** non-reliable leaf→head telemetry, reliable unicast across the backbone, and reliable group sends from the Network Manager.

| Cell | Nodes | Worst-case hops | Asserts | Approx. run time |
|------|-------|-----------------|---------|------------------|
| `10n_uniform` | 10 | 3 | Pass/fail on 1-hop delivery, relay queue, TDMA alignment | ~1 min |
| `25n_uniform` | 25 | 6 | TDMA alignment only (other metrics reported); **opt-in** | ~5–10 min |

Build the suite once, then run a single cell directly (the full suite runs every cell):

```bash
# Build only (library + suite)
pio test -e test_native -f "protocols/lora_mesh/services/test_network_stress" --without-testing

# Run one cell, writing the (multi-MB, verbose) output to a file
.pio/build/test_native/program --gtest_filter='*10n_uniform' > stress.log 2>&1

# The 25-node cell is skipped unless explicitly enabled (it is not run in CI)
LORAMESHER_STRESS_FULL=1 .pio/build/test_native/program --gtest_filter='*25n_uniform' > stress25.log 2>&1

# Extract the results
grep -aE "STRESS SCORECARD|^(reliable|non-reliable|group|relay|collision|TDMA|superframe|VERDICT)|##METRICS##|##MISALIGNED##|  OK |FAILED" stress.log
```

**Reading the output:**

- **Scorecard:** delivery ratio per traffic class, reliable RTT and one-way latency, relay TX-queue depth (max / mean of the final quarter; queue capacity is 10), link collision rate, TDMA misalignments (a count, 0 when every neighbour listens in every slot a node transmits), superframe length and convergence time, and a `HEALTHY` / `COLLAPSED` verdict.
- `##METRICS## {...}`: the same values as one JSON line, for scripting.
- `##DATA## N<i> | ...`: each node's data band after settling — `T` = own TX slot, `R<j>` = listening to node `j`.
- `##ALLOC## N<i> ...`: per-node TX / RX / sleep slot counts and frame length.
- `##MISALIGNED## slot <s>: ...`: a slot where a node transmits but a neighbour is not listening to it.

**Tips:** always redirect to a file and search it with `grep -a` (the log contains colour codes); do not edit sources while a build is running; allow a long timeout for the 25-node cell. The fixture writes per-test logs to `test_logs/` in the current directory — on WSL, run the binary from a Linux directory (not `/mnt/<drive>`), since slow log writes there make the tests stall.

### Node Reboot Tests

`test/protocols/lora_mesh/services/test_node_reboot/` power-cycles nodes of a simulated network
and checks that it recovers. A reboot destroys the node's protocol and hardware (packets addressed
to it are lost while it is off), lets virtual time pass and builds the same device again; each
node's `MemoryStateStore` outlives its reboots, standing in for RTC memory or flash. Scenarios
cover line, star and full-mesh networks, rebooting the manager, a relay, the farthest member or
every node at once, short and long downtimes, low duty cycles, repeated reboots, corrupted or
foreign snapshots, and the same reboots without a store (cold start) for comparison.

A network counts as recovered when, for four superframes in a row, it has one manager (the
original one unless the outage allows an election), every node agrees on the manager and the
network id, members hear every beacon, control slots are unique and inside the band, and every node
has a route to every other. Data must then flow in both directions with the rebooted nodes.

```bash
pio test -e test_native -f "protocols/lora_mesh/services/test_node_reboot" --without-testing
.pio/build/test_native/program --gtest_filter='Scenarios/WarmRebootTest.*Line4*' > reboot.log 2>&1
```

New scenarios take a `NetworkSpec` and the fixture's `RebootNodes(nodes, downtime_ms,
save_state, boot_stagger_ms)`; a deep-sleep cycle is the same reboot with an `RtcStateStore`-like
store and a downtime of whole superframes.

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
