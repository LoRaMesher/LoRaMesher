# Decisions Log & Extraction Spec

> A record of every non-trivial decision taken during the refactor, plus the vetted,
> ready-to-execute specification for the remaining `NetworkService` extractions. The
> extraction spec is the synthesis of a two-agent design panel (a minimal-risk lens and a
> clean-architecture lens) whose independent designs converged. Read after `06-refactor-roadmap.md`.

## Part A — Decisions already made and shipped

Branch `refactor/architecture-review`. Each row links the decision to the commit that
encodes it.

| # | Decision | Rationale | Commit |
|---|---|---|---|
| D1 | Capture the whole review as validated docs *before* touching code | User asked for a reviewable, validated analysis; cheap to correct on paper | `6e45992` |
| D2 | Re-verify every agent claim against source (32/32) before acting | Agents can hallucinate metrics; the radio-dup `diff` was initially masked by CRLF + chip-name token | (WS-0 validation) |
| D3 | Collapse the 4 radio drivers with a **template base + 2 family layers + 4 thin leaves**, keeping the `LoraMesherSX12xx` type names | Within-family files are byte-identical; only `begin()`/CRC/current-limit differ across families; preserving type names keeps `radiolib_radio.cpp` untouched | `02cf488` |
| D4 | Gate WS-1 on **esp32 compile reaching the link stage**, accepting the pre-existing `setup()/loop()` link failure | esp32 is a compile-only env (needs an example sketch to link); baseline fails identically, so "reached Linking" == library compiles | `02cf488` |
| D5 | Fix layering by **relocating value types down into `types/`** and leaving `using` aliases behind (`INetworkService::ProtocolState`, `protocols::lora_mesh::Subslot*`) | Removes the two upward includes with zero churn across the 23 + 8 reference sites; enum/struct definitions byte-identical so behavior is preserved | `7817a24` |
| D6 | Keep the enum's **default underlying type** (do not add `: uint8_t`) when relocating `ProtocolState` | Changing the underlying type is an observable change; behavior-preservation forbids it | `7817a24` |
| D7 | Delete `NetworkService::CalculateComprehensiveLinkQuality` but **keep** `DistanceVectorRoutingTable::CalculateComprehensiveLinkQuality` | Same name, different class; only the NetworkService copy is dead (no callers) — the routing-table one is actively used | `f622d0d` |
| D8 | Defer the "named constants" sub-task of WS-5 ph.1 into the later per-extraction phases | `0xFF`/`255`/`256` literals carry different meanings across 4,700 lines; a blind sweep risks semantic bugs — name each where its scope is unambiguous | `f622d0d` |
| D9 | Introduce `EnqueueForTransmission(slot, msg)` and convert only the **10 uniform** send/forward sites | The non-uniform sites (conditional build, the returning factory, the pre-built sync beacon) would force awkward signatures; partial conversion is still a clean win and the shared seam for WS-4 | `7c5e9b3` |
| D10 | Validate fast iterations by **building once then running the binary directly** | `pio test` occasionally errors in its run phase transiently; the binary run is deterministic. The full `test_routing` suite (~95 min) is reserved for extraction exit gates only | (process) |
| D11 | Commit the refactor on a **dedicated branch**, staging only refactor files | The working tree carries ~50 unrelated WIP items; isolated diffs keep each phase revertible | (all) |

## Part B — Resolved design questions (the panel's open points)

| Q | Options weighed | Resolution |
|---|---|---|
| Should extracted components hold their own mutex? | (a) per-component `mutex` (minimal-risk lens) vs (b) none — rely on the coordinator | **(b) none.** The protocol runs on a single task; existing state like `slot_table_dirty_` is already documented as "protocol-task only, no sync needed." A second mutex would invite the lock-order inversion the minimal-risk lens itself flagged (ReliableDelivery `Tick()` → `send_attempt` → enqueue). Components inherit the coordinator's single-threading. |
| Extraction order | both lenses | **ReliableMessaging → SlotScheduler → SyncBeaconService → JoinService → NmElectionService.** SlotScheduler must precede sync/join because it becomes the sole owner of `slot_table_`, which those two currently write. |
| How to move `slot_table_` safely | both lenses | **Two commits:** (1) route every `slot_table_` access through accessors while it still lives in NetworkService (behavior-preserving indirection, fast-gate); (2) move the array + `UpdateSlotTable` into `SlotScheduler` (slow-gate). |
| Where canonical state lives | both lenses | **Coordinator owns** `state_`, `network_manager_`, `network_id_`, `is_synchronized_`, `node_address_`, `message_seq_`. Components **read** via a const-ref context struct and **mutate** only via closures (`transition_to`, `apply_role_change`, `set_synchronized`, `next_seq`). |
| Inbound reliable-data receive path | minimal-risk lens | **Stays in `NetworkService::ProcessDataMessage`** (it owns next-hop routing + app delivery); only ACK and GROUP messages route into `ReliableMessaging`. |

