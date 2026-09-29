# Migration Guide: 0.0.x → 1.0.0

LoRaMesher 1.0.0 is a complete rewrite. The public API, configuration model,
and wire protocol have all changed — your `0.0.x` sketch will not compile
against 1.0.0, and a `0.0.x` node cannot talk to a 1.0.0 node on the air. This
guide walks through what to update in user code.

For protocol-level details (state machine, message formats, TDMA superframe,
routing), see [PROTOCOL_SPEC.md](PROTOCOL_SPEC.md). For the full release
summary, see [CHANGELOG.md](CHANGELOG.md).

> **Network upgrade is all-or-nothing.** Wire formats are incompatible. Plan
> to flash every node in a deployment in one window — mixed-version networks
> will not form.

## At a glance

| Concern | 0.0.x (`v0.0.11-legacy`) | 1.0.0 |
|---|---|---|
| Header | `#include "LoraMesher.h"` | `#include "loramesher.hpp"` (namespace `loramesher`) |
| Instance | `LoraMesher& r = LoraMesher::getInstance();` | `auto m = LoraMesher::Builder()...Build();` (`std::unique_ptr`) |
| Init | `r.begin(cfg); r.start();` | `Result r = m->Start();` |
| Send | `r.createPacketAndSend<T>(dst, ptr, n)` (void) | `Result r = m->Send(dst, std::vector<uint8_t>);` |
| Broadcast | `r.createPacketAndSend(BROADCAST_ADDR, ...)` | `Result r = m->SendBroadcast(payload);` |
| Receive | Task handle + `ulTaskNotifyTake` + queue poll | `m->SetDataCallback(cb);` |
| Cleanup | `r.deletePacket(pkt);` (mandatory) | automatic |
| Config | `LoraMesher::LoraMesherConfig` flat struct | `PinConfig` + `RadioConfig` + `LoRaMeshProtocolConfig` |
| Module enum | `LoraMesher::LoraModules::SX1276_MOD` | `RadioType::kSx1276` |
| Lifecycle | `standby()` / `start()` cycle | `Stop()` then rebuild |
| Errors | silent `void` returns | `[[nodiscard]] Result` |
| Payload type | `AppPacket<UserStruct>` (templated) | `std::vector<uint8_t>` (serialize yourself) |

## 1. Initialization

The singleton is gone, configuration is now three composed objects, and
`begin()` + `start()` collapse into a single `Builder().Build()` + `Start()`.

**Before (0.0.11):**
```cpp
#include "LoraMesher.h"

LoraMesher& radio = LoraMesher::getInstance();

void setupLoraMesher() {
    LoraMesher::LoraMesherConfig config = LoraMesher::LoraMesherConfig();
    config.loraCs  = 18;
    config.loraRst = 23;
    config.loraIrq = 26;
    config.loraIo1 = 33;
    config.module  = LoraMesher::LoraModules::SX1276_MOD;

    radio.begin(config);
    radio.setReceiveAppDataTaskHandle(receiveLoRaMessage_Handle);
    radio.start();
}
```

**After (1.0.0):**
```cpp
#include "loramesher.hpp"
using namespace loramesher;

std::unique_ptr<LoraMesher> mesher;

void setupLoraMesher() {
    PinConfig pins(/*cs=*/18, /*rst=*/23, /*dio0=*/26, /*dio1=*/33);
    RadioConfig radio(RadioType::kSx1276, /*freq_mhz=*/869.9F,
                      /*sf=*/7, /*bw_khz=*/125.0, /*cr=*/7,
                      /*power_dbm=*/6, /*sync=*/20, /*crc=*/true,
                      /*preamble=*/8);
    LoRaMeshProtocolConfig protocol;  // defaults are sensible

    mesher = LoraMesher::Builder()
        .withPinConfig(pins)
        .withRadioConfig(radio)
        .withLoRaMeshProtocol(protocol)
        .Build();

    if (Result r = mesher->Start(); !r) {
        Serial.printf("Start failed: %s\n", r.GetErrorMessage().c_str());
    }
}
```

