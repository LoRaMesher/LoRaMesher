# LoRaMesher Architecture — Overview

> Status: **review draft** (2026-06). Captures the current architecture, its deviations,
> and the target end-state. No code has changed yet; this set drives the refactor in
> `06-refactor-roadmap.md`. Every quantitative claim here was verified directly against the
> source (`wc -l`, `grep`, `diff`) — see each doc's "Validation" note.

## What LoRaMesher is

A C++20 library implementing a distance-vector routing protocol over a TDMA superframe for
LoRa mesh networks. It runs on **ESP32** (Arduino + FreeRTOS + RadioLib drivers) and on a
**native desktop target** (mocked hardware/RTOS) used for the GoogleTest suites. RAM is
constrained and heap fragmentation has caused field crashes, so memory discipline is a
first-class concern (see `05-memory-model.md`).

`src/` is ~37k LOC. The single largest file, `network_service.cpp`, is 4,713 lines and
concentrates nine responsibilities; decomposing it is the core of the roadmap.

## Target layered architecture

Dependencies point **downward only**. A lower layer must never `#include` a higher one.

```
  Application API     loramesher.{hpp,cpp}                       facade + Builder
        │
        ▼ depends on
  Protocol layer      protocols/ (protocol_manager, lora_mesh, ping_pong)
        │             owns the state machine; coordinates services
        ▼
  Service layer       protocols/lora_mesh/services/ + protocols/reliability/
        │             focused, single-responsibility, closure-injected components
        ▼
  Domain / routing    protocols/lora_mesh/routing/ + types/protocols/lora_mesh/
        │             routing algorithm (IRoutingTable) + pure protocol value types
        ▼
  Shared types        types/ (messages, configurations, error_codes, radio/hardware ifaces)
        │
        ▼
  Abstractions        hardware/ (HAL + RadioLib drivers), os/ (RTOS)  — behind interfaces
        │
        ▼
  Utilities           utils/ (logger, byte_operations, span compat, lora_airtime)  — leaf
```

**Rule of thumb for each layer:**
- *Application* exposes a small, stable, intention-revealing API and nothing else.
- *Protocol* decides *what happens when*; it holds the state machine and wires services.
- *Services* each do *one* thing and receive their cross-cutting dependencies as injected
  closures + plain config structs (the `reliability::ReliableDelivery` model).
- *Types* are pure data + interfaces with **no upward dependencies**.
- *Hardware/OS* are swappable implementations hidden behind `IRadio` / `os::RTOS`.
- *Utils* depend on nothing in the project.

## Where the code deviates today (summary)

| # | Deviation | Evidence | Fixed by |
|---|---|---|---|
| 1 | `network_service.cpp` is a 9-responsibility god class (4,713 L) | file size + method inventory | WS-5 (`02-…`) |
| 2 | `types/` depends upward on `protocols/` (2 includes) | grep (`01-…`) | WS-2 |
| 3 | 4 radio modules are duplicate copies (~1,350 L) | normalized `diff` = 0 (`04-…`) | WS-1 |
| 4 | 14 message types repeat serialization boilerplate (~2,177 L) | per-file inspection (`03-…`) | WS-3 |
| 5 | TX/forward path triple-copies every message | RAM trace (`05-…`) | WS-4 |
| 6 | Dead code: 3 unused interface stubs + `LinkQualityMetrics` | grep (no callers) | WS-5 ph.1 |
| 7 | `lora_mesh_protocol.cpp` (1,696 L) mixes 3 concerns; `protocol_configuration.hpp` (909 L) | file size | deferred follow-ups |

## The behavior-preservation invariant

The protocol is **timing-sensitive** — slot scheduling, beacon cadence, and election
backoffs depend on exact ordering. Therefore:

1. Every refactor step preserves observable behavior exactly. Refactors are structural, not
   semantic.
2. `loramesher.hpp`'s public API and `LoRaMeshProtocol::GetNetworkServiceForTest()` stay
   stable until the explicit interface-segregation phase (WS-6).
3. The 118 `test_unit_network_coverage` cases are the characterization net; they must stay
   green across every phase with no assertion edits (only call-site renames when a method
   moves).
4. Slow integration suites (`test_routing`, `test_tdma`, `test_sync_beacon_subslot`,
   `test_routing_nm_merge`, `test_routing_role_change`) run as phase exit gates, never inside
   a red→green loop (the full `test_routing` suite is ~95 min).

## The reference pattern: closure-injected components

`protocols/reliability/reliable_delivery.{hpp,cpp}` (168 L cpp) is the model every extracted
component imitates:

- **No heap, no virtual dispatch.** Fixed-capacity arrays for pending state.
- Constructed from a **`Host`** struct of `std::function` closures (`now_ms`, `send_attempt`,
  …) — the component calls back into its owner without knowing the owner's type.
- Plus a **`Policy`/config** struct of plain values.

This keeps each component independently unit-testable (mock the closures) while the
coordinator (`NetworkService`) owns the canonical state and the single validated mutation
points. See `02-network-service-decomposition.md` for how state ownership is split.

## How to read this set

- `01-module-responsibilities.md` — per-folder/per-file responsibility contract + deviations.
- `02-network-service-decomposition.md` — the god-class inventory and component split.
- `03-message-serialization.md` — message duplication catalogue + consolidation design.
- `04-hardware-radio-modules.md` — the verified radio-driver duplication + template design.
- `05-memory-model.md` — end-to-end per-message RAM trace + copy-chain elimination.
- `06-refactor-roadmap.md` — the sequenced, test-first execution plan with gates.
