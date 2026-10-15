# Join Contention — Design Rationale

Status: implemented (2026-10). Closes TODO-017. CI symptom:
`LinkQualitySlicingTests.DenseMeshLinkQualityStable/SF10..SF12` (1 NM + 7 simultaneous
joiners, full mesh) never formed the network.

## 1. Why simultaneous joiners never got in

1. **One transmit opportunity.** `StartJoining` queued the JOIN_REQUEST at once. Every
   joiner had heard the same beacon, so all of them sent in the first discovery slot (d0)
   of the same superframe.
2. **Few subslots at high SF.** A JOIN_REQUEST (12 B) picks a random discovery subslot, but
   only as many subslots as fit a whole transmission are used: SF7 5, SF8 5, SF9 4,
   **SF10–SF12 2**. With 7 joiners and 2 subslots, P(some joiner alone) ≈ 2k/2^k = 0.11.
3. **Dead backoff.** Retries backed off 1..3 superframes after retry #1, but the join
   timeout was 3 superframes, so the timeout always fired first.
4. **Timeout re-synchronised everyone.** JOINING → FAULT_RECOVERY → DISCOVERY → the next
   beacon → `StartJoining`, which reset the retry state. All joiners restarted together.

The NM's response side was fine: it answered every request it decoded.

## 2. Options compared

Slotted-ALOHA model (contention units per superframe = usable request slots × subslots),
Monte Carlo over 3000 runs, 7 joiners, SF11 superframe growth (16 → 43 slots), test budget
36 initial superframes:

| Policy | Mean superframes to form | p99 | P(over budget) |
|---|---|---|---|
| Before (lockstep) | 45.7 | 115 | 0.91 |
| X: random superframe backoff only, window cap 4 | 10.8–12.8 | 19–21 | 0.02 |
| Request in any discovery slot with a following slot | 10.3 | 17 | — |
| **Z: request in an even discovery slot + superframe backoff** | **9.0** | **15** | **0.001** |

With 15 joiners X needs ~31 superframes on average, Z ~18; the gain grows with load. At
SF7 (5 subslots) Z costs ~0.4 superframe more than X because two responses decoded in the
same slot pair push the second response into the next request slot ("spill").

- A request in an **odd** slot hits a NM that is transmitting a response in that slot
  (half duplex), so "any slot" is barely better than X.
- A window cap of 2 collapses with 15 joiners; cap 8 is too conservative for 7. Cap 4.
- A hard "one join per superframe" guarantee would need NM-scheduled joins. Random access
  gives ~0.8–1.0 joins per superframe once depth ≥ 1.

## 3. Chosen mechanism

- **Request/response pairs.** The discovery band is the last `(depth+1)*2` slots, i.e.
  `depth+1` pairs. A direct join (sponsor is the NM) sends in a random even slot
  `d_{2i}`, `i = GetRandom() % (depth+1)` (no draw at depth 0); the NM queues its
  response on reception, and the DISCOVERY_RX fallback sends it in `d_{2i+1}`. Up to
  `depth+1` requests per superframe can be answered.
- **Sponsored joins use d0.** The relayed round trip needs the whole band
  (`2(k+1)` slots for a sponsor at hop k), so it must start in the first slot.
- **First attempt in the joining superframe.** With no failures yet, `StartJoining`
  schedules the attempt in the current superframe, so a lone joiner still joins in the
  superframe it heard the beacon in.
- **Unanswered = still pending at the next superframe start.** A direct response arrives
  in the request's superframe. The node then increments `join_retry_count_` and waits
  `GetRandom() % 2^min(retry, 2)` superframes (0..1, then 0..3). A sponsored joiner waits
  one extra superframe, since its response can be relayed into the next superframe.
- **Retry state survives a rejoin.** `join_retry_count_` is reset only on ACCEPTED,
  `CreateNetwork` and `ResetNetworkState`, not per network id (a newly elected NM keeps
  the old `network_id_`). Joiners that time out together come back with different
  backoffs.
- **Join timeout = 13 superframes** (first superframe + three attempts at the full
  4-superframe window). It is only the exit for a sponsor that cannot answer.
- **RETRY_LATER** is not a failure (the request was delivered): the retry count is kept
  and the node waits a random 0..max(1, window) superframes so deferred joiners don't
  return together.
- **Hook.** `LoRaMeshProtocol::OnSlotTransition` passes the slot's position within the
  discovery band, counted from the slot table, to
  `NetworkService::HandleDiscoverySlotStart()`, which queues the JOIN_REQUEST at the
  start of its slot. A position counter is not used: slot transitions can be dropped.
  An attempt whose slot never occurred (resync, smaller band after a depth change) is
  re-scheduled at the next superframe start without counting as a failure.

## 4. Random draws

`GetRTOS().GetRandom()` is drawn only: for the request slot when `depth ≥ 1` on a direct
join; for the backoff after an unanswered attempt, on a rejoin with retries, and on
RETRY_LATER. Each draw shifts the per-node deterministic stream in the test harness, so
multi-joiner runs at depth ≥ 1 replay differently than before the change.

## 5. Future options

- NM-scheduled joins (assign request slots in the beacon) for a hard per-superframe bound.
- Shrink the backoff window when another joiner's ACCEPTED is overheard (helps ≥ 20
  simultaneous joiners).
- Let sponsored joins start in later even slots when `j ≤ 2(depth − k)`.
