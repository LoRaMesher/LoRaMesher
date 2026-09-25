# TODO-017: Joiners retry in lockstep and collide (dense-mesh formation fails at SF10–SF12)

Status: 🔴 OPEN. Analysed 2026-09-24/25, **not changed yet**. Priority: P1 (CI stays red
until fixed). This file is a self-contained handoff for a new session: read it top to
bottom before touching code.

---

## 0. TL;DR

- `LinkQualitySlicingTests.DenseMeshLinkQualityStable/SF10`, `/SF11` and `/SF12`
  (`test/protocols/lora_mesh/services/test_routing/link_quality_slicing_test.cpp`) fail
  network formation. Since the test harness became deterministic (§6), they fail **on
  every run** for seed 42 (default), and for every seed tried in 1–10 at least one of the
  three fails.
- Cause: a **join-protocol flaw that predates this branch** (March 2026 code). With
  7 nodes joining at once, their JOIN_REQUESTs keep colliding, and nothing in the protocol
  spreads them apart in time:
  1. all joiners transmit in the **same single discovery slot** per superframe;
  2. inside it they pick a random subslot, but at SF8+ only **2 subslots** fit a
     JOIN_REQUEST;
  3. the join retry **backoff never runs**: the join timeout (3 superframes) always fires
     first;
  4. the timeout sends every joiner back through FAULT_RECOVERY → DISCOVERY, so they all
     hear the same beacon and **restart in lockstep**, with their retry state reset.
- The user was **worried that changing joining would break other tests**, and asked to
  review this proposal before any code change. Get their go-ahead first.
- The user's standing rules for this work (see §9): test-first; tests must be
  deterministic and pin a worst-case that passes *every* time; never label a test "flaky";
  randomness control lives in the test harness, **not** in library code.

---

## 1. Where things stand (branch `feature/load-aware-slot-allocation`)

The branch is pushed to `loramesher/feature/load-aware-slot-allocation` by the user (the
latest local commits may not be pushed yet — check `git status -sb`). Base branch:
`refactor/architecture-review`. Library version is already bumped to **2.0.0** on this
branch.

Recent commits relevant to this TODO (newest first):

| Commit | What |
|---|---|
| `d6a2a95` | harness: `LORAMESHER_TEST_SEED` env var (default 42), seed printed on failure |
| `19c717a` | harness: reblock timeouts fail the test; VirtualNetwork topology locked |
| `d9ed80f` | harness: packets resolved at exact air-time end; collisions by on-air overlap |
| `7c92d0a` | rtos-mock: event-by-event virtual time advance |
| `c732a72` | rtos-mock: per-node random stream in `RTOSMock::GetRandom()` |
| `6de4588` | harness: fixed virtual epoch, seeded packet loss |
| `848ae57` | docs: start-up routing fix result |
| `fdc995d` | routing: unidirectional verdict only after the peer could hear us (fixes `GroupNoDuplicateDeliveryUnderFlood`) |
| `6e13c60` | revert of a library-side per-node RNG (user: randomness belongs in the harness) |
| `b73dccc` | first version of this TODO |
| `1fa24ae` | rtos-mock: reblock settledness from the queue itself (fixed ~150 s stalls) |

Full `test_routing` suite on this state, seed 42: **48 pass, 3 fail (SF10, SF11, SF12)**,
~1m28s, byte-identical PKT_TX/PKT_RX logs across two runs.

Design notes for the determinism and routing work:
`docs/design/test_determinism_and_startup_routing.md`.

---

## 2. Symptom and evidence

Test: `DenseMeshLinkQualityStable/<SF>` builds a full mesh of `kDenseMeshNodes = 8`
(`link_quality_slicing_test.cpp:31`): one NETWORK_MANAGER (`0x1000`) and 7 NODE_ONLY
nodes, all started together, then
`ASSERT_TRUE(WaitForNetworkFormation(nodes, kDenseMeshNodes - 1))` (line 87).

