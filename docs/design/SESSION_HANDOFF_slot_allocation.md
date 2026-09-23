# Session Handoff — Slot Allocation / 25-Node Stress Test / Delivery

Written to resume later. Everything needed to continue is here or in the linked
docs. Read this top-to-bottom first.

---

## 0. TL;DR / where we are RIGHT NOW (updated 2026-09-23)

- **Decision taken:** load-aware fan-in redistribution DROPPED (code removed);
  `default_data_slots=2` + `max_data_slots=100` KEPT.
- **Root cause of the ~25% 1-hop loss found:** it is NOT half-duplex. Nodes
  computed different data-band layouts from their local routing tables, so
  senders transmitted while receivers slept. See
  `delivery_packet_loss_analysis.md`.
- **Fix in progress** (plan: `~/.claude/plans/what-we-were-doing-optimized-phoenix.md`):
  1. failing alignment unit tests in `test_unit_slot_scheduler`;
  2. data band indexed by `control_slot_index` in `SlotScheduler`;
  3. new routing-header field `source_control_slot_index` so direct neighbours
     learn each other's index;
  4. stress test: alignment assertion, destination-only 1-hop PDR, counters reset
     at the measured window, `max_hops=6`.
- Multi-hop reliable/group ~0% remains a separate P0
  (`todos/reliable_multihop_ack_zero.md`).
- Sections below describe the Jul 2026 state and are kept for history.

## 0.1 EXACT FILE MANIFEST (mine vs. pre-existing tree noise)

`git status` shows ~64 changed entries, but **only these 11 are from this work.**
Everything else in the tree (log_analyzer.html, platformio.ini, experiments/,
docs/paper/, sf12/, etc.) is PRE-EXISTING and unrelated — do NOT touch/commit it.

Ground-truth diff of my tracked-file edits: **`docs/design/slot_allocation_changes.diff`**
(regenerate with the `git diff -- <files>` in §7 if line numbers drift).

Modified (8, tracked):
- `src/protocols/lora_mesh/services/network_service.cpp`  (fan-in tally, EvaluateDataSlotDemand, HandleSuperframeStart hook)
- `src/protocols/lora_mesh/services/network_service.hpp`  (fan-in/hold-down members + decl)
- `src/protocols/lora_mesh/services/slot_scheduler.cpp`   (stable-frame sizing + round-robin fair fill)
- `src/types/configurations/protocol_configuration.hpp`   (default_data_slots 2, max_data_slots 100, load_aware flag)
- `src/protocols/lora_mesh/interfaces/i_network_service.hpp` (NetworkConfig mirror)
- `src/protocols/lora_mesh_protocol.cpp`                  (CreateServiceConfig wiring x2)
- `test/types/test_configurations/protocol_configuration_test.cpp` (default-value assertions)
- `test/CMakeLists.txt`                                   (600s timeout for test_network_stress)

New/untracked (created by this work):
- `test/protocols/lora_mesh/services/test_network_stress/{mixed_traffic_stress_test.cpp,stress_metrics.hpp,test_network_stress.cpp}`
- `docs/design/{SESSION_HANDOFF_slot_allocation.md,load_aware_slot_allocation.md,delivery_packet_loss_analysis.md,slot_allocation_changes.diff}`
- `todos/reliable_multihop_ack_zero.md`

Nothing is committed. To checkpoint just this work without the noise:
`git add <the 8 files + the new dirs/files above> && git commit`.

## 1. Original request & the (reframed) success criterion

Original ask: build a test with ~25 devices in a topology, all periodically
sending reliable + non-reliable + group-with-ACK messages; see if the network
collapses and how long end-to-end delivery takes. User also wanted to change
default data slots to 2–4 and use demand-driven slot requests for relays.

**Success criterion (user reframed this late, and it governs everything):**
> Every originated packet must be DELIVERED within *some* bounded number of
> superframes, and no queue may grow unboundedly. **Latency does NOT matter.**