## Part C — Validation-cost reality (why the big extractions are dedicated sessions)

The fast characterization net (`test_unit_network_coverage`, ~2 min, 115 runnable cases) catches
behavioral regressions in discovery/join/normal-op. But **`SlotScheduler` Commit 2 is
data-structural** — an off-by-one in slot allocation only surfaces under multi-hop integration,
which is the **`test_routing` suite (~95 min)**. That gate cannot be run many times in one
sitting. Therefore:

- **Incremental, fast-gate-only:** ReliableMessaging; SlotScheduler Commit 1 (accessors);
  SyncBeaconService; JoinService; NmElectionService.
- **Dedicated, slow-gate-blocking session:** SlotScheduler Commit 2 (the array move). Do not
  proceed to SyncBeacon/Join extraction until its slow gate is green.

## Part D — Executable extraction spec (next phases)

The closure-`Host` model from `reliability::ReliableDelivery` applies throughout. All components
are private members of `NetworkService`; the public API + `GetNetworkServiceForTest()` stay frozen
until WS-6.

### D.1 ReliableMessaging (WS-5 ph.3 — do first, LOW risk)
New `services/reliable_messaging.{hpp,cpp}`. Owns `reliable_` (ReliableDelivery),
`reliable_dest_` shadow, `group_windows_`, `groups_`/`group_count_`, `delivery_callback_`,
`data_received_ex_callback_`. Methods moved: `SendReliable`, `SendReliableAttempt`,
`ProcessAckMessage`, `EnqueueAck`, `OnReliableOutcome`, `BuildReliableHost`,
`ComputeReliableTimeout`, `Lookup/Record/ClearReliableDest`, `Join/Leave/IsMemberOf/GetGroups`,
`SendGroup`, `SendGroupReliable`, `ProcessGroupMessage`, `ForwardGroupMessage`,
`CloseExpiredGroupWindows`, `HopsFromTtl`, `Set{Delivery,DataReceivedEx}Callback`,
`ProcessReliableTimers`.

Closures supplied by the coordinator: `now_ms`, `send_attempt` (Host); plus
`enqueue_for_transmission` (reuse `EnqueueForTransmission`), `find_next_hop`, `compute_timeout`,
`deliver_to_app`, `next_seq` (→ `++message_seq_`).

Commit sequence (each fast-gate green): (A) header + member + build stub; (B) move group
membership; (C) move `reliable_dest_` shadow; (D) move `SendReliable`/`SendReliableAttempt` +
wire Host; (E) move ACK path (`ProcessAckMessage`/`EnqueueAck`/`OnReliableOutcome` +
`group_windows_`); (F) move group send/receive; (G) move tick + cleanup. Final gate also runs
`test_routing/group_ack_test`.

### D.2 SlotScheduler (WS-5 ph.4 — LINCHPIN, dedicated session)
New `services/slot_scheduler.{hpp,cpp}`. Pass a const-ref `SlotSchedulerContext`
(node_address, network_manager, state, current_network_depth, beacon_node_count,
number_of_slots_per_superframe, my_control_slot_index, allocated_control/discovery_slots,
local_allocated_data_slots, target_duty_cycle, min_sleep_fraction, churn_margin_slots,
ewma_alpha_fixed). Host closures: `get_routing_nodes`, `now_ms`, `get_superframe_duration`,
`get_hop_distance_to_nm`, `notify_superframe`.

