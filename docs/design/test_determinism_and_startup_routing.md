# Deterministic Tests and Start-up Routing Transients — Design Rationale

Status: in progress (2026-09-24). Follows the CI investigation of
`feature/load-aware-slot-allocation`. User directive: tests must not depend on luck —
each test pins a worst-case scenario that must pass every time.

## 1. Start-up routing transient (full mesh)

Observed in `GroupAckTests.GroupNoDuplicateDeliveryUnderFlood` (~1 in 5 runs): a node
in a 4-node full mesh is left with no active hop-1 routes for ~2 superframes after
joining, so its slot table has no data RX slots and a one-shot group flood is lost.

Mechanism (all code predates the branch: ac199ca, d6a52d2):
1. **False UNIDIRECTIONAL verdict.** A peer reports `reception_quality` for us only
   after 3 of our tables (`NetworkNodeRoute::ToRoutingTableEntry`), but we declare the
   link unidirectional once we have heard the peer 3 times with `remote == 0`
   (`LinkQualityStats::CalculateQuality`). A joining node hears the NM's tables before
   it broadcasts any of its own, so it reaches 3 samples while the NM has 0–2 → a working
   bidirectional link is marked unidirectional (quality 1, cost 65535).
2. **Provisional gate keeps a dead route.** A new direct neighbour with < 3 samples is
   not allowed to replace the current route even when that route's cost is 65535
   (`DistanceVectorRoutingTable::ProcessRoutingTableMessage`, `use_direct`).
3. **Routes via a rejected neighbour.** The same provisional neighbour's advertised
   routes are still installed through it, producing multi-hop routes via a next hop
   that is not an active direct neighbour, and loops (`0x1000 via 0x1003`,
   `0x1003 via 0x1000`). The slot scheduler gives such next hops no RX/TX pairing.

Fix direction:
- The unidirectional verdict counts only the peer's tables received **after the peer
  could have heard us** (i.e. after our own routing broadcasts started), with the
  existing sample threshold.
- A direct link may replace a route whose cost is unusable (65535) or whose next hop is
  not an active direct neighbour, even while provisional.
- Routes are not installed via a source that was not accepted as a direct neighbour.

Result (fdc995d): peer tables that omit us count toward the unidirectional verdict only
after `kUnidirectionalGraceBroadcasts = kMinSamplesForQuality + 1` of our own routing
broadcasts since first contact (recorded at actual transmit via
`IRoutingTable::NotifyLocalRoutingBroadcast`); `LinkQualityStats::IsUnidirectional()` is the
single verdict. A non-unidirectional direct link replaces an unusable route (cost 65535 or
next hop not an active direct neighbour) even while provisional, and advertised entries are
installed only through a source that is an active direct neighbour.
`GroupNoDuplicateDeliveryUnderFlood` 11/11. Reporting `reception_quality` after the first
sample was rejected: it would seed the peer's remote EWMA low and depress link costs for
many superframes.

## 2. Nondeterminism in the test harness

Seeding the RTOS mock's RNG (fixture seeds 42) does not make runs repeatable:
- **One shared RNG for all nodes** (`RTOSMock::GetRandom`); nodes run on real threads,
  so which node receives which draw depends on OS scheduling. Draws: discovery subslot
  (`RANDOM` strategy), join backoff, NM election jitter, network id.
- **Coarse time steps.** Every task due inside a 10–50 ms step wakes at once; subslot
  offsets inside one step collapse onto the same transmission time.
- **Batch-based delivery.** The virtual network detects collisions only among packets
  delivered in the same step batch and delivers survivors in TX-thread push order; a
  receiver accepts only the first packet of a batch.
- **Delivery before time advance.** Radio events are delivered before the RTOS time
  advance, racing with slot transitions.
- **Silent reblock timeouts.** A reblock timeout logs and continues with tasks mid-work.
- **Variable epoch.** Virtual time starts from the real clock.

Plan:
1. Per-node RNG **in the test harness only**: `RTOSMock::GetRandom()` draws from a
   generator owned by the calling task's node (seeded from the fixture seed and the node
   address), so the library keeps calling `GetRTOS().GetRandom()` unchanged and ESP32
   builds keep hardware entropy. (A library-side per-node RNG was tried and reverted:
   determinism is a test concern and must not change library code.)
2. Harness: event-driven time stepping (advance to each wake deadline), collision
   detection on air-time overlap across batches, stable delivery order, deliver after
   the time advance, fixed virtual epoch, reblock timeout fails the test.
3. `LORAMESHER_TEST_SEED`: fixture seed from the environment (printed on failure);
   sweep seeds to find worst cases and pin them in regression tests.
