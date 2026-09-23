# Reliable Delivery at Scale — Design Rationale

Status: **implemented (2026-09-23)** — commit `54cf983`; PROTOCOL_SPEC §3.2.6
and §7.2 updated. Tracks TODO-014.

## Result (stress test, library adaptive timeout)

| cell | reliable PDR | pending at end | relay queue final-q | retries dropped at relays |
|------|------|------|------|------|
| 10n before | 85.7% (12/14) | — | 2.0 | — |
| 10n after  | 100% (14/14) | 0 | 1.7 | 0 |
| 25n before | 0% (0/14) | — | 4.6 | 44+ |
| 25n after  | 100% (14/14) | 0 | 2.5 | 0 |

25n reliable RTT is long (p50 ≈ 1137 s ≈ 18 superframes for 10 hops) but
bounded. Group ACK completeness is unchanged (~28% at 25n): the test's group
window (4 superframes) is shorter than a 5-hop ACK return — tracked separately.

## Problem
In the 25-node stress test (`test_network_stress`, 5 hops worst case, 62.5 s
superframe) reliable unicast delivered 0/14 while 1-hop non-reliable delivered
100%. At 10 nodes (2 hops) reliable delivered 85.7%.

## Evidence (25-node log, hop-by-hop trace)
- 8 of 14 messages reached their destination and were ACKed. Every ACK that got
  back arrived 1015–1450 s after the send; the sender had already given up at
  5 attempts × 187.5 s = 937.5 s, so each logged `matched=0`.
- Per-hop delay was ~250–300 s: each head has 2 TX slots per superframe and its
  shared TX queue (capacity 10) held 8–10 packets, much of it group-ACK fan-in
  toward the NM.
- 44+ retransmissions were sent; every one was dropped as a duplicate at the
  first relay (`Dropping duplicate DATA` at 0x1005 and 0x100F).
- 8 forwards failed with `Queue full`.

## Root causes
1. **Retries were indistinguishable from the original at relays.** A retry reused
   the original `(source, seq)`, and relays de-duplicate on that key, so a retry
   could only ever pass the first hop if the first hop had lost the original.
   Retries consumed the origin's TX slots without ever repairing a later loss.
2. **A relay recorded a packet as seen before forwarding it.** When the forward
   failed (`kQueueFull`, no route), the packet was dropped but every later copy
   was rejected as a duplicate.
3. **Enqueue failures consumed attempts.** `ReliableDelivery` ignored the
   `send_attempt` result, so an attempt rejected by a full queue used up a retry
   and a full timeout.
4. **The retransmission timeout did not follow the path.** The caller-provided
   timeout (3 superframes) was used for every hop count and load; the library's
   hop formula is also load-blind. Late ACKs were discarded, losing the only
   measurement of the real round trip.
5. **Group-ACK capacity was 16 responders**, fewer than a 25-node group.

## Design

### Per-attempt link sequence, stable message sequence
Each transmission attempt is a distinct link-layer packet with its own sequence
number drawn from the node's counter. Relays keep de-duplicating on
`(source, link seq)`, which still suppresses loops and overheard copies, and now
forward every attempt.

End-to-end identity moves into the reliable framing prefix:

```
DATA_RELIABLE / reliable GROUP payload:
[msg_seq:1][send_ts:4][application payload]
```

- `msg_seq` is the sequence allocated when the message was first sent; it is the
  `MessageId` returned to the application and tracked in the pending table.
- The first attempt uses `msg_seq` as its link seq; each retry draws a new one.
- The destination de-duplicates delivery on `(source, msg_seq)` and ACKs every
  reception with `acked_seq = msg_seq`, so the origin matches the ACK regardless
  of which attempt arrived.
- `send_ts` is the attempt's own timestamp, echoed by the ACK, so every ACK gives
  an unambiguous RTT sample (no Karn ambiguity).

### Relay caching after a successful forward
A relay records `(source, link seq)` only once the forward has been queued. A
failed forward leaves the packet unrecorded, so a later copy can still pass.

### Enqueue failures do not consume attempts
When `send_attempt` fails, the entry keeps its retry budget and is re-attempted
after a short delay instead of waiting a full timeout.

### Adaptive RTO stored on the route
Per-destination SRTT/RTTVAR (RFC 6298, α = 1/8, β = 1/4) is kept on the
destination's routing entry (`NetworkNodeRoute::path_rtt`, not serialized),
alongside the other local per-destination state (`link_stats`,
`control_slot_index`). The estimator math is stateless and lives in
`protocols/reliability/rtt_estimator.hpp`; the routing table only stores the
values and resets them when the route's next hop or hop count changes.

- RTO = SRTT + 4·RTTVAR, clamped to [500 ms, max_hops × 4 superframes].
- With no sample yet, the hop formula `(2·hops + 1) × superframe` is used.
- Each retransmission doubles the entry's timeout (clamped).
- Every ACK — including one that arrives after the entry failed — feeds a sample.

### Group responders
The responder capacity covers a full network instead of a fixed 16.

## Rejected alternatives
- **Only lengthening the test timeout:** hides the problem from applications that
  pass short timeouts and leaves retries useless.
- **Skipping relay de-duplication for reliable traffic:** works without a wire
  change, but gives up duplicate suppression for reliable packets and conflates
  link-layer and end-to-end identity.
- **An attempt counter byte (dedup on `(source, seq, attempt)`):** same wire cost
  as `msg_seq`, but keeps the link seq ambiguous and needs a relay-side key change.
- **A separate RTT table keyed by destination:** duplicates the routing table's
  destination set and needs its own capacity and eviction policy.

## Compatibility
The reliable prefix grows from 4 to 5 bytes, so reliable traffic is not
interoperable with 1.x nodes; the maximum reliable application payload shrinks
by 1 byte.
