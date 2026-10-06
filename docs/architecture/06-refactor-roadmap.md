# Refactor Roadmap

> The sequenced, test-first execution plan. Each workstream is independently shippable;
> phases inside a workstream are ≤5 files and committed individually. Low-risk, isolated wins
> come first; the timing-sensitive god-class decomposition comes after the base is clean.

## Status

Single source of truth for refactor progress; the other docs in this set point here.
Line counts are `wc -l` of the current source.

| Workstream | Status | Where it stands |
|---|---|---|
| WS-0 Architecture docs | ✅ Done | This set (`00`–`07`). |
| WS-1 RadioLib module consolidation | ✅ Done | `radiolib_modules/radiolib_module_base.hpp` (472 L: `RadioLibModuleBase` + SX126x/SX127x family layers) + four thin leaf headers `sx1262/sx1268/sx1276/sx1278.hpp` (~34 L each). The per-chip `.cpp` drivers are removed. |
| WS-2 `types/` layering | ✅ Done | `ProtocolState` lives in `types/protocols/lora_mesh/protocol_state.hpp`, subslot config in `types/protocols/lora_mesh/subslot_config.hpp`; `using` aliases remain in the old locations. `grep -rn '#include "protocols/' src/types/` returns nothing. |
| WS-3 Message serialization DRY | ⬜ Not started | |
| WS-4 Memory copy chain | ⬜ Not started | `EnqueueForTransmission` (from WS-5 ph.2) is the only seam in place. |
| WS-5 ph.1 Dead code + constants | 🟡 Partial | Removed: the three dead `i_*_service.hpp` interface stubs, `LinkQualityMetrics`, `NetworkService::CalculateComprehensiveLinkQuality`, `AllocateDataSlotsBasedOnRouting`, `FindNextAvailableSlot`. **Still present in `network_service.{hpp,cpp}`:** `SlotTableToSuperframe` (stub, no callers), `UpdateNetworkTopology` (no-op stub, still called), `NotifySuperframeOfNetworkChanges` (no-op, also on `INetworkService`), `CalculateLinkStability` (no callers), `SendSlotRequest` (no callers), and `ProcessSlotAllocation` / `BroadcastSlotAllocation` with commented-out bodies. Named constants deferred to the per-extraction phases (decision D8 in `07`). |
| WS-5 ph.2 Duplication helpers | 🟡 Partial | Done: `EnqueueForTransmission` (uniform send/forward sites); `MessageCache` (`services/message_cache.hpp`: shared sequence counter + `(source, seq)` dedup cache). Not done: `ClampSlot`, `ApplyRouteFromMessage`, `ExtractNmParams`, single `TransitionTo(state)`. |
| WS-5 ph.3 `ReliableMessaging` | ✅ Done | `services/reliable_messaging.{hpp,cpp}` (283 / 736 L) owns the `ReliableDelivery` machine, ACK windows, group membership, reliable and group send, and the group receive/forward path. `DeliverToApp` / `HopsFromTtl` stay in `NetworkService` (shared with data and broadcast). Tests: `test/protocols/reliability/test_reliability/` (`reliable_messaging_test.cpp`, `reliable_messaging_concurrency_test.cpp`). |
| WS-5 ph.4 `SlotScheduler` | ✅ Done | `services/slot_scheduler.{hpp,cpp}` (226 / 634 L) is the sole owner of the slot table; `UpdateSlotTable` is decomposed into `ComputeBandSizes` / `FillSlotTableLocked` / `LogSlotTable` behind `RebuildSlotTable` / `UpdateSlotTableIfDirty`. Tests: `test/protocols/lora_mesh/services/test_unit_slot_scheduler/`. |
| WS-5 ph.5 `SyncBeaconService` | 🟡 Partial | `services/sync_beacon_service.{hpp,cpp}` (87 / 181 L) handles transmit and forward only (`SendSyncBeacon`, `ForwardSyncBeacon`, `ShouldForwardSyncBeacon`, pre-send timestamp callback); it is stateless. The receive path (`ProcessSyncBeacon`, 287 L), `PerformTimingSynchronization`, `HandleSuperframeStart` and the sync state (`last_sync_time_`, `table_version_`, `current_network_depth_`, `beacon_node_count_`, `my_control_slot_index_`, …) are still in `NetworkService`. No dedicated unit suite yet. |
| WS-5 ph.6 `JoinService` | ⬜ Not started | Join logic (`ProcessJoinRequest` 206 L, `ProcessJoinResponse` 118 L, sponsor forwarding) is in `NetworkService`. |
| WS-5 ph.7 `NmElectionService` | ⬜ Not started | `ProcessNMClaim`, `ApplyRoleChange`, election backoff are in `NetworkService`. |
| WS-6 Interface segregation | ⬜ Not started | `INetworkService` (392 L, 33 pure-virtual methods). |
| Deferred file splits | ⬜ Not started | `lora_mesh_protocol.cpp` (1,683 L), `protocol_configuration.hpp` (908 L). |

`NetworkService` today: `network_service.cpp` 3,724 L / `network_service.hpp` 1,511 L
(from 4,713 / 1,512 at the start of the programme). The `~700 L coordinator` end-state still
needs the rest of ph.1/ph.2, the sync receive path, and the Join and NmElection extractions.

## Ordering rationale

```
WS-1 radio modules ─┐  (isolated, verified-duplicate, near-zero risk)
WS-2 layering ──────┤  (mechanical, removes the 2 dependency-direction bugs)
WS-3 message DRY ───┼─ independent of the god class
WS-4 memory ────────┘  (builds EnqueueMessage, reused by WS-5)
                    │
WS-5 NetworkService decomposition  (the core; depends on WS-4's helper)
                    │
WS-6 interface segregation  (last — touches the most tests)
```

