# Module Responsibilities — current vs. target

> What each folder and significant file *should* own, what it *actually* contains, and the
> deviation to correct. Line counts verified with `wc -l` (2026-06).

## Dependency direction (the one rule)

`app → protocol → services → routing/domain → types → hardware/os → utils`. An `#include`
that points *up* this list is a layering violation. Two exist today (§types).

---

## `src/` top level

| File | LOC | Should own | Verdict |
|---|---|---|---|
| `loramesher.hpp` / `.cpp` | 623 / 642 | Public facade + Builder; thin delegation to the protocol | ✅ Clean facade. Watch: 5 distinct callback signatures — candidate for a single observer later. |

## `protocols/` (protocol layer)

| File | LOC | Should own | Verdict |
|---|---|---|---|
| `protocol_manager.{hpp,cpp}` | – / 269 | Factory + lifecycle for protocol instances | ✅ Clean factory. |
| `protocol.hpp` (in `types/protocols/`) | 116 | Abstract protocol interface | ✅ Minimal. |
| `lora_mesh_protocol.{hpp,cpp}` | 594 / **1696** | Per-protocol coordinator: event loop + state machine + service wiring | ⚠️ **Too large** — three concerns fused. Deferred split: coordinator / state-machine / event-dispatcher. |
| `ping_pong_protocol.{hpp,cpp}` | 203 / 468 | Simple echo test protocol | ✅ OK. One nested `unordered_map` for ping tracking is avoidable (see `05-…`). |

### `protocols/lora_mesh/services/`

| File | LOC | Should own | Verdict |
|---|---|---|---|
| `network_service.{hpp,cpp}` | 1512 / **4713** | Coordinate node/routing/discovery services | 🔴 **God class** — nine responsibilities. See `02-…`. |
| `superframe_service.{hpp,cpp}` | – / 934 | TDMA superframe timing + slot clock | ✅ Single concern; large but cohesive. |
| `message_queue_service.{hpp,cpp}` | – / 209 | Buffer outbound messages by slot type | ✅ Good. |
| `subslot_scheduler.{hpp,cpp}` | – / 175 | Intra-slot TX scheduling | ✅ Good (but its config is referenced from `types/` — a layering bug, see §types). |

### `protocols/lora_mesh/interfaces/`

| File | Should own | Verdict |
|---|---|---|
| `i_network_service.hpp` (401 L, ~82 methods) | Cohesive service interface | ⚠️ Bloated — segregate in WS-6. Also leaks `ProtocolState` enum (layering). |
| `i_superframe_service.hpp`, `i_message_queue_service.hpp` | Role interfaces | ✅ |
| `i_slot_management_service.hpp`, `i_join_service.hpp`, `i_network_discovery_service.hpp` | (intended future interfaces) | 🔴 **Dead** — referenced nowhere. Delete (WS-5 ph.1). |

### `protocols/lora_mesh/routing/`

| File | LOC | Should own | Verdict |
|---|---|---|---|
| `i_routing_table.hpp` | 345 | Routing algorithm interface | ✅ Good abstraction, properly used. |
| `distance_vector_routing_table.{hpp,cpp}` | 298 / 1281 | Distance-vector implementation | ✅ Legitimate algorithm complexity, isolated behind the interface. |
| `routing_table_factory.cpp` | 20 | Factory | ✅ |

### `protocols/reliability/`

| File | LOC | Should own | Verdict |
|---|---|---|---|
| `reliable_delivery.{hpp,cpp}` | 174 / 168 | End-to-end reliable delivery via Host closures | ✅ **Reference pattern** for all extractions. |

## `types/` (shared types — must have NO upward deps)

| File | LOC | Should own | Verdict |
|---|---|---|---|
| `error_codes/result.hpp` | 207 | `Result<T>` type | ✅ |
| `error_codes/loramesher_error_codes.hpp` | 132 | Error enum | ✅ |
| `configurations/protocol_configuration.hpp` | **909** | Protocol config structs | ⚠️ Too large (4 configs in one file) **and** 🔴 includes `protocols/lora_mesh/services/subslot_scheduler.hpp` — **upward dependency**. Fix: move `SubslotSchedulerConfig` into `types/` (WS-2). |
| `application/application_types.hpp` | 55 | App-facing value types/enums | 🔴 includes `protocols/lora_mesh/interfaces/i_network_service.hpp` to re-export `ProtocolState` — **upward dependency**. Fix: move the enum into `types/protocols/lora_mesh/protocol_state.hpp` (WS-2). |
| `messages/` (base + 14 loramesher types) | base 893 + types ~2177 | Wire formats | ⚠️ Heavy serialization boilerplate, no shared base. See `03-…`. |
| `protocols/lora_mesh/network_node_route.{hpp,cpp}` | 407 / 452 | Route value type | ⚠️ Carries link-quality calculation that arguably belongs in the routing/service layer — review during WS-5. |
| `radio/`, `hardware/`, `power/`, `node_capabilities.hpp` | – | Interfaces + POD types | ✅ |

## `hardware/`

| File | LOC | Should own | Verdict |
|---|---|---|---|
| `hal.hpp`, `hal_factory.hpp` | 68 / 39 | HAL abstraction | ✅ |
| `hardware_manager.{hpp,cpp}` | – / 237 | Orchestrate radio + HAL | ✅ |
| `radiolib/radiolib_radio.{hpp,cpp}` | 417 / 638 | RadioLib `IRadio` wrapper | ✅ Reasonable for radio control. |
| `radiolib/radiolib_modules/sx12{62,68,76,78}.{hpp,cpp}` | ~1,350 total | Per-chip driver | 🔴 **4 near-identical copies** (normalized diff = 0 within family). Collapse to one template base (WS-1). See `04-…`. |
| `arduino/`, `native/`, `SPIMock.*`, `mocks/` | – | Platform impls + test doubles | ✅ |

## `os/`

| File | LOC | Verdict |
|---|---|---|
| `rtos.hpp` | 421 | ✅ Clean abstract RTOS interface. |
| `rtos_freertos.hpp` | 437 | ✅ FreeRTOS impl. Minor: system-semaphore wrappers are thin (low-ROI to merge). |
| `rtos_mock.hpp` | **2478** | ✅ Legitimate full mock for native virtual-time testing; large but justified. |

## `utils/`

| File | LOC | Verdict |
|---|---|---|
| `logger.{hpp,cpp}`, `file_log_handler.hpp` | 336 / 123 / 234 | ✅ |
| `byte_operations.h` | 263 | ✅ Clean `ByteSerializer`/`ByteDeserializer` — consistently used. The message duplication is in *callers*, not here. |
| `address_generator.{hpp,cpp}`, `lora_airtime.hpp`, `task_monitor.hpp`, `compat/span.hpp` | – | ✅ |

## Priority of fixes (cross-ref to roadmap)

1. **WS-1** radio modules — biggest LOC cut at lowest risk (isolated, verified duplicate).
2. **WS-2** layering — small, mechanical, removes the only two dependency-direction bugs.
3. **WS-3 / WS-4** message DRY + memory — independent of the god class.
4. **WS-5** network_service decomposition — the structural core.
5. **WS-6** interface segregation — last (touches the most tests).
6. *Deferred:* `lora_mesh_protocol.cpp` and `protocol_configuration.hpp` splits.