Notes:
- `mesher` is a `std::unique_ptr<LoraMesher>` — share it via reference, not by
  value, and tear it down explicitly if you need to reconfigure.
- `Stop()` halts tasks; there is no `standby()` / resume. Rebuild the instance
  to restart.
- Board pinouts are documented in `README.md` under *Common board presets*.

## 2. Sending

`createPacketAndSend<T>(dst, ptr, n)` is replaced by `Send(dst, vector)` for
unicast and `SendBroadcast(vector)` for broadcast. Both return `Result` and
are `[[nodiscard]]` — the compiler will warn if you ignore the return value.

**Before:**
```cpp
struct dataPacket { uint32_t counter = 0; };
dataPacket* helloPacket = new dataPacket;

void loop() {
    helloPacket->counter = dataCounter++;
    radio.createPacketAndSend(BROADCAST_ADDR, helloPacket, 1);
    vTaskDelay(20000 / portTICK_PERIOD_MS);
}
```

**After:**
```cpp
struct DataPacket { uint32_t counter = 0; };

void loop() {
    DataPacket pkt{ .counter = dataCounter++ };
    std::vector<uint8_t> payload(reinterpret_cast<uint8_t*>(&pkt),
                                 reinterpret_cast<uint8_t*>(&pkt) + sizeof(pkt));

    if (Result r = mesher->SendBroadcast(payload); !r) {
        Serial.printf("Send failed: %s\n", r.GetErrorMessage().c_str());
    }
    vTaskDelay(20000 / portTICK_PERIOD_MS);
}
```

The payload is now a flat byte buffer — serialize your structs yourself
(plain `memcpy` for POD, or a real serializer for anything with pointers or
non-trivial alignment). The 0.0.x `AppPacket<T>` template is gone.

For deterministic timing, query the next slot:
```cpp
uint32_t wait_ms = mesher->GetTimeUntilNextDataSlot();
vTaskDelay(pdMS_TO_TICKS(wait_ms));
auto r = mesher->Send(dst, payload);
```

## 3. Receiving

The task-handle + `ulTaskNotifyTake` + queue-poll + `deletePacket` pattern is
replaced by a single callback registration.

**Before:**
```cpp
TaskHandle_t receiveLoRaMessage_Handle = NULL;

void processReceivedPackets(void*) {
    for (;;) {
        ulTaskNotifyTake(pdPASS, portMAX_DELAY);
        while (radio.getReceivedQueueSize() > 0) {
            AppPacket<dataPacket>* packet =
                radio.getNextAppPacket<dataPacket>();
            // ... use packet->payload, packet->src, packet->getPayloadLength()
            radio.deletePacket(packet);   // mandatory
        }
    }
}

xTaskCreate(processReceivedPackets, "RX", 4096, nullptr, 2,
            &receiveLoRaMessage_Handle);
radio.setReceiveAppDataTaskHandle(receiveLoRaMessage_Handle);
```

**After:**
```cpp
void OnDataReceived(AddressType source, const std::vector<uint8_t>& data) {
    // source = sender address, data = raw payload bytes
}

mesher->SetDataCallback(OnDataReceived);
```

> ⚠ The callback runs in the protocol context. Keep it short — for any
> non-trivial work, push the payload onto your own queue and drain it from a
> dedicated task. See `examples/queued_receive_example/` for a ready-made
> pattern.

You no longer need to call `deletePacket(...)` — payload lifetime is owned
by the library and the buffer is valid for the duration of the callback.

## 4. Configuration mapping