**If effort is limited:** finish WS-5 ph.1–2, then WS-4. Together with the completed WS-1
and SlotScheduler extraction that captures most of the cleanliness/memory/maintainability win.

## Universal rules (every phase)

- **Test-first:** write the failing test, confirm it fails *for the right reason*, implement,
  go green. (CLAUDE.md mandate.)
- **≤5 files per phase**, one commit per phase, behavior-preserving.
- **Characterization net:** the `test_unit_network_coverage` suite stays green with no
  assertion edits (only call-site renames when a method moves).
- **Fast loop:** `pio test -e test_native --without-testing` to build, then
  `.pio/build/test_native/program --gtest_filter='<Suite>*'`. Never run the full (very slow)
  `test_routing` suite inside a red→green loop — only as a phase exit gate.
- **ESP32 gate:** `pio run -e esp32` each phase (catches Arduino/RadioLib-path breakage).
- New component suites live in `test/.../test_unit_<component>/` (PlatformIO needs a `test_`-
  prefixed leaf dir with a `test_unit.cpp` GoogleTest `main`).

## WS-0 — Architecture docs ✅
No code. Every claim validated against source.

## WS-1 — RadioLib module consolidation ✅
See `04-…`. `RadioLibModuleBase<RadioLibType>` + 2 family layers + 4 leaf specializations;
normalize CRLF→LF. ~1,350 L → ~610 L (base 472 L + four ~34 L leaves). **Gate:** native build +
`pio run -e esp32` (all four chips) + firmware-size check.

## WS-2 — Layering fixes ✅
- Move `ProtocolState` enum → `types/protocols/lora_mesh/protocol_state.hpp`; update
  `application_types.hpp` + `i_network_service.hpp` to include it.
- Move the subslot config types → `types/protocols/lora_mesh/subslot_config.hpp`; break
  `protocol_configuration.hpp`'s include of `services/subslot_scheduler.hpp`.
- **Gate:** full build + unit suite. Verify with `grep -rn '#include "protocols/' src/types/`
  returning nothing.

## WS-3 — Message serialization DRY
See `03-…`. Add `RequireField` helper → `HeaderSerializerMixin` CRTP → `MessageContainer`
base; migrate a few message types per phase. Add a Serialize↔Deserialize round-trip test per
type **before** refactoring it. **Gate:** `test/types/test_messages/*` + flash-size check.

## WS-4 — Memory: kill the copy chain
See `05-…`. (1) `EnqueueMessage` in-place helper, (2) span-backed typed payloads (audit
lifetimes), (3) serialize into the destination buffer, (4) span/const-ref getters. **Gate:**
unit nets + one slow forward-path run + a forwarded-bytes-unchanged test.

## WS-5 — NetworkService decomposition (7 phases; progress in the Status table)
See `02-…`. Facade stays; components are private members built with the closure-Host model.

1. **Dead code + constants** — remove `LinkQualityMetrics` +
   `NetworkService::CalculateComprehensiveLinkQuality` (⚠️ keep the actively-used
   `DistanceVectorRoutingTable::CalculateComprehensiveLinkQuality`), delete the 3 dead
   interface stubs, name magic numbers (`kUnassignedSlot=0xFF`, `kUint8Max=255`,
   `kVersionWrap=256`).
2. **Duplication helpers** — `EnqueueMessage`/`ForwardGeneric` (from WS-4),
   `IsDuplicateAndRecord`, `ClampSlot` (~31 sites), `ApplyRouteFromMessage`, `ExtractNmParams`,
   single `TransitionTo(state)`.
3. **Extract `ReliableMessaging`** — folds `reliable_dest_`/`group_windows_`/`groups_`;
   unit tests in `test/protocols/reliability/test_reliability/`. Gate also runs
   `test_routing/group_ack_test`.
4. **Extract `SlotScheduler`** — sole owner of `slot_table_`; two revertible commits
   (route-through-accessors, then move); decompose `UpdateSlotTable` into
   duty-plan/constraint-solve/fill/log; `test_unit_slot_scheduler/`. **Gate: full slow suite.**
5. **Extract `SyncBeaconService`** — client of SlotScheduler; gate `test_sync_beacon_subslot`.
   Transmit/forward is extracted; the remaining step moves `ProcessSyncBeacon` and the sync
   state into the component.
6. **Extract `JoinService`** — allocates via SlotScheduler; port `sponsor_based_join_test`.
7. **Extract `NmElectionService`** — `ApplyRoleChange` computes, coordinator commits via
   `apply_role_change` closure; gate `test_routing_nm_merge` + `test_routing_role_change`.

End-state: `NetworkService` ≈ 700 L coordinator + 5 components.

## WS-6 — Interface segregation (API change, last)
Split the 33-method `INetworkService` into `INodeService` / `IRoutingService` /
`INetworkDiscovery` / `ISuperframeIntegration` / `IProtocolState`; `NetworkService` implements
all. Update `LoRaMeshProtocol`, the integration fixture, and the test files referencing
`INetworkService::ProtocolState` (21 today). Keep the concrete `NetworkService*` accessor during
migration. **Gate:** full unit + integration suite.

## Deferred follow-ups (after WS-1..6)
- Split `lora_mesh_protocol.cpp` (1,683 L) → coordinator / state-machine / event-dispatcher.
- Split `protocol_configuration.hpp` (908 L) per protocol.
- Review `network_node_route.cpp` link-quality logic placement (types vs service layer).
- Unify the 5 facade callbacks into a single observer (optional).

## Final verification (end of programme)
Full suite green: `test_routing` + `test_tdma` + `test_sync_beacon_subslot` +
`test_routing_nm_merge` + `test_routing_role_change` + all unit suites, plus `pio run -e esp32`.
Confirm firmware RAM/flash is not worse than baseline.