## 2. What is DONE and solid

- **25-node mixed-traffic stress test** — new suite
  `test/protocols/lora_mesh/services/test_network_stress/`
  (`mixed_traffic_stress_test.cpp`, `stress_metrics.hpp`, `test_network_stress.cpp`).
  - Topology: 5 clusters of 5 on a backbone line (star clusters on cluster-heads;
    heads in a line; NM = centre head; worst-case 6 hops). `TEST_P` over
    `{node_count, data_slots, backbone_loss, load_aware, enforce, label}`.
  - Traffic: leaves→head non-reliable telemetry; cross-backbone reliable unicast;
    NM→group reliable (GROUP_ALL, GROUP_BACKBONE). Paced by round.
  - Measurement: idle "settle" (kSettleSuperframes=20) so measurement is
    steady-state, then measured rounds. Prints a `STRESS SCORECARD`, a
    `##METRICS## {json}` line, and a `##ALLOC## N<i> local=.. tx=.. rx=.. sleep=..
    frame=..` per-node diagnostic dump after settle.
  - Enforce cells gate on non-reliable PDR + relay-queue depth (NOT reliable PDR —
    see the separate bug). CTest timeout for this dir raised to 600s in
    `test/CMakeLists.txt`.
- **Confirmed the RED**: at flat base=1 the multi-hop network collapses (reliable
  7%, relay queue saturated ~9/10) while 1-hop non-reliable survives (~87%). This
  proved the "relay queue grows to infinity on multi-hop paths" thesis.
- **Config changes**: `default_data_slots` 1→2 and `max_data_slots` 50→100 in
  `src/types/configurations/protocol_configuration.hpp` (member + ctor default),
  `src/protocols/lora_mesh/interfaces/i_network_service.hpp` (NetworkConfig),
  wired in `src/protocols/lora_mesh_protocol.cpp` CreateServiceConfig(+ForTest).
  Config test assertion updated in
  `test/types/test_configurations/protocol_configuration_test.cpp:274-276`.
  **This is the part that actually fixes queue collapse** (see §4).
- **Durable design docs** (keep these updated — user directive #1):
  - `docs/design/load_aware_slot_allocation.md` — full design history + the 3
    rejected approaches (traffic-adaptive churn; grow-only; NM-central).
  - `docs/design/delivery_packet_loss_analysis.md` — root cause of the ~25% 1-hop
    loss (half-duplex RX + collisions) with code locations. **This is the handoff
    for the delivery-fix work.**
  - `todos/reliable_multihop_ack_zero.md` — the separate multi-hop reliable/group
    ~0% bug (P0, investigate next).

## 3. The full journey (so we don't repeat rejected paths)

Design was chosen by a panel (3 proposers + consensus): topology-derived
single-authored allocation. It was then implemented and **empirically disproven**
step by step:
1. Traffic-adaptive per-superframe allocation → churn → reliable 0% (WORSE). Any
   allocation that changes often misaligns the TDMA layout network-wide.
2. Grow-only high-water mark → still churned non-reliable 87%→51%, never drained
   queue.
3. Growing the frame to fit escalations → NM didn't re-size / propagation didn't
   converge → whole clusters got 0 TX slots (non-reliable 8%).
4. Stable-frame redistribution (fan-in decides who owns a FIXED pool; frame never
   grows) → **killed the churn** (non-reliable recovered to 87.5%) but relays
   still got only 1 slot because pool = base·N with floor=base ⇒ zero surplus.
5. Fixes A+B+C: (A) exclude route-to-self from fan-in tally; (B) origination
   FLOOR=1 decoupled from pool multiplier (`default_data_slots`); (C) test at
   base=2. → Relays finally got extra slots (heads tx≈6, leaves tx=1), relay
   queue drained. BUT this REVEALED load-aware is net-negative (see §4).

The current CODE reflects state #5 (fixes A+B+C are in `network_service.cpp` and
`slot_scheduler.cpp`). See §5 for exact locations.

