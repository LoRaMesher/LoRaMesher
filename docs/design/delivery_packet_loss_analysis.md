# Delivery Packet-Loss Analysis — where the ~25% 1-hop loss comes from

Status: root cause identified (2026-09-23); fix in progress (index-based data
band, below). Supersedes the Jul 2026 "half-duplex + collisions" explanation.

Success criterion (from user): **every originated packet delivered within some
bounded number of superframes, and no queue grows unboundedly. Latency is NOT a
criterion.**

## Symptom
25-node clustered-backbone stress test (`test_network_stress`, 5 stars of 5 on a
line of heads, NM = centre head, `default_data_slots=2`): queues are bounded, but
~25% of 1-hop leaf→head non-reliable packets are lost. The harness logs
`dropped at 0x.... - receiver unavailable (state=4)` and reports ~10% collisions.

## Root cause: nodes compute different data-band layouts
`state=4` is `RadioState::kSleep` (`radio_state.hpp`), **not** transmit. The
harness mock `Send` restores `kReceive` immediately, so half-duplex TX is never
the cause of a drop. The receiver is asleep because its slot table has SLEEP (or
its own TX, after which the radio sleeps) where the sender has TX.

The frame length, sync band and control band are agreed network-wide (they come
from the sync beacon and each node's NM-assigned `control_slot_index`). The data
band was not: `SlotScheduler::BuildOrderedNodes` took this node's *own* routing
table plus itself, sorted NM-first then by address, and `FillSlotTable` gave each
node `allocated_data_slots` consecutive slots in that order. Any difference in
the local node set, order or per-node counts shifts every later node's slots.
Three independent sources of divergence:

- **A. NM-first sort uses a flag that does not propagate.** `is_network_manager`
  is set only when a sync beacon arrives *directly* from the NM
  (`ProcessSyncBeacon`); `RoutingTableEntry` carries no NM field. Nodes not
  adjacent to the NM sort it by address → every lower address shifts by `d`.
- **B. `max_hops` truncates the node set.** Routes longer than `max_hops` (5) are
  rejected, so far leaves (6 hops) omit some nodes → their data band starts
  earlier than their head's.
- **C. Stale per-node counts and inactive entries.** A remote node's
  `allocated_data_slots` is refreshed only when a better route arrives, and
  inactive entries keep occupying positions until removal, whose timing differs
  per node.

Collisions have the same cause: misaligned senders share a slot index a head
listens to. Additionally, the stress metrics count link events at *every*
listener (overhearing, formation-time traffic, sync-beacon forwards in shared
hop-layer slots), so the link-level rates overstate the leaf→head loss.

## Why slot-count changes could not fix it
The loss is independent of how many data slots a node has, so "load-aware"
redistribution (see `load_aware_slot_allocation.md`, rejected) could not help.

## Fix: data band indexed by control-slot index
Every node derives the identical data-band layout from network-wide state:

- Data band size = `min(N × d, max_data_slots)`, where `N` is the control-band
  size (NM: highest index + 1; others: beacon `node_count`) and
  `d = default_data_slots` (uniform, config).
- Data slot `k` of the band belongs to control index `c = k / d`:
  own index → TX; index owned by an active direct neighbour → RX(owner);
  anything else → SLEEP.

A node therefore needs only its own index, its direct neighbours' indices and
`N` — none of which depends on how complete its routing table is.

Neighbour indices previously spread only by third-party gossip (routing entries
carry `control_slot_index`, but a node's broadcast slice never includes itself
and the header had no field for it), so a leaf never learnt its head's index. A
new routing-table header field, `source_control_slot_index`, carries the sender's
own index; it takes precedence over gossip for direct neighbours.
`source_allocated_data_slots` is unchanged (still used for join admission).

## Verification hooks
- Unit: `test_unit_slot_scheduler` alignment tests (two schedulers with different
  local views must agree: every TX slot of A is RX(A) at neighbour B).
- Integration: `test_network_stress` deterministic `AssertTdmaAlignment()` over
  every node/neighbour pair, 1-hop non-reliable PDR counted only at the intended
  destination, and link counters reset at the start of the measured window.
