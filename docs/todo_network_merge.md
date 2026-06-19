# TODO: Network-Manager merge (Path A) — currently DISABLED

Cross-network **NM merge** (two separate networks bridging and converging under the higher-priority
network manager) is **disabled** because its only-partly-working implementation made two integration
tests flaky and could converge to the wrong winner. This document captures the full diagnosis and the
deterministic fix design so it can be implemented later.

## Current state (how it is disabled)

- **Feature gate:** `kNetworkMergeEnabled` in
  `src/protocols/lora_mesh/services/network_service.hpp` is `false`. The merge entry point — the
  `HandleForeignBeacon()` call in `ProcessSyncBeacon()`
  (`src/protocols/lora_mesh/services/network_service.cpp`, NETWORK_MANAGER foreign-beacon branch) — is
  skipped when the flag is false. A foreign beacon is logged and ignored; the two networks coexist.
- **Only the cross-network merge trigger is gated.** Same-network NM election (`NM_CLAIM` during
  `NM_ELECTION` / `FAULT_RECOVERY`, exercised by `nm_election_test.cpp`) is untouched and still works.
- **Tests skipped:** `NMMergeTests.BasicNetworkMerge` and `NMMergeTests.AutoRoleNMYieldsToConfiguredNM`
  in `test/protocols/lora_mesh/services/test_routing_nm_merge/nm_merge_test.cpp` call `GTEST_SKIP()`
  referencing this doc.
- **Kept in place:** the sticky-surrender behaviour (commit `10cd536`) in `PerformDiscovery` /
  `ProcessJoinResponse` / `CreateNetwork` — dormant for merge (no surrender happens while merge is off),
  still correct for normal election. The `ProcessSlotRequest` capabilities fix (commit `a720249`) is
  unrelated and stays.

## How to re-enable (when implementing the fix)

1. Implement the deterministic detection fix (Part 1 below) and the correctness fix (Part 2).
2. Set `kNetworkMergeEnabled = true`.
3. Remove the `GTEST_SKIP()` lines from the two merge tests and delete the brittle manual phase-alignment
   hack (`nm_merge_test.cpp` ~lines 211-223 and ~349-361).
4. Validate per "Verification" below.

---

## Root cause (verified by a 6-agent analysis)

1. **Permanent phase lock.** The desktop mock uses ONE shared virtual clock with zero drift
   (`src/os/rtos_mock.hpp`). Two networks that compute the same superframe length (`kMinSlots=16`) are
   phase-locked forever. Reception is slot-gated (`test/utils/network_testing_impl.hpp` `CanReceive()`
   requires the receiver to be in an RX slot at delivery time), so whether one NM's `SYNC_BEACON` /
   `NM_CLAIM` ever lands in the other NM's RX window is a fixed permutation that never sweeps. The test
   papered over this with a brittle, one-directional manual phase hack. On real hardware, oscillator
   drift slowly sweeps the phase so the networks eventually align — the deterministic test removes
   exactly that mechanism.
2. **A second nondeterminism:** the mock's shared seeded PRNG is consumed in nondeterministic thread
   order (feeds `network_id` and election jitter), so it is flaky (~50-75% pass) rather than
   deterministically pass/fail even with `SeedRandom(42)`.
3. **Wrong-winner by attrition (correctness bug).** When the winner→loser `NM_CLAIM` is never heard, the
   loser NM never yields; meanwhile the winner's own member loses beacons, enters DISCOVERY, and joins
   whichever NM it can hear — the loser. The rightful higher-priority NM ends isolated. The election
   arithmetic itself is correct (`ComputeElectionPriority`; `their_priority < election_priority_` is
   symmetric). This is the `AutoRoleNMYieldsToConfiguredNM` failure.

**Drift/jitter is rejected as a fix:** realistic drift (~0.3ms/superframe) is far too slow to sweep a
slot, and drift large enough to be fast breaks intra-network resync (> guard/2 ≈ 25ms). The fix must be
**deterministic**.

## The deterministic fix

### Part 1 — Phase-independent foreign-network detection (sweeping listen slot)
Make the NM's foreign-detection RX window POSITION sweep across the superframe, driven by the
deterministic superframe counter, so over a bounded number of superframes it visits every offset and is
GUARANTEED to catch a foreign NM's beacon for ANY fixed phase — no drift, no randomness.