| 0.0.x field | 1.0.0 location |
|---|---|
| `config.loraCs` | `PinConfig(cs, …)` |
| `config.loraRst` | `PinConfig(_, rst, …)` |
| `config.loraIrq` | `PinConfig(_, _, dio0, …)` |
| `config.loraIo1` | `PinConfig(_, _, _, dio1)` |
| `config.module` | `RadioConfig(RadioType::kSx127x / kSx126x / …)` |
| `config.freq` | `RadioConfig(_, freq_mhz, …)` |
| `config.sf` | `RadioConfig(_, _, sf, …)` |
| `config.bw` | `RadioConfig(_, _, _, bw_khz, …)` |
| `config.cr` | `RadioConfig(_, _, _, _, cr, …)` |
| `config.power` | `RadioConfig(_, _, _, _, _, power_dbm, …)` |
| `config.syncWord` | `RadioConfig(_, _, _, _, _, _, sync, …)` |
| `config.preambleLength` | `RadioConfig(_, _, _, _, _, _, _, _, preamble)` |

Anything mesh-protocol related (timeouts, role, slot counts) lives on
`LoRaMeshProtocolConfig`. Defaults match the documented behavior; override
per-field as needed.

## 5. Removed APIs

| Removed (0.0.x) | Replacement (1.0.0) |
|---|---|
| `LoraMesher::getInstance()` | `LoraMesher::Builder()...Build()` → `std::unique_ptr<LoraMesher>` |
| `begin(LoraMesherConfig)` + `start()` | `Build()` (consumes configs) + `Start()` |
| `setReceiveAppDataTaskHandle(handle)` | `SetDataCallback(cb)` |
| `getReceivedQueueSize()` | none — payloads delivered via callback |
| `getNextAppPacket<T>()` | callback parameter `const std::vector<uint8_t>& data` |
| `deletePacket(packet)` | none — automatic |
| `createPacketAndSend<T>(dst, ptr, n)` | `Send(dst, vector)` / `SendBroadcast(vector)` |
| `standby()` / resume cycle | `Stop()` + rebuild instance |
| `LoraMesher::LoraMesherConfig` | `PinConfig` + `RadioConfig` + `LoRaMeshProtocolConfig` |
| `LoraMesher::LoraModules` enum | `RadioType` enum (`kSx1276`, `kSx1262`, …) |
| `AppPacket<T>` templated user packet | flat `std::vector<uint8_t>`; serialize on top |
| `BROADCAST_ADDR` macro | `SendBroadcast(...)` (no destination needed) |

## 6. New capabilities worth adopting

These have no 0.0.x equivalent — once your sketch compiles, look at the
[`README.md` API Usage](README.md#api-usage) section for full details:

- **Send-readiness probe** — `IsReadyToSend()` and `IsReadyToSend(dst)` return
  a `Result` describing whether the node is synchronized, has a TX slot, and
  knows a route to `dst`. Use it in your send loop instead of guessing.
- **TDMA timing** — `GetTimeUntilNextDataSlot()` and
  `GetDataSlotsPerSuperframe()` let you sleep until your slot opens.
- **Capability discovery** — `SetNodeCapabilities()` and
  `GetClosestGateway()` / `GetClosestNodeByCapability()` for role-aware
  routing (e.g. closest gateway for uplink).
- **Network introspection** — `GetRoutingTable()` and `GetNetworkStatus()`
  expose the routing state for diagnostics and dashboards.
- **Runtime role changes** — `SetNodeRole(NodeRole::NETWORK_MANAGER)` to
  promote/demote a node without rebooting. Pre-designating one NM at boot
  cuts time-to-network from ~30 s to immediate; see the *Deployment Tips*
  section in `README.md`.

## 7. Protocol incompatibility

The 1.0.0 wire format introduces TDMA superframes, sponsor-based join, NM
election with merge handling, and link-quality-weighted routing. **A 0.0.x
node will not see, route, or respond to any 1.0.0 traffic, and vice versa.**
Plan deployments accordingly:

- Flash every node in a network during one upgrade window.
- Field-test on a small lab network before rolling out.
- See [PROTOCOL_SPEC.md](PROTOCOL_SPEC.md) for the wire format, state
  machine, and timing rules.

## Reference: complete before/after

A complete 0.0.x → 1.0.0 conversion of the canonical "broadcast a counter"
example is the difference between `examples/Counter/` on the
`v0.0.11-legacy` tag and `examples/simple_example/` on `main` — diff them
side-by-side if you want a single reference point.
