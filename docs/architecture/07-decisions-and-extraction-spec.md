# Decisions Log & Extraction Spec

> A record of every non-trivial decision taken during the refactor, plus the specification
> for the `NetworkService` extractions. The spec reconciles two independent designs — one
> optimised for minimal risk, one for clean architecture — which converged. Read after
> `06-refactor-roadmap.md`; current progress is tracked only in the **Status** table there.

## Part A — Decisions made and shipped

Each row links the decision to the commit that encodes it.

| # | Decision | Rationale | Commit |
|---|---|---|---|
| D1 | Capture the whole review as validated docs *before* touching code | A reviewable analysis is cheap to correct on paper | `6e45992` |
| D2 | Re-verify every quantitative claim against source before acting | Metrics can be wrong; the radio-dup `diff` was initially masked by CRLF + chip-name token | (WS-0 validation) |
| D3 | Collapse the 4 radio drivers with a **template base + 2 family layers + 4 thin leaves**, keeping the `LoraMesherSX12xx` type names | Within-family files are byte-identical; only `begin()`/CRC/current-limit differ across families; preserving type names keeps `radiolib_radio.cpp` untouched | `02cf488` |
| D4 | Gate WS-1 on **esp32 compile reaching the link stage**, accepting the pre-existing `setup()/loop()` link failure | esp32 is a compile-only env (needs an example sketch to link); baseline fails identically, so "reached Linking" == library compiles | `02cf488` |
| D5 | Fix layering by **relocating value types down into `types/`** and leaving `using` aliases behind (`INetworkService::ProtocolState`, `protocols::lora_mesh::Subslot*`) | Removes the two upward includes with zero churn across the 23 + 8 reference sites; enum/struct definitions byte-identical so behavior is preserved | `7817a24` |
| D6 | Keep the enum's **default underlying type** (do not add `: uint8_t`) when relocating `ProtocolState` | Changing the underlying type is an observable change; behavior-preservation forbids it | `7817a24` |
| D7 | Delete `NetworkService::CalculateComprehensiveLinkQuality` but **keep** `DistanceVectorRoutingTable::CalculateComprehensiveLinkQuality` | Same name, different class; only the NetworkService copy is dead (no callers) — the routing-table one is actively used | `f622d0d` |
| D8 | Defer the "named constants" sub-task of WS-5 ph.1 into the later per-extraction phases | `0xFF`/`255`/`256` literals carry different meanings across 4,700 lines; a blind sweep risks semantic bugs — name each where its scope is unambiguous | `f622d0d` |
| D9 | Introduce `EnqueueForTransmission(slot, msg)` and convert only the **10 uniform** send/forward sites | The non-uniform sites (conditional build, the returning factory, the pre-built sync beacon) would force awkward signatures; partial conversion is still a clean win and the shared seam for WS-4 | `7c5e9b3` |
| D10 | Validate fast iterations by **building once then running the binary directly** | `pio test` occasionally errors in its run phase transiently; the binary run is deterministic. The full `test_routing` suite (very slow) is reserved for extraction exit gates only | (process) |
| D11 | Keep each refactor phase an **isolated commit** containing only refactor files | Isolated diffs keep each phase revertible and separate structural from behavioral changes | (all) |
| D12 | `SlotScheduler` reads coordinator state through a per-call `Context` snapshot (`NetworkService::MakeSlotContext`) and guards the slot table with an **internal, innermost mutex** (Host closures are never called while it is held) | The snapshot keeps the component free of back-references. The table is rebuilt on the protocol task but read from the application thread, so the initial lock-free design was replaced by an internal lock | `e0720eb` + later |
| D13 | `GetHopDistanceToNM` stays in `NetworkService` and is injected into `SlotScheduler` as a `Host` closure | It is a routing-table query, not slot-table state | `e0720eb` |
| D14 | Extract `SyncBeaconService` transmit/forward first, as a **stateless** component (`Context` snapshot + `Host` closures); the receive path follows separately | Send/forward has a clean seam; `ProcessSyncBeacon` is entangled with timing sync, join and election state | — |

## Part B — Resolved design questions