- In `UpdateSlotTable` (`network_service.cpp`), the `discovery_reserve` `DISCOVERY_RX` slots are currently
  pinned to the superframe TAIL (Phase 5). Instead place that RX block at a rotating offset **within the
  elastic SLEEP region** (never colliding with fixed SYNC/CONTROL/DATA slots):
  `offset = (superframe_index * step + address_term) % (sleep_span - reserve + 1)`. The `address_term`
  (from `node_address_`) de-syncs the two NMs' sweeps so they are not in lockstep (avoids the
  both-transmit-at-slot-0 collision). Power/duty-cycle neutral (RX↔SLEEP reposition, counts unchanged).
- Re-position per superframe in `HandleSuperframeStart` — a lightweight in-place move of the
  `DISCOVERY_RX` slots, not a full rebuild. Wire in a monotonic superframe index (reuse
  `SuperframeService::superframes_completed_`; expose via the interface if needed).
- Once a foreign beacon is caught, the existing path runs unchanged: `ProcessSyncBeacon` NM-branch →
  `HandleForeignBeacon` → `SendNMClaim` → `ProcessNMClaim` (loser yields). Both NMs sweep, so the
  `NM_CLAIM` exchange completes deterministically within the bound.
- Then delete the manual phase-alignment hack in both tests; re-validate `MergeBudgetMs`
  (`test/.../test_integration/lora_mesh_test_fixture.hpp`) against the new deterministic bound.
- Edge case: a foreign beacon offset that permanently lands on one of OUR TX slots (sweep is confined to
  the sleep region — ~2-3 TX slots in a 2-node frame). The address-derived sweep start, plus the fact
  that detection only needs to succeed in ONE direction to start the claim exchange, should cover it. If
  empirics show a residual gap, add a small deterministic per-address superframe-phase offset so the two
  networks' beacons never permanently coincide with each other's TX slots.

Files: `network_service.cpp` (`UpdateSlotTable`, `HandleSuperframeStart`, `SetDiscoverySlots`),
`network_service.hpp`, `superframe_service.{hpp,cpp}` (expose superframe index), `nm_merge_test.cpp`
(delete hack), `lora_mesh_test_fixture.hpp` (`MergeBudgetMs`). Re-check
`comprehensive_slot_allocation_test.cpp` for any DISCOVERY_RX position assertions.

### Part 2 — Correctness: the rightful (higher-priority) NM must always win
Deterministic detection (Part 1) makes the loser reliably hear the `NM_CLAIM` and yield, which already
removes most of the wrong-winner basin (the loser stops beaconing; orphaned members fault-recover onto the
winner). On top of that, guarantee correctness explicitly:
- Smallest first: when an NM detects a foreign network, widen its RX listening for a few superframes
  (reuse `ExpandSyncBeaconListening`, `network_service.cpp`) — an event-scoped "merge wide-listen" so the
  claim exchange + member resync complete deterministically.
- Only if Part 1 + wide-listen don't fully guarantee a member converges to the true winner: add an
  election-priority byte to `SYNC_BEACON` (`sync_beacon_header.*`) so a NORMAL_OPERATION member adopts a
  foreign NM strictly by HIGHER PRIORITY (NOT by address — address is not priority-monotone across roles;
  the AutoRole winner `0x2` has a higher address than the loser `0x1`). This is a wire-format change
  (size asserts + golden tests) — avoid unless necessary.

### Candidate approaches considered (for the record)
Scored on solves-root-cause × low-risk: sweeping listen slot **(chosen)**; vernier superframe-length
difference (deterministic but perturbs duty cycle / many timing tests); production phase jitter
(architecturally unsound — one `superframe_start_time_` drives both TX and listen); directed-join (the
timing-aware join is already implemented via `SYNC_BEACON`→`SynchronizeWith`; `NM_CLAIM` carries no
timing); faster orphan rejoin (helps the tail but can push members to the wrong NM if keyed on address);
harness clock-drift (matches reality but too slow / non-deterministic — rejected).

## Verification (when re-enabling)
- `pio test -e test_native --filter "protocols/lora_mesh/services/test_routing_nm_merge"` run **≥8 times
  sequentially** (never looped/concurrent); both tests must pass EVERY run and converge to the
  higher-priority NM (`0x1` in Basic, `0x2` in AutoRole).
- No regressions, one env at a time: `test_unit_superframe_timing`, `test_tdma`,
  `test_unit_network_coverage`, `test_routing_role_change`, `test_integration`, `test_routing`.
- `pio run -e esp32` compiles.

## Constraints
- No runtime heap allocation in library code; the sweep only reorders fixed-array slots.
- Keep the sweep strictly inside the sleep span so data/control slots are never disturbed.
