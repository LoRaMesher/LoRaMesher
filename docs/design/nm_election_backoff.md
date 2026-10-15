# NM Election Backoff — Design Rationale

Status: implemented (2026-10). Closes TODO-018. Symptom:
`NMElectionTests.TwoAutoNodes_ExactlyOneWinsElection` failed for some `LORAMESHER_TEST_SEED`
values — after the Network Manager failed, both AUTO survivors ended in NETWORK_MANAGER.

## 1. Why both survivors became NM

1. **The backoff spread was quantised away.** A node entering FAULT_RECOVERY starts a staggered
   backoff (listen window + role bonus + address bonus + jitter), so the highest-priority node
   claims first and the others hear its NM_CLAIM. But the expiry was only checked in
   `HandleSuperframeStart`. With ~22 s superframes and backoffs that differ by at most ~7.5 s, both
   nodes usually expired at the same superframe start (seed 17: due at 1074577 ms and 1076333 ms,
   both acted at 1084999 ms).
2. **Simultaneous claims are not heard.** Both queued their NM_CLAIM at the same instant and sent
   it in the same slot (both at 1085425 ms). Radios are half duplex, so neither heard the other;
   after the 2-slot election window both called `CreateNetwork()`.
3. **A split is permanent.** An NM ignores beacons from another NM of the same network id, and two
   NMs created at the same instant with the same superframe length send beacons in the same slot
   forever (the phase lock described in `docs/todo_network_merge.md`).

## 2. Fix

- **Precise expiry.** `NetworkService::CheckElectionBackoff()` moves FAULT_RECOVERY to NM_ELECTION
  once `GetElectionBackoffRemaining()` reaches 0. The protocol task's FAULT_RECOVERY wait times out
  exactly at the backoff deadline and calls it; `HandleSuperframeStart` still calls it too.
- **Waiting nodes listen on every slot.** `StartElectionBackoff()` switches the slot table to
  discovery slots (all `DISCOVERY_RX`). Without beacons the old schedule is stale and would sleep
  through a claim sent at an arbitrary time; now a node still in its backoff hears the earlier,
  higher-priority NM_CLAIM and surrenders (existing `ProcessNMClaim` path).

The election priorities, backoff formula, NM_CLAIM format and the 2-slot election window are
unchanged.

## 3. Residual risk

Two nodes whose backoffs end within the same slot still claim together. With precise expiry the
backoffs keep their spread (address bonus up to 5 s plus 0–2.5 s jitter), and within one slot the
claims also pick random discovery subslots. A split that does happen is still permanent until
same-network NM conflicts are resolved — that needs the deterministic merge detection planned in
`docs/todo_network_merge.md`.

## 4. Tests

- `test_unit_network_coverage/election_backoff_test.cpp`: remaining backoff counts down; an
  expired backoff enters NM_ELECTION and queues NM_CLAIM without a superframe start; a pending
  backoff keeps waiting; a waiting node listens on every slot.
- `NMElectionTests.TwoAutoNodes_BackoffsEndingInOneSuperframe`: the seed-17 scenario pinned with
  `UseTestSeed(17)`.
