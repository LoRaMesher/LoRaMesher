# Follow-up: High-SF (SF11/SF12) larger-payload & contention redesign

Status: deferred (not needed for the SF12 fixes in commit after 004c6e1)

## Why
At SF12/BW125 one packet's airtime (~1.5–3.9 s) is about a whole TDMA slot, so:
- A slot can carry only ONE transmission. Subslots within a slot don't help (the
  message is bigger than any sub-window). We currently collapse subslots to 1 at SF12.
- The data MTU is small (~41 B payload at SF12). Bigger app payloads are rejected.

## Options for later (each is a bigger change)
1. Non-uniform slot durations: give discovery/sync slots more time than data slots.
   Needs per-slot duration (today slot_duration is one global value used by
   GetCurrentSlot/GetSlotStartTime/GetTimeInSlot/SynchronizeWith) + a sync-beacon wire
   format change. Est. ~1–2 weeks.
2. Slot-level contention: pick a random discovery SLOT (not subslot) at high SF, so
   joining nodes don't collide. Touches discovery/join scheduling.
3. DATA fragmentation: split payloads > MTU into ≤MTU fragments, reassemble at the
   destination (sequence numbers, reassembly, partial-loss handling).

## Decision
Ship the easy fixes first (hard MTU enforcement + ToA-aware message-sized subslots).
Revisit the above only if SF12 deployments need larger payloads or faster join
convergence under heavy contention.

## Also deferred: systematic SF sweep in tests
We added a few SF12 topology tests to the fast suite. A full SF7–SF12 sweep across all
topologies (parameterized fixtures) is deferred — it would push per-commit CI to hours
because high-SF superframes are ~5–9x longer. If wanted later: make RoutingTestFixture
parameterized on SF and run it as a nightly/on-demand job, not per commit.
