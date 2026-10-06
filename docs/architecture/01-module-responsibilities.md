# Module Responsibilities — current vs. target

> What each folder and significant file *should* own, what it *actually* contains, and the
> deviation to correct. Line counts are `wc -l` of the current source. Refactor progress is
> tracked in the Status table of `06-refactor-roadmap.md`.

## Dependency direction (the one rule)

`app → protocol → services → routing/domain → types → hardware/os → utils`. An `#include`
that points *up* this list is a layering violation. The review found two (§types); both are
fixed (WS-2).

---

## `src/` top level

| File | LOC | Should own | Verdict |
|---|---|---|---|
| `loramesher.hpp` / `.cpp` | 625 / 640 | Public facade + Builder; thin delegation to the protocol | ✅ Clean facade. Watch: 5 distinct callback signatures — candidate for a single observer later. |

## `protocols/` (protocol layer)

| File | LOC | Should own | Verdict |
|---|---|---|---|
| `protocol_manager.{hpp,cpp}` | 143 / 269 | Factory + lifecycle for protocol instances | ✅ Clean factory. |
| `protocol.hpp` (in `types/protocols/`) | 116 | Abstract protocol interface | ✅ Minimal. |
| `lora_mesh_protocol.{hpp,cpp}` | 585 / **1683** | Per-protocol coordinator: event loop + state machine + service wiring | ⚠️ **Too large** — three concerns fused. Deferred split: coordinator / state-machine / event-dispatcher. |
| `ping_pong_protocol.{hpp,cpp}` | 203 / 466 | Simple echo test protocol | ✅ OK. One nested `unordered_map` for ping tracking is avoidable (see `05-…`). |

### `protocols/lora_mesh/services/`

| File | LOC | Should own | Verdict |
|---|---|---|---|
| `network_service.{hpp,cpp}` | 1511 / **3724** | Coordinate node/routing/discovery services | 🔴 **God class** (was 4,713 L). Still holds sync-beacon receive, join, NM election, routing/data/broadcast glue and leftover dead stubs. See `02-…`. |
| `reliable_messaging.{hpp,cpp}` | 283 / 736 | Reliable unicast + group send, ACKs, group membership and group receive/forward | ✅ Extracted component (WS-5 ph.3). |
| `slot_scheduler.{hpp,cpp}` | 226 / 634 | Sole owner of the TDMA slot table: sizing, fill, discovery/joining reshapes | ✅ Extracted component (WS-5 ph.4). |
| `sync_beacon_service.{hpp,cpp}` | 87 / 181 | Sync-beacon transmit and forward | 🟡 Extracted, stateless; the receive path is still in `NetworkService` (WS-5 ph.5). |
| `message_cache.hpp` | 104 | Per-node sequence counter + `(source, seq)` dedup cache | ✅ Shared by all send/receive paths. |
| `superframe_service.{hpp,cpp}` | 456 / 933 | TDMA superframe timing + slot clock | ✅ Single concern; large but cohesive. |
| `message_queue_service.{hpp,cpp}` | 119 / 209 | Buffer outbound messages by slot type | ✅ Good. |
| `subslot_scheduler.{hpp,cpp}` | 120 / 175 | Intra-slot TX scheduling | ✅ Good. Its config types now live in `types/protocols/lora_mesh/subslot_config.hpp` (WS-2). |

### `protocols/lora_mesh/interfaces/`

| File | Should own | Verdict |
|---|---|---|
| `i_network_service.hpp` (392 L, 33 pure-virtual methods) | Cohesive service interface | ⚠️ Mixes several roles — segregate in WS-6. `ProtocolState` is now a `using` alias of the `types/` enum. Still declares the no-op `NotifySuperframeOfNetworkChanges`. |
| `i_routing_table.hpp` (390 L) | Routing algorithm interface | ✅ Good abstraction, properly used. |
| `i_superframe_service.hpp`, `i_message_queue_service.hpp` | Role interfaces | ✅ |

The former dead stubs `i_slot_management_service.hpp`, `i_join_service.hpp` and
`i_network_discovery_service.hpp` have been deleted (WS-5 ph.1).

### `protocols/lora_mesh/routing/`

| File | LOC | Should own | Verdict |
|---|---|---|---|
| `distance_vector_routing_table.{hpp,cpp}` | 313 / 1357 | Distance-vector implementation | ✅ Legitimate algorithm complexity, isolated behind the interface. |
| `routing_table_factory.cpp` | 20 | Factory | ✅ |