## 4. The decisive evidence (why DROP load-aware)

All at `default_data_slots=2`:

| metric | 10n flat | 10n load-aware | 25n flat | 25n load-aware |
|---|---|---|---|---|
| relay queue (/10) | 2.0 | 0.21 | **1.28** | 0.23 |
| non-reliable PDR | 88.7% | 56% | 75% | 75% |
| non-reliable latency p95 | 11.9s | 69.4s | 24s | 209s |
| group ACK | 56% | 41% | 30% | 48% |
| reliable PDR | 50% | 0% | 0% | 0% |

Findings (an agent verified the mechanism from code + logs):
- **`default_data_slots=2` alone keeps the relay queue bounded even at 25n/6hops
  (1.28/10, never saturated).** The relay-starvation collapse was a base=1
  phenomenon; base=2 solves it. Queues bounded = user criterion #2 met by base=2.
- **Load-aware does NOT improve delivery** (75%→75%): the 1-hop non-reliable loss
  is receiver-side PHY (half-duplex + collisions at the head), not relay capacity.
  The relay was never the bottleneck.
- **Load-aware VIOLATES "queues not collapsing"**: all traffic shares ONE FIFO TX
  queue drained 1 pkt/slot; cutting leaves 2→1 slot pushes leaf utilisation ρ
  from ~0.35 to ~0.7 (>1 for reliable-endpoint leaves → unbounded growth). The
  stress metric only sampled HEAD queues, so it was blind to the leaf collapse.
- Latency cost is structural/steady-state (M/D/1 math matches), not a transient —
  but latency is irrelevant per the user anyway.

Conclusion: under the user's own criteria, load-aware is strictly worse than plain
base=2. Drop it.

## 5. Exact code changes currently on the branch (to keep or revert)

Load-aware machinery (recommend REMOVING on confirmation):
- `src/protocols/lora_mesh/services/network_service.hpp`: members
  `downstream_fanin_this_superframe_`, `pending_data_slot_target_`,
  `data_slot_target_stable_count_`, consts `kMaxDataSlotsPerNode`,
  `kDataSlotHoldDownSuperframes`; decl `EvaluateDataSlotDemand()`.
- `src/protocols/lora_mesh/services/network_service.cpp`: fan-in tally in
  `ProcessRoutingTableMessage` (~line 340, `entry.next_hop==node_address_ &&
  entry.destination!=node_address_`); `EvaluateDataSlotDemand()` +
  `FanInSlotBonus` anon-namespace helper (after `SetLocalAllocatedDataSlots`);
  call to `EvaluateDataSlotDemand()` in `HandleSuperframeStart` (after the
  RemoveInactiveNodes block).
- `src/protocols/lora_mesh/services/slot_scheduler.cpp`: (a) `ComputeBandSizes`
  overrides `plan.total_data_slots = default_data_slots * allocated_control_slots_`
  clamped to max_data_slots (STABLE frame — this part is arguably worth KEEPING
  even without fan-in, since it decouples frame size from per-node allocation);
  (b) `FillSlotTable` Phase-3 rewritten as a demand-sorted round-robin fair fill.
- `src/types/configurations/protocol_configuration.hpp`: `load_aware_data_slots_`
  flag + get/set (used by the test to toggle flat vs load-aware).
- `i_network_service.hpp` NetworkConfig `load_aware_data_slots`;
  `lora_mesh_protocol.cpp` wiring.

KEEP regardless (the actual fix): `default_data_slots=2`, `max_data_slots=100`.

Dead code the consensus said to also delete (never done, low priority):
`SendSlotRequest`, `ProcessSlotRequest`, `ProcessSlotAllocation`,
`BroadcastSlotAllocation` + dispatch cases in `network_service.cpp`.

## 6. The REAL remaining work (to hit "all packets delivered")

