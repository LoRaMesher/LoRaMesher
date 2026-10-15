# NetworkService Decomposition

> When the review started, `network_service.cpp` was **4,713 lines** (`.hpp` 1,512). It is the
> coordinator of the mesh, but it also *implemented* nine distinct responsibilities. This doc
> inventories them, defines what becomes its own component, and — critically — specifies who
> owns which state so the split preserves behavior. Drives WS-5 in the roadmap.
>
> Today `network_service.cpp` is 3,724 lines (`.hpp` 1,511): responsibilities 1 and 8 are
> extracted, 2 is partly extracted. The inventory below is the baseline; per-phase progress is
> in the Status table of `06-refactor-roadmap.md`.

## Why decompose

- No single method can be reasoned about in isolation: `slot_table_` is mutated by slot
  scheduling **and** sync-beacon handling **and** join handling.
- Three methods exceeded 200 lines (`UpdateSlotTable` 276, `ProcessSyncBeacon` 280,
  `ProcessJoinRequest` 209) — untestable as units. `UpdateSlotTable` has since been decomposed
  inside `SlotScheduler`; `ProcessSyncBeacon` (287 L) and `ProcessJoinRequest` (206 L) remain.
- Every behavioral fix risks an unrelated subsystem.

## The nine responsibilities (baseline)

| # | Responsibility | ~LOC | Largest methods | State it owns |
|---|---|---|---|---|
| 1 | **Slot / TDMA scheduling** | 490 | `UpdateSlotTable`(276), `LogSlotTable`(103), `SetJoiningSlots`(91), `SetDiscoverySlots` | `slot_table_`, `slot_count_`, `slot_table_dirty_`, `pending_slot_table_rebuild_`, `allocated_control_slots_`, `allocated_discovery_slots_`, `local_allocated_data_slots_`, `number_of_slots_per_superframe_`, `churn_margin_slots_`, `target_duty_cycle_`, `min_sleep_fraction_` |
| 2 | **Sync beacon** | 539 | `ProcessSyncBeacon`(280), `HandleSuperframeStart`(127), `Send/ForwardSyncBeacon` | `last_sync_time_`, `last_sync_beacon_received_`, `no_received_sync_beacon_count_`, `beacon_node_count_`, `current_network_depth_`, `table_version_`, `my_control_slot_index_` |
| 3 | **Join protocol** | 614 | `ProcessJoinRequest`(209), `ProcessJoinResponse`(119), `ForwardJoinResponseToSponsoredNode`(82), `ApplyPendingJoin` | `pending_joins_`, `selected_sponsor_`, `joining_start_time_`, `join_retry_count_`, `join_backoff_remaining_`, `local_capabilities_` |
| 4 | **NM election / merge** | 180 | `ProcessNMClaim`(82), `ApplyRoleChange`(81), `StartElectionBackoff` | `election_end_time_`, `election_priority_`, `nm_election_start_time_`, `surrendered_in_election_`, `surrender_discovery_retries_` |
| 5 | **Routing glue** | 232 | `ProcessRoutingTableMessage`(99), `FindNextHop`(95) | (delegates to `IRoutingTable`) |
| 6 | **Data flow** | 225 | `ProcessDataMessage`(97), `SendData`(81), `ForwardDataMessage` | message cache |
| 7 | **Broadcast / multicast** | 211 | `ProcessGroupMessage`(62), `SendGroupReliable`(64), broadcast send/process/forward | message cache, group membership |
| 8 | **Reliable delivery adapter** | 191 | `SendReliableAttempt`, `ProcessAckMessage`, `EnqueueAck`, `OnReliableOutcome` | `reliable_`, `reliable_dest_` (shadow), `group_windows_`, `groups_`, `group_count_`, `delivery_callback_` |
| 9 | **Link quality** | 72 | `CalculateLinkStability`, `CalculateTimeOnAir`, … | (stateless; + dead `LinkQualityMetrics` / `NetworkService::CalculateComprehensiveLinkQuality`, since removed; `CalculateLinkStability` has no callers and is still present) |

## State ownership model (the key to a safe split)

Two categories of member state:

### A. Canonical/global — STAYS in the coordinator
`state_`, `network_manager_`, `network_id_`, `is_synchronized_`, `network_found_`,
`network_creator_`, `node_address_`, `message_seq_`.