`WaitForNetworkFormation` (`test/protocols/lora_mesh/services/test_integration/lora_mesh_test_fixture.hpp:882`)
uses a budget of `GetDiscoveryTimeout(first_node) * (nodes.size() + 4)`, i.e. derived from
the *initial* superframe, and steps ≥ 50 ms.

Evidence from logs before the harness was made deterministic (SF11, 3 local runs, 2 failed):

- The budget is used fully in every run ("Using default timeout of 840720 ms"). The
  passing run finished formation at 682260 ms (81 % of the budget).
- Failing runs stop at 5 of 7 joined; the remaining nodes are **not stuck**. They cycle
  JOINING → "Join timeout - Fault recovery state" → FAULT_RECOVERY → DISCOVERY → JOINING,
  **all together**. In one run all 7 joiners entered state 5 at 69650 ms, 6 of them again
  at 171150 and 272650, 5 at 394460 and 516250. Each cycle is ~4 superframes (3 for the
  timeout + 1 to hear a beacon again).
- Loss is entirely JOIN_REQUEST collisions: 74 JOIN_REQUEST transmissions, 7 received by
  the NM. The NM answered every request it received (6 JOIN_RESPONSEs; the 7th at the end
  of the run), so the response side is fine.
- Example cycle (JOINING from 194350 ms): all 6 joiners send at 198700/214660 ms —
  subslot 0 = {1001,1006}, subslot 1 = {1003,1004,1005,1007} — and again at
  250900/266860 ms — subslot 0 = {1001,1004,1005,1006}, subslot 1 = {1003,1007}. Each logs
  "Join retry #1, next backoff: N superframes", then all time out at 272650. **Zero joins.**

After the deterministic harness (seed 42): SF10 log shows 7 joiners hashed/drawn into
2 discovery subslots, JOIN_REQUESTs from e.g. 0x1002 and 0x1007 going out at the same
instant (`[1010549–1010838]`) and colliding at 0x1000 in every round; 747 collision lines.
The old harness detected collisions only within one delivery batch, so some of these
used to get through by luck — that is why SF10/SF11 "sometimes passed" before.

Seed sweep 1–10 (deterministic harness, current branch): SF10/SF11/SF12 formation fails
in varying combinations for every seed; SF7–SF9 always pass (more subslots fit).

---

## 3. Root cause, with code references (line numbers as of `d6a2a95`)

### 3.1 One transmit opportunity per superframe, shared by all joiners
- `SlotScheduler::SetJoiningSlots` (`src/protocols/lora_mesh/services/slot_scheduler.cpp:426`)
  turns only the **first** `DISCOVERY_RX` slot into `DISCOVERY_TX` (`discovery_tx_added`,
  ~line 475). Every joiner therefore uses the same slot index every superframe.
- The JOIN_RESPONSE fallback path also uses discovery subslots
  (`lora_mesh_protocol.cpp:1372-1379`).

### 3.2 In-slot randomness is capped at 2 subslots for SF8+
- JOIN_REQUESTs go through `LoRaMeshProtocol::TrySendSubslottedMessage`
  (`src/protocols/lora_mesh_protocol.cpp:1220`) for `DISCOVERY_TX` (line ~1321) with
  `ComputeSubslotIdentifier(discovery_subslot_config)` (line 1202). The discovery config
  defaults to **5 subslots, `RANDOM` assignment**
  (`src/types/configurations/protocol_configuration.hpp:691`).
- `SubslotScheduler::ComputeTiming` (`src/protocols/lora_mesh/services/subslot_scheduler.cpp:16`,
  `effective_subslots` from line 57) keeps only as many subslots as fit a whole
  transmission. At SF11 a JOIN_REQUEST is ≈578 ms of air time in a 1450 ms slot → **2**
  subslots ("subslot=0/2 … tx_window=660 ms" in logs). SF8–SF12 all end up with 2; SF7
  gets 3.
- With k joiners and 2 subslots, P(some node alone in its subslot) ≈ 2k/2^k: 0.11 for
  k=7, 0.19 for k=6, 0.31 for k=5.

