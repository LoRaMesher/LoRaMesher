# Load-Aware Data-Slot Allocation — Design Rationale

Status: **evaluated and rejected (2026-09-23).** The fan-in redistribution
machinery was removed. The only change retained is the uniform baseline:
`default_data_slots` 1→2 and `max_data_slots` 50→100, which alone keeps relay
queues bounded (see "The decisive evidence" in
`SESSION_HANDOFF_slot_allocation.md` §4). This document is kept as the record of
the approaches that were tried and why they were rejected. The remaining 1-hop
loss turned out to be a slot-table alignment problem, not a capacity problem —
see `delivery_packet_loss_analysis.md`. PROTOCOL_SPEC §10.5.1 (Dynamic Slot
Allocation) stays in the planned section.

## Problem

TDMA superframes give every node a flat `default_data_slots` (originally 1) data
TX slot. On multi-hop topologies a **relay** must forward traffic for everything
downstream of it while still holding only its own slots, so its forwarding TX
queue saturates and multi-hop delivery collapses.

Measured in the 25-node stress harness (`test_network_stress`):
- Flat allocation, 10 nodes: reliable-unicast PDR ~7%, group-ACK ~23%, local
  1-hop non-reliable ~87%, backbone relay TX queue persistently saturated (~9/10).
- Flat allocation, 25 nodes: routing does not even fully converge.

The failure is **structural** (relays starved of TX capacity), not a radio or
routing-quality problem.

## Hard constraint that shapes every decision

Every node independently builds an **identical** copy of the slot table from the
per-node `allocated_data_slots` values that propagate network-wide (routing-table
entries + sync beacon). TDMA only works if all nodes compute the *same* layout —
otherwise a node transmits in a slot where its neighbour is not listening and the
packet is lost.

Corollary: **any change to a node's allocation reshapes the layout** and must
re-propagate (~network diameter superframes) before peers realign. Frequent or
oscillating changes ⇒ permanent misalignment ⇒ collapse. Stability is paramount.

## Approaches tried, and why they were rejected

A design panel (three independent proposals + a consensus) evaluated three
families. The empirical results below drove the decision.

1. **Traffic-driven self-allocation, re-evaluated every superframe with
   ramp-up/decay.** Each node counts its own forwards and adapts its slot count.
   REJECTED — measured: drove reliable delivery to **0%**, *worse than flat*,
   purely from slot-table churn (the allocation never stopped changing, so the
   layout never stabilised).

2. **Same, but grow-only (monotonic high-water mark) + measurement warmup.** Only
   ratchets up, never decays, so it converges. REJECTED — still net-harmful:
   dropped 1-hop non-reliable 87%→51% (residual churn during growth events), never
   drained the relay queue, and permanently inflates the frame because it never
   releases. Grow-only is really an admission that the raw signal churns.

3. **NM-central authoritative allocation.** The Network Manager computes every
   node's slots from its global routing view and dictates them. REJECTED by the
   consensus as more machinery than needed: it still must propagate to every node
   for RX alignment (central computation removes *divergence*, not *latency*),
   adds an adoption round-trip, and hard-couples correctness to NM reachability
   and NM-election churn.

## Chosen design

**Topology-derived, single-authored allocation.** Two independent parts:

### 1. What each node requests (the per-node byte)

A node's `local_allocated_data_slots_` is a function of its **fan-in** — a stable
topology signal, NOT live traffic:

```
fanin(X)  = number of routes, advertised by X's neighbours in their routing
            broadcasts, whose next_hop == X AND destination != X
            (X's downstream relay burden; the destination!=X guard excludes a
             neighbour's route TO X itself, which is not forwarding burden)
target(X) = clamp(ORIGINATION_FLOOR + FanInSlotBonus(fanin), ORIGINATION_FLOOR, cap)
ORIGINATION_FLOOR = 1   (per-node minimum; deliberately NOT default_data_slots)
FanInSlotBonus: 0→0, 1-2→+1, 3-5→+3, 6-10→+7, ≥11→+11   (coarse buckets)
cap       = min(kMaxDataSlotsPerNode=12, max_data_slots)
```

Critical: the per-node **origination floor is 1**, NOT `default_data_slots`.
`default_data_slots` is used ONLY as the pool multiplier when sizing the data
band (`base * N` slots, below). If the floor equalled `default_data_slots`, the
`N` per-node floors would sum to the entire pool (`base * N`), leaving **zero
surplus** — every node would get exactly `base` and a relay could never exceed a
leaf. With floor = 1, the `N` floors consume `N` slots and the remaining
`(base-1) * N` surplus is what the round-robin hands to relays by fan-in.

Why this specific design:
- **Single author.** X computes and advertises one byte about *itself*; every
  other node only *copies* it. A value with one writer is consistent by
  construction — it is never recomputed across nodes, so nodes cannot disagree.
  This is the same invariant the flat scheme already relied on.