These are read by nearly every responsibility and define the validated state machine.
Components **read** them through a small const-ref *context struct* passed per call, and
**mutate** them only through injected closures so the mutation stays in one validated place:

- `transition_to(ProtocolState)` — the single state-transition point.
- `apply_role_change(RoleChange)` — election decides, coordinator commits role/state.
- `set_synchronized(bool)`.
- `next_seq() -> uint8_t` — shared sequence counter (data + broadcast both use it).

### B. Single-responsibility — MOVES to the new owner
Each component takes the "State it owns" column above wholesale.

**`slot_table_` is the ordering hazard.** It is written by responsibilities 1, 2, and 3.
Resolution: extract `SlotScheduler` (resp. 1) **first** and make it the *sole owner*; sync
and join code is rewritten to call its accessors *before* those responsibilities are
themselves extracted. This is why the phase order below is not arbitrary.

## Target component set

All follow the `reliability::ReliableDelivery` model: a `Host` of closures + a config
struct, no heap, no virtual dispatch. All are **private members of `NetworkService`**, which
remains the facade.

| Component | Files (`services/`) | Wraps responsibility | Depends on | State |
|---|---|---|---|---|
| `ReliableMessaging` | `reliable_messaging.{hpp,cpp}` | 8 (+ group receive from 7) | `Host` closures (send attempt, enqueue, next hop, deliver to app, …) | ✅ exists |
| `SlotScheduler` | `slot_scheduler.{hpp,cpp}` | 1 | per-call `Context` snapshot + `Host` closures (routing nodes, hop distance, slot duration, NM TX time, notify superframe) | ✅ exists |
| `SyncBeaconService` | `sync_beacon_service.{hpp,cpp}` | 2 | per-call `Context` snapshot, superframe + queue services, `Host` | 🟡 transmit/forward only |
| `JoinService` | `join_service.{hpp,cpp}` | 3 | `SlotScheduler&`, `enqueue`, `transition_to`, NM-param helper, context | ⬜ planned |
| `NmElectionService` | `nm_election_service.{hpp,cpp}` | 4 | `enqueue`, `apply_role_change`, context | ⬜ planned |

`MessageCache` (`services/message_cache.hpp`) is a small shared helper rather than a
responsibility component: it owns the per-node sequence counter and the `(source, seq)` dedup
cache used by the data, broadcast and group paths.

Responsibilities 5/6/7 (routing/data/broadcast glue) and 9 (stateless link-quality) **stay**
as thin coordinator methods once the Phase-2 helpers and `ReliableMessaging` absorb their
duplication. Re-evaluate only if they re-bloat.

## Phase order (detail in `06-refactor-roadmap.md`, WS-5)

1. Dead code + named constants (remove dead stubs + `LinkQualityMetrics` +
   `NetworkService::CalculateComprehensiveLinkQuality` — **not** the actively-used
   `DistanceVectorRoutingTable::CalculateComprehensiveLinkQuality`; name magic numbers).
2. Collapse duplication into private helpers — creates the seams the extractions lift through.
3. Extract `ReliableMessaging` (cleanest seam — proves the recipe).
4. Extract `SlotScheduler` (linchpin — sole owner of `slot_table_`; decompose `UpdateSlotTable`).
5. Extract `SyncBeaconService`.
6. Extract `JoinService`.
7. Extract `NmElectionService`.

Each extraction phase is test-first with unit tests for the new component
(`test_unit_slot_scheduler/`; `ReliableMessaging` tests live in
`test/protocols/reliability/test_reliability/`), and re-runs the `test_unit_network_coverage`
characterization suite to prove delegation is behavior-preserving. End-state: `NetworkService`
≈ 700 L coordinator + 5 focused components.

## Validation
- Baseline method/line figures from direct inspection of `network_service.cpp` at 4,713 L;
  current figures from `wc -l` and the method extents in today's file.
- Dead-code claims confirmed by grep. The three `i_*_service.hpp` stubs and
  `NetworkService::CalculateComprehensiveLinkQuality` are deleted. Still present and dead or
  no-op: `SlotTableToSuperframe`, `UpdateNetworkTopology`, `NotifySuperframeOfNetworkChanges`,
  `CalculateLinkStability`, `SendSlotRequest`, and the commented-out bodies of
  `ProcessSlotAllocation` / `BroadcastSlotAllocation`.