### 3.3 The backoff is dead code
- `NetworkService::StartJoining` (`src/protocols/lora_mesh/services/network_service.cpp:699`)
  resets `join_retry_count_ = 0`, `join_backoff_remaining_ = 1` (lines 712-713) and calls
  `SendJoinRequest` immediately (line ~731).
- Retries happen in `HandleSuperframeStart` (JOINING branch, lines ~2984-3011): after the
  backoff counts down, it resends, increments `join_retry_count_`, and draws
  `join_backoff_remaining_ = 1 + GetRTOS().GetRandom() % (max_backoff + 1)` with
  `max_backoff = min(1 << min(retry, 2), 4)`.
- `GetJoinTimeout()` (line 2619) = **3 superframes**. `PerformJoining` (line 2472) moves to
  FAULT_RECOVERY when `joining_start_time_ + timeout` passes.
- Timeline per cycle: send at superframe 0, retry at superframe 2, timeout at 3. The backoff
  drawn after retry #1 (1..3 superframes) always ends after the timeout, so **every joiner
  sends exactly twice per cycle, in the same two superframes**.
- `ProcessJoinResponse` with `RETRY_LATER` (line ~1698) also resets
  `join_backoff_remaining_ = 1; join_retry_count_ = 0`.

### 3.4 The timeout re-synchronises everyone
- FAULT_RECOVERY for NODE_ONLY nodes restarts discovery (`lora_mesh_protocol.cpp`,
  STATE_TIMEOUT handling ~lines 851-860). All joiners hear the next NM beacon in the same
  superframe and call `StartJoining` together, with reset retry state.

### 3.5 The budget ignores superframe growth (amplifier, not cause)
- The NM's slot table grows as nodes join (SF11: 16 → 18 → 22 → 26 → 30 → 35 → 43 slots,
  i.e. 23.2 s → 62.4 s superframes), so each 4-superframe cycle grows from ~93 s to
  >180 s, while the test budget is computed from the initial superframe.
- Widening the budget only adds margin; it does not fix the lockstep.

### 3.6 Not caused by this branch
`git blame` of `GetJoinTimeout`, the `StartJoining` reset and the backoff: commits
`4059090f` / `3c90b8d6` (March 2026). The branch's data-band change keeps 2 data slots per
node (superframe growth unchanged); the RT-header +1 byte and `StoreControlSlotIndex` do not
touch the join path; JOIN_REQUEST (12 B) and JOIN_RESPONSE (15 B) fit easily.

---

## 4. Proposed fix (to be confirmed with the user)

Goal: joiners that start together must spread out over time, and a join timeout must not
re-synchronise them. Keep changes minimal and local to the join path.

**(A) Random initial offset.** In `StartJoining`, do not send immediately. Draw
`join_backoff_remaining_ = GetRTOS().GetRandom() % W` superframes (W ≈ 2–4, or scaled
with the observed node count / discovery load) and let `HandleSuperframeStart` send when
it reaches 0. Randomness must come from `GetRTOS().GetRandom()` (platform RNG) — **do not
add an RNG to library classes** (user decision; the harness makes `GetRandom()` per-node
and deterministic).

**(B) Retry state survives a join timeout.** Either:
- (B1, preferred) stay in JOINING while the NM's beacons are still heard, instead of
  FAULT_RECOVERY → DISCOVERY; only leave JOINING after missed beacons (the existing
  `no_received_sync_beacon_count_` mechanism), or