| Q | Options weighed | Resolution |
|---|---|---|
| Should extracted components hold their own mutex? | (a) per-component `mutex` (minimal-risk design) vs (b) none — rely on the coordinator | **(b) none** for protocol-task-only components. The protocol runs on a single task; a second mutex would invite a lock-order inversion (ReliableDelivery `Tick()` → `send_attempt` → enqueue). Exception: components whose state is also reached from the application thread — `ReliableMessaging` (send paths), `MessageCache` (sequence counter), `SlotScheduler` (slot-table reads) — synchronise internally, each with a documented lock order (`SlotScheduler`'s lock is innermost; `ReliableMessaging`'s is outermost relative to the host's locks). |
| Extraction order | both designs | **ReliableMessaging → SlotScheduler → SyncBeaconService → JoinService → NmElectionService.** SlotScheduler must precede sync/join because it becomes the sole owner of `slot_table_`, which those two currently write. |
| How to move `slot_table_` safely | both designs | **Two commits:** (1) route every `slot_table_` access through accessors while it still lives in NetworkService (behavior-preserving indirection, fast-gate); (2) move the array + `UpdateSlotTable` into `SlotScheduler` (slow-gate). |
| Where canonical state lives | both designs | **Coordinator owns** `state_`, `network_manager_`, `network_id_`, `is_synchronized_`, `node_address_`, `message_seq_`. Components **read** via a const-ref context struct and **mutate** only via closures (`transition_to`, `apply_role_change`, `set_synchronized`, `next_seq`). |
| Inbound reliable-data receive path | minimal-risk design | **Stays in `NetworkService::ProcessDataMessage`** (it owns next-hop routing + app delivery); only ACK and GROUP messages route into `ReliableMessaging`. |

## Part C — Validation cost

The fast characterization net (`test_unit_network_coverage`, a few minutes) catches
behavioral regressions in discovery/join/normal-op. But **`SlotScheduler` Commit 2 is
data-structural** — an off-by-one in slot allocation only surfaces under multi-hop integration,
which is the slow **`test_routing` suite**. That gate is too slow to run inside an iteration
loop. Therefore:

- **Incremental, fast-gate-only:** ReliableMessaging; SlotScheduler Commit 1 (accessors);
  SyncBeaconService; JoinService; NmElectionService.
- **Slow-gate-blocking:** SlotScheduler Commit 2 (the array move). Sync/Join extraction
  proceeds only once its slow gate is green.

## Part D — Extraction spec

The closure-`Host` model from `reliability::ReliableDelivery` applies throughout. All components
are private members of `NetworkService`; the public API + `GetNetworkServiceForTest()` stay frozen
until WS-6.

### D.1 ReliableMessaging (WS-5 ph.3 — implemented)
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

As implemented, the group receive/forward path (`ProcessGroupMessage`, `ForwardGroupMessage`)
also moved; `DeliverToApp` and `HopsFromTtl` stay in `NetworkService` because the data and
broadcast paths share them. Sequence numbers come from `MessageCache::NextSeq()`.

### D.2 SlotScheduler (WS-5 ph.4 — implemented)
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

As implemented (see D12/D13): `SlotScheduler` owns `slot_table_`, `slot_count_`,
`slot_table_dirty_` and `allocated_control/discovery_slots_`; `UpdateSlotTable` became
`RebuildSlotTable` over `ComputeBandSizes` / `FillSlotTableLocked` / `LogSlotTable`. `Host`
closures: `get_routing_nodes`, `get_hop_distance_to_nm`, `get_slot_duration`,
`calculate_nm_tx_time`, `notify_superframe`. Duty-cycle parameters (`target_duty_cycle`,
`min_sleep_fraction`, `churn_margin_slots`) and the data-band inputs (`default_data_slots`,
`max_data_slots`) are passed in the `Context`; the dirty flag is atomic so other threads can
mark the table for rebuild.

### D.3 SyncBeaconService / JoinService / NmElectionService (WS-5 ph.5–7)

`SyncBeaconService` exists for transmit/forward only (D14); the rest of this section is the
remaining spec.
Clients of SlotScheduler. SyncBeacon owns `last_sync_*`, `no_received_sync_beacon_count_`,
`beacon_node_count_`, `current_network_depth_`, `table_version_`, `my_control_slot_index_`.
Join owns `pending_joins_`, `selected_sponsor_`, join backoff, `local_capabilities_`. NmElection
owns the election timers/priority and **computes** role changes that the coordinator commits via
`apply_role_change` (single validated mutation point). Gates: SyncBeacon →
`test_sync_beacon_subslot`; Join → join integration paths; NmElection →
`test_routing_nm_merge` + `test_routing_role_change`.

## Part E — Status

Progress is tracked in the **Status** table of `06-refactor-roadmap.md` (single source of
truth). This document records decisions and the spec only.