### `protocols/reliability/`

| File | LOC | Should own | Verdict |
|---|---|---|---|
| `reliable_delivery.{hpp,cpp}` | 268 / 242 | End-to-end reliable delivery via Host closures | ✅ **Reference pattern** for all extractions. |
| `delivery_windows.{hpp,cpp}`, `rtt_estimator.hpp` | 79 / 91, 75 | Group-ACK delivery windows; per-peer RTT estimate for the adaptive timeout | ✅ |

## `types/` (shared types — must have NO upward deps)

| File | LOC | Should own | Verdict |
|---|---|---|---|
| `error_codes/result.hpp` | 206 | `Result<T>` type | ✅ |
| `error_codes/loramesher_error_codes.hpp` | 132 | Error enum | ✅ |
| `configurations/protocol_configuration.hpp` | **908** | Protocol config structs | ⚠️ Too large (4 configs in one file) — deferred split. Its former upward include of `services/subslot_scheduler.hpp` is gone; it includes `types/protocols/lora_mesh/subslot_config.hpp` (WS-2 ✅). |
| `application/application_types.hpp` | 55 | App-facing value types/enums | ✅ Includes `types/protocols/lora_mesh/protocol_state.hpp` instead of `i_network_service.hpp` (WS-2 ✅). |
| `protocols/lora_mesh/protocol_state.hpp`, `subslot_config.hpp` | 29 / 54 | `ProtocolState` enum; subslot config types | ✅ Relocated down from `protocols/` (WS-2). |
| `messages/` (base + 14 loramesher types) | base 893 + types ~2,190 (`.cpp`) | Wire formats | ⚠️ Heavy serialization boilerplate, no shared base. See `03-…`. |
| `protocols/lora_mesh/network_node_route.{hpp,cpp}` | 438 / 473 | Route value type | ⚠️ Carries link-quality calculation that arguably belongs in the routing/service layer — review during WS-5. |
| `radio/`, `hardware/`, `power/`, `node_capabilities.hpp` | – | Interfaces + POD types | ✅ |

## `hardware/`

| File | LOC | Should own | Verdict |
|---|---|---|---|
| `hal.hpp`, `hal_factory.hpp` | 68 / 39 | HAL abstraction | ✅ |
| `hardware_manager.{hpp,cpp}` | 214 / 237 | Orchestrate radio + HAL | ✅ |
| `radiolib/radiolib_radio.{hpp,cpp}` | 417 / 637 | RadioLib `IRadio` wrapper | ✅ Reasonable for radio control. |
| `radiolib/radiolib_modules/radiolib_module_base.hpp` | 472 | Shared driver template + SX126x/SX127x family layers | ✅ Replaces the four duplicated per-chip drivers (WS-1). See `04-…`. |
| `radiolib/radiolib_modules/sx12{62,68,76,78}.hpp` | ~34 each | Per-chip leaf (RadioLib type + chip name) | ✅ Header-only; the per-chip `.cpp` files are removed. |
| `arduino/`, `native/`, `SPIMock.*`, `mocks/` | – | Platform impls + test doubles | ✅ |

## `os/`

| File | LOC | Verdict |
|---|---|---|
| `rtos.hpp` | 422 | ✅ Clean abstract RTOS interface. |
| `rtos_freertos.hpp` | 452 | ✅ FreeRTOS impl. Minor: system-semaphore wrappers are thin (low-ROI to merge). |
| `rtos_mock.hpp` | **2697** | ✅ Legitimate full mock for native virtual-time testing; large but justified. |

## `utils/`

| File | LOC | Verdict |
|---|---|---|
| `logger.{hpp,cpp}`, `file_log_handler.hpp` | 336 / 123 / 234 | ✅ |
| `byte_operations.h` | 263 | ✅ Clean `ByteSerializer`/`ByteDeserializer` — consistently used. The message duplication is in *callers*, not here. |
| `address_generator.{hpp,cpp}`, `lora_airtime.hpp`, `task_monitor.hpp`, `compat/span.hpp` | – | ✅ |

## Priority of fixes (cross-ref to roadmap)

1. **WS-1** radio modules — done.
2. **WS-2** layering — done.
3. **WS-3 / WS-4** message DRY + memory — independent of the god class.
4. **WS-5** network_service decomposition — the structural core.
5. **WS-6** interface segregation — last (touches the most tests).
6. *Deferred:* `lora_mesh_protocol.cpp` and `protocol_configuration.hpp` splits.