- (B2) keep `join_retry_count_` across the FAULT_RECOVERY → DISCOVERY → JOINING loop
  (don't reset in `StartJoining` when re-joining the same network id), so the backoff
  window keeps growing.

**(C) Join timeout covers the backoff.** Make `GetJoinTimeout()` at least
`(max_backoff + 2)` superframes (or scale with `join_retry_count_`), so the exponential
backoff actually runs. Consider raising the backoff cap above 4 for dense networks.

**(D) Optional — more capacity.** Let a joiner pick a random `DISCOVERY` slot from the
discovery band (not always the first), keeping the slots used for JOIN_RESPONSE / NM_CLAIM
free. Check `SetJoiningSlots` and `ScheduleDiscoverySlotForwarding` before doing this;
the discovery band has `(depth+1)*2` slots and is shared with NM_CLAIM reception and
sponsor forwarding.

**RETRY_LATER** (NM join queue full): should also back off with the grown window, not
reset to 1.

Things to double-check while designing:
- Sponsor-based joins (§6.4 of PROTOCOL_SPEC): joins forwarded by a sponsor
  (`ScheduleDiscoverySlotForwarding`) and the JOIN_RESPONSE fallback path must still work.
- NM election / fault recovery tests rely on FAULT_RECOVERY behaviour (B1 changes when a
  joiner enters FAULT_RECOVERY).
- `test_routing_role_change`, `NMElectionTests.*`, `SponsorBasedJoinTest.*`
  (coverage suite), `LoRaMeshDiscoveryTests.*` and `test_integration` exercise joining.
- PROTOCOL_SPEC §6.3.x (join flow, RETRY_LATER, backoff) must be updated.

---

## 5. Test plan (test-first)

1. **Unit / coverage test (NetworkService)** — deterministic, no network timing:
   several joiners call `StartJoining` on the same beacon; step `HandleSuperframeStart`
   and record in which superframe each queues a JOIN_REQUEST. Assert they are not all in
   the same superframe, and that retry/backoff state survives a join timeout (or that the
   node stays in JOINING while beacons arrive, for B1). Red today: every joiner sends in
   superframes 0 and 2.
2. **Integration** — `DenseMeshLinkQualityStable/SF10..SF12` must pass for seed 42 and
   for a seed sweep (see §7). Use the sweep to find the worst seed and pin it (e.g. an
   explicit `LORAMESHER_TEST_SEED` set by the test or a dedicated regression test that
   calls the fixture's seeding with that value).
3. Consider a dedicated "N simultaneous joiners at SF12" test that asserts formation
   within a bound derived from the protocol (not from luck), so the worst case is pinned
   independently of `LinkQualitySlicingTests`.

---

## 6. The deterministic harness (what changed, how to use it)

Before (2026-09-24), test outcomes depended on thread scheduling:
- one shared platform RNG for all nodes → which node got which draw varied;
- time advanced in 10–50 ms steps; all tasks due in a step ran concurrently;
- collisions detected only inside one delivery batch; delivery in TX-thread push order;
- radio deliveries raced with slot transitions; reblock timeouts silently continued.

Now (commits `6de4588` … `d6a2a95`, all in `src/os/rtos_mock.hpp` and `test/`):
- `RTOSMock::GetRandom()` draws from a per-node stream (keyed by the calling task's node
  address, seeded from `SeedRandom` seed ⊕ address); the test thread has its own stream.
- Virtual time starts at `RTOSMock::kVirtualEpochMs` (1'000'000) and advances **event by
  event** (each wake-up in deadline order, stable tie-break, reblock after each).
- The virtual network resolves each packet at its exact air-time end; collisions by
  on-air overlap per receiver across steps; a transmitting receiver misses packets
  (half duplex); stable order (delivery time, TX start, source, destination).
- A reblock timeout fails the test (`EXPECT_EQ(..., 0)` in fixture `TearDown`).
- `LORAMESHER_TEST_SEED=N` selects the seed (default 42); a failing test prints
  `[   SEED   ] Reproduce with LORAMESHER_TEST_SEED=N`.
- Per-test logs: `test_logs/<Suite>_<Test>.log` relative to the **current directory**
  (parameterised names have `/` replaced).

Determinism check used: run the suite twice with the same seed and diff per-test
`PKT_TX|PKT_RX` lines — 0 of 52 logs differed.

---

## 7. How to reproduce and iterate

```bash
# Build the routing suite only (no run)
pio test -e test_native -f "protocols/lora_mesh/services/test_routing" --without-testing

# Run from a LINUX directory (not /mnt/<drive>): big logs, faster I/O
mkdir -p /tmp/jc && cd /tmp/jc
/mnt/d/Projects/ESP32/loramesher/.pio/build/test_native/program \
  --gtest_filter='AllSpreadingFactors/LinkQualitySlicingTests.DenseMeshLinkQualityStable/SF11' \
  > sf11.log 2>&1
# Per-test log (verbose protocol trace):
less test_logs/AllSpreadingFactors_LinkQualitySlicingTests_DenseMeshLinkQualityStable_SF11.log

# Another seed
LORAMESHER_TEST_SEED=7 /mnt/d/.../program --gtest_filter='...SF11' > sf11_s7.log 2>&1

# Seed sweep (note: gtest filters do NOT support [...] character classes)
for s in $(seq 1 20); do LORAMESHER_TEST_SEED=$s /mnt/d/.../program \
  --gtest_filter='*DenseMeshLinkQualityStable/SF1*' > sweep_$s.log 2>&1; \
  echo "$s $(grep -aE '^\[  (PASSED|FAILED)' sweep_$s.log | tr '\n' ' ')"; done
```

Useful log greps (strip ANSI with `sed 's/\x1b\[[0-9;]*m//g'`, always `grep -a`):
- state changes: `state changed to` (1 DISCOVERY, 2 JOINING, 3 NORMAL, 4 NM, 5 FAULT_RECOVERY)
- `Join request queued`, `Join retry #`, `Join timeout`, `JOIN_REQUEST`, `type=0x42`
  (JOIN_REQUEST), `type=0x43` (JOIN_RESPONSE)
- `Subslot timing: ... subslot=a/b`, `COLLISION`, `receiver unavailable`

Formation-dependent suites to re-run after a join change (one `pio test` build at a time):
`test_routing` (full), `test_routing_role_change`, `test_integration`,
`test_unit_network_coverage` (includes `SponsorBasedJoinTest`), `test_unit_core_services`,
`test_tdma`, `test_sync_beacon_subslot`, `test_network_stress` (10n cell), plus a seed
sweep.

---

## 8. Other seed-dependent failures found by the sweep (separate, still open)

Deterministic with the given seed, found 2026-09-25 (seeds 1–10, current branch):
- `TopologyRoutingTests.RingTopologyFiveNodes` fails with seeds **7** and **9**.
- `NMElectionTests.TwoAutoNodes_ExactlyOneWinsElection` fails with seed **8**.

They were always possible but only surfaced now that runs are reproducible. Root-cause
each (per-test log + `LORAMESHER_TEST_SEED`), fix, then pin the seed as a regression.
Could be related to joining/formation timing — check after the join fix too.

---

## 9. User preferences and constraints for this work

- Test-first: write the failing test, confirm it fails for the right reason, then fix.
- Tests must be deterministic; a test should pin a worst-case scenario that passes every
  time. Never call a failing test "flaky" — find the root cause.
- Randomness control lives in the **test harness** (`RTOSMock::GetRandom` per node), not
  in library classes. Library code keeps using `GetRTOS().GetRandom()`.
- The user is cautious about changing join behaviour — propose, get approval, then change
  with a full test sweep.
- Protocol changes: document the *why* in `docs/design/*.md` and update PROTOCOL_SPEC.md.
- Code style: Google style, 4 spaces, 100 cols; comments say what, not history; keep CRLF
  on files that have it (check with `file`); clang-format-19 runs in a pre-commit hook.
- No runtime heap allocation in library code.
- Stage only files from this work; the working tree holds unrelated user WIP
  (`log_analyzer.html`, `platformio.ini`, `examples/simple_example/platformio.ini`,
  `TODO.md` is an untracked user file, experiments dirs, …).
- Don't push; the user pushes.

---

## 10. CI context

GitHub Actions `test.yml` runs `pio test -e test_native_profile` (coverage-instrumented) and
an `esp32-build` job that builds `examples/simple_example` env `ttgo-lora32-v1` on
`espressif32` 7.1.3 (GCC 8.4, no `<span>`, no defaulted comparisons). Reproduce the ESP32 job
locally with a copy of the example pinned to `platform = espressif32@7.1.3` and
`lib_deps = symlink://<repo>`. `test_network_stress`'s 25-node cell only runs with
`LORAMESHER_STRESS_FULL=1` (not in CI).