See `docs/design/delivery_packet_loss_analysis.md` for detail. Summary:
1. **~15% half-duplex RX-drops**: head is TRANSMITTING (its own slot) when a leaf
   transmits to it → deaf → drop (`DeliverMessage` !CanReceive, logged
   `receiver unavailable (state=4)`). Fix in the SCHEDULE: guarantee a receiver is
   in RX (not TX/busy) at the slot where a sender transmits to it. Look at
   `SlotScheduler::FillSlotTable` Phase-3 placement (`slot_scheduler.cpp` ~254-330)
   — do heads' TX slots overlap their leaves' TX slots? Are RX slots assigned for
   every neighbour's TX?
2. **~10% collisions**: two nodes transmit to the same head in the same slot
   (`DetectAndFilterCollisions`). Proper TDMA gives each sender→receiver a distinct
   slot; check whether two leaves of one head can share a data-slot index.
3. PHY model itself (`test/utils/network_testing_impl.hpp` ~735-878) is realistic;
   the fix belongs in the schedule, not the model.
4. SEPARATE: multi-hop reliable/group ~0% (`todos/reliable_multihop_ack_zero.md`)
   — likely ACK-return path / reliable-timeout too short for 62.5s superframes at
   6 hops. Investigate after the 1-hop loss is fixed.

## 7. How to build / run / iterate (and gotchas)

```bash
# Build the stress suite only (no run) — compiles lib + suite, ~2-2.5 min:
pio test -e test_native -f "protocols/lora_mesh/services/test_network_stress" --without-testing
# Binary:
.pio/build/test_native/program
# Run specific cells (10n cells ~40-55s each; 25n cells ~9 min EACH):
.pio/build/test_native/program --gtest_filter='*10n_flat:*10n_loadaware' > /tmp/run.log 2>&1
# Extract signal from the (huge, verbose) log:
grep -aE "  OK |FAILED |STRESS SCORECARD|##METRICS##|##ALLOC## N" /tmp/run.log
```

GOTCHAS (learned the hard way):
- **25-node runs take ~9 min per cell.** Use timeouts ≥ 1500s for both 25n cells.
  Iterate on 10n; only run 25n for final evidence.
- **Do NOT chain `until grep ... done` wait-loops on a marker that isn't in the
  file** — several got stuck forever. If you background a build, rely on the
  harness completion notification, and do NOT pipe build output through `tail`
  (it strips the `Took`/marker lines a waiter greps for).
- The run log is multi-MB and verbose (file logging is on); never `cat` it — grep
  with `-a`.
- Don't edit source while a `pio` build is running (inconsistent binary).
- Config default is now 2 but the test PARAM overrides it per cell; make sure a
  load-aware cell uses `data_slots=2` or there's no surplus to redistribute.

## 8. Process directives from the user (persist these behaviours)

1. **Document every protocol change's WHY in `docs/design/*.md`** as you go, to be
   folded into `PROTOCOL_SPEC.md` later. (Also saved as memory
   `feedback_protocol_change_workflow`.)
2. **Delegate thinking/analysis to spawned agents** to preserve main-thread
   context (sessions get long / compressed).
3. Panels + a consensus agent for design forks; the consensus "returns what should
   be done."

## 9. If PROTOCOL_SPEC.md gets updated (was Phase 7, not started)

- §10.5.1 "Dynamic Slot Allocation" (planned) — do NOT promote it if load-aware is
  dropped; leave as planned or note it was evaluated and rejected.
- §5.4.2 "Dynamic Reallocation" has stale struct drift (shows SLOT_REQUEST 0x23;
  real is 0x44) — fix regardless.
- §5.8.2 formula `Data Slots = N × data_slots_per_node` — with the stable-frame
  approach this is `default_data_slots × N` (clamped to max_data_slots); update if
  that part is kept.
- `default_data_slots` default 1→2, `max_data_slots` 50→100 in any spec text.

## 10. Immediate next action when resuming

Follow the phase list in §0. Commit each phase separately; stage only files from
this work.