- **Topology, not traffic.** Fan-in derives from routing broadcasts (control
  slots), which happen every superframe regardless of data traffic. It changes
  only when the topology changes, so the churn pathology of approaches 1–2 is
  definitionally impossible.
- **Fan-in is locally computable.** `RoutingTableEntry.next_hop` is already on the
  wire. A node tallies, per superframe window, how many received entries name it
  as next_hop (implemented in `ProcessRoutingTableMessage`). Routing tables are
  only broadcast by direct neighbours, so these entries measure exactly the
  traffic that would be handed to the node to forward. (Accurate while tables are
  not sliced, i.e. ≲24 nodes; for larger networks it under-counts, degrading
  safely toward flat.)
- **Coarse buckets** mean a warming relay crosses ~3 thresholds instead of a
  dozen, minimising the number of reshape events.
- **Hold-down debounce** (`kDataSlotHoldDownSuperframes`, ~5): a new target must
  persist for several superframes before it is committed, so even genuine
  topology changes reshape the layout rarely and the network converges between
  changes.

### 2. How slots are laid out — stable frame, redistribute only

The critical insight from diagnosis: **growing the superframe to fit escalated
demand is what breaks.** When relays escalate, the total data band grows; if the
frame grows to fit it, that frame-size change must propagate network-wide and
transiently misaligns everything, and if it *doesn't* grow (the observed failure —
the NM did not re-size), the fill overruns the frame and starves whole nodes
(measured: an entire cluster got 0 TX slots, local delivery 8%).

Resolution — **decouple frame *size* from allocation:**
- The data band is sized by the **stable baseline** `default_data_slots ×
  active_node_count` (clamped to `max_data_slots`), NOT the escalated per-node
  sum (`SlotScheduler::ComputeBandSizes`). So the superframe size is invariant to
  escalation — no network-wide frame-size churn.
- Within that fixed pool, `FillSlotTable` distributes slots by a **demand-sorted
  round-robin**: every node with a request keeps its first slot before any node
  gets a second (no leaf starvation), then relays (higher fan-in) receive the idle
  leaves' surplus. Load-aware allocation thus only changes *who owns* the fixed
  set of data slots, never how many exist.

Net effect: a relay gains slots by taking the surplus that idle leaves do not
need, inside a frame whose size never changes.

## Supporting configuration changes

- `default_data_slots` 1 → 2 (`protocol_configuration.hpp`,
  `i_network_service.hpp`). A baseline of 2 gives every node minimal headroom and
  sizes the stable data pool at `2 × N`, which the redistribution draws from.
- `max_data_slots` 50 → 100. This is a *safety ceiling* on total data slots (to
  keep the superframe under the 255-slot wire limit), not a target — the frame is
  sized by actual baseline demand, so raising the cap does not enlarge networks
  that do not need the slots. Raised to give redistribution headroom on larger
  networks.
- `load_aware_data_slots` config flag (default true): toggles the fan-in
  escalation. `false` reproduces the flat baseline. Used by the stress test to
  compare flat vs load-aware.

## Known-separate issue (do NOT attribute to allocation)

Reliable-unicast and group-ACK **multi-hop** delivery is ~0% *even under flat
allocation* (allocation held constant). By definition that failure is not caused
by allocation; it points at the ACK-return path / reliable-timeout sizing /
multi-hop routing convergence at large superframe sizes. Tracked as a separate
investigation. Consequently the load-aware stress test gates only on the metrics
allocation governs — **1-hop non-reliable PDR and relay-queue depth** — and does
not gate on reliable/group PDR until that separate issue is resolved.

## Code map

- `network_service.cpp`: fan-in tally in `ProcessRoutingTableMessage`;
  `EvaluateDataSlotDemand` (bucket + hold-down, called from `HandleSuperframeStart`).
- `network_service.hpp`: `downstream_fanin_this_superframe_`,
  `pending_data_slot_target_`, `data_slot_target_stable_count_`,
  `kMaxDataSlotsPerNode`, `kDataSlotHoldDownSuperframes`.
- `slot_scheduler.cpp`: stable-baseline data-band sizing in `ComputeBandSizes`;
  demand-sorted round-robin fair fill in `FillSlotTable`.
- Config: `protocol_configuration.hpp`, `i_network_service.hpp`,
  `lora_mesh_protocol.cpp` (CreateServiceConfig wiring).
- Test: `test/protocols/lora_mesh/services/test_network_stress/`.

## Dead code to retire (consensus)

`SendSlotRequest`, `ProcessSlotRequest`, `ProcessSlotAllocation`,
`BroadcastSlotAllocation` and their dispatch cases — the request/grant handshake
is unnecessary under single-authored propagation. (Pending; not yet removed.)