Accessor API the component must expose so sync/join/data become pure clients:
`UpdateSlotTableIfDirty(ctx, force)`, `MarkDirty()`, `GetSlotTable()`/`GetSlotCount()`,
`SetSlotType(i, type)`, `SetSlotTarget(i, addr)`, `SetDiscoverySlots()`, `SetJoiningSlots(ctx)`,
`ExpandSyncBeaconListening()`, `RestoreSyncBeaconTxSlot()`, `ScheduleDiscoverySlotForwarding()`.

Commit 1 (fast-gate): add those accessors **inside NetworkService**, rewrite every
`slot_table_[...]` mutation and every `slot_table_dirty_ = true` to call them, rename
`UpdateSlotTable` → `UpdateSlotTable_Impl` behind `UpdateSlotTableIfDirty`. Commit 2 (slow-gate):
move the array + methods + slot-only members into `SlotScheduler`; NetworkService builds the
context and delegates.

### D.3 SyncBeaconService / JoinService / NmElectionService (WS-5 ph.5–7)
Clients of SlotScheduler. SyncBeacon owns `last_sync_*`, `no_received_sync_beacon_count_`,
`beacon_node_count_`, `current_network_depth_`, `table_version_`, `my_control_slot_index_`.
Join owns `pending_joins_`, `selected_sponsor_`, join backoff, `local_capabilities_`. NmElection
owns the election timers/priority and **computes** role changes that the coordinator commits via
`apply_role_change` (single validated mutation point). Gates: SyncBeacon →
`test_sync_beacon_subslot`; Join → join integration paths; NmElection →
`test_routing_nm_merge` + `test_routing_role_change`.

## Part E — Status snapshot (2026-06)

Shipped on `refactor/architecture-review`: WS-0 (docs), WS-1 (radio), WS-2 (layering),
WS-5 ph.1 (dead code), WS-5 ph.2 (`EnqueueForTransmission`), and the first two steps of the
ReliableMessaging extraction — **ph.3a** (component scaffolding + group membership) and
**ph.3b** (the `reliable_dest_` shadow table). Both were the closure-free pieces; they prove
the component wiring (owned `unique_ptr`, coordinator-mutex reference, GLOB-picked-up build).

**ph.3c DONE** (commits b1569ed, cc4b602): the closure-wired core is extracted —
`ReliableMessaging` now owns the `reliability::ReliableDelivery` machine, ack windows, and the
send/ACK/group-send logic, driven by a `Host` struct of ~12 closures. Validated by
`test_unit_network_coverage` (115/116) + all 8 `GroupAckTests`. NetworkService 4713→4361 L.
What still lives in NetworkService (the group *receive* path, entangled with the dedup cache +
app-delivery callbacks — an optional follow-up): `SendGroup` (non-reliable), `ProcessGroupMessage`,
`ForwardGroupMessage`, `DeliverToApp`, `HopsFromTtl`. **The biggest remaining reduction is
`SlotScheduler` (ph.4).** The validation method is now proven cheap: build the slow suite once
with `--without-testing`, then filter-run the relevant tests on the prebuilt binary.

(Superseded note) The original ph.3c plan was to move `reliable_`
(the ReliableDelivery member) + `BuildReliableHost`, `SendReliable`/`SendReliableAttempt`,
`ProcessAckMessage`/`EnqueueAck`/`OnReliableOutcome`, `ComputeReliableTimeout`, `HopsFromTtl`,
`DeliverToApp`, the group send/receive (`SendGroup`/`SendGroupReliable`/`ProcessGroupMessage`/
`ForwardGroupMessage`), `group_windows_`/`CloseExpiredGroupWindows`, and the
`delivery_callback_`/`data_received_ex_callback_`. This step wires the ~6 closures
(`now_ms`, `send_attempt`, `enqueue_for_transmission`=`EnqueueForTransmission`, `find_next_hop`,
`compute_timeout`, `deliver_to_app`, `next_seq`) per §D.1; it is the large, cohesive unit best
done in one focused pass. Gate: `test_unit_network_coverage` + `group_ack_test` (run via
`--gtest_filter` on the prebuilt routing binary — fast).

Then WS-5 ph.4–7 (SlotScheduler → Sync/Join/NmElection), WS-3 (message CRTP), WS-4 (memory),
WS-6 (interface segregation), and the deferred `lora_mesh_protocol.cpp` /
`protocol_configuration.hpp` splits.
