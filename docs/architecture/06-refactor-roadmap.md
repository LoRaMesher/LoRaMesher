# Refactor Roadmap

> The sequenced, test-first execution plan. Each workstream is independently shippable;
> phases inside a workstream are ≤5 files and committed individually. Low-risk, isolated wins
> come first; the timing-sensitive god-class decomposition comes after the base is clean.

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

**If effort is limited:** WS-1 → WS-5 phases 1–2 + 4 (SlotScheduler) → WS-4. That captures
most of the cleanliness/memory/maintainability win.

## Universal rules (every phase)

- **Test-first:** write the failing test, confirm it fails *for the right reason*, implement,
  go green. (CLAUDE.md mandate.)
- **≤5 files per phase**, one commit per phase, behavior-preserving.
- **Characterization net:** the 118 `test_unit_network_coverage` cases stay green with no
  assertion edits (only call-site renames when a method moves).
- **Fast loop:** `pio test -e test_native --without-testing` to build, then
  `.pio/build/test_native/program --gtest_filter='<Suite>*'`. Never run the ~95-min
  `test_routing` suite inside a red→green loop — only as a phase exit gate.
- **ESP32 gate:** `pio run -e esp32` each phase (catches Arduino/RadioLib-path breakage).
- New component suites live in `test/.../test_unit_<component>/` (PlatformIO needs a `test_`-
  prefixed leaf dir with a `test_unit.cpp` GoogleTest `main`).

## WS-0 — Architecture docs ✅ (this set)
No code. Validate every claim against source; have a review agent read for consistency.

## WS-1 — RadioLib module consolidation
See `04-…`. `RadioLibModuleBase<RadioLibType>` + 2 family layers + 4 leaf specializations;
normalize CRLF→LF. ~1,350 L → ~350 L. **Gate:** native build + `pio run -e esp32` (all four
chips) + firmware-size check.

## WS-2 — Layering fixes
- Move `ProtocolState` enum → `types/protocols/lora_mesh/protocol_state.hpp`; update
  `application_types.hpp` + `i_network_service.hpp` to include it.
- Move `SubslotSchedulerConfig` → `types/configurations/`; break
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

## WS-5 — NetworkService decomposition (8 phases)
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
   new `test_unit_reliable_messaging/`. Gate also runs `test_routing/group_ack_test`.
4. **Extract `SlotScheduler`** — sole owner of `slot_table_`; two revertible commits
   (route-through-accessors, then move); decompose `UpdateSlotTable` into
   duty-plan/constraint-solve/fill/log; `test_unit_slot_scheduler/`. **Gate: full slow suite.**
5. **Extract `SyncBeaconService`** — client of SlotScheduler; gate `test_sync_beacon_subslot`.
6. **Extract `JoinService`** — allocates via SlotScheduler; port `sponsor_based_join_test`.
7. **Extract `NmElectionService`** — `ApplyRoleChange` computes, coordinator commits via
   `apply_role_change` closure; gate `test_routing_nm_merge` + `test_routing_role_change`.

End-state: `NetworkService` ≈ 700 L coordinator + 5 components.

## WS-6 — Interface segregation (API change, last)
Split the 82-method `INetworkService` into `INodeService` / `IRoutingService` /
`INetworkDiscovery` / `ISuperframeIntegration` / `IProtocolState`; `NetworkService` implements
all. Update `LoRaMeshProtocol`, the integration fixture, and the ~20 test files referencing
`INetworkService::ProtocolState`. Keep the concrete `NetworkService*` accessor during
migration. **Gate:** full unit + integration suite.

## Deferred follow-ups (after WS-1..6)
- Split `lora_mesh_protocol.cpp` (1,696 L) → coordinator / state-machine / event-dispatcher.
- Split `protocol_configuration.hpp` (909 L) per protocol.
- Review `network_node_route.cpp` link-quality logic placement (types vs service layer).
- Unify the 5 facade callbacks into a single observer (optional).

## Pre-flight
The working tree is currently dirty (branch `new_loramesher`, many modified/untracked files).
Commit or stash unrelated changes first so each phase is an isolated, revertible diff.

## Final verification (end of programme)
Full suite green: `test_routing` + `test_tdma` + `test_sync_beacon_subslot` +
`test_routing_nm_merge` + `test_routing_role_change` + all unit suites, plus `pio run -e esp32`.
Confirm firmware RAM/flash is not worse than baseline.
