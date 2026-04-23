# Slot-table underflow defenses (Fix B + Fix C)

Follow-ups to the root-cause fix in `NetworkService::GetAllocatedDataSlots()`
that filtered out inactive routing entries. The root-cause fix eliminates the
known trigger for the 48 > 47 underflow seen in `experiments3/`, but the two
defenses below protect against future divergence sources (e.g. heterogeneous
`allocated_data_slots` from `SlotRequestMessage`, buggy peer routing tables,
or off-by-one bugs we haven't found yet).

## Context

`src/protocols/lora_mesh/services/network_service.cpp`:

- `UpdateSlotTable()` at line ~1948 computes
  `sleep_slots = total_superframe_slots - total_active_slots` with `uint8_t`
  at line ~2062. No guard — any overrun wraps to 255 and later trips the
  out-of-bounds checks in the slot-allocation phases.
- `ProcessSyncBeacon()` at line ~2966 calls `UpdateSlotTable()` BEFORE
  `PerformTimingSynchronization()` (line ~2984). On `UpdateSlotTable` failure
  it returns early, so timing sync is skipped — a one-shot slot-table build
  error becomes a permanent one-slot desync.

## Fix B — underflow safety guard

In `UpdateSlotTable()` around line 2062, before the subtraction:

```cpp
if (total_active_slots > total_superframe_slots) {
    LOG_WARNING("Slot budget exceeded: active=%u > total=%u (data=%u)",
                total_active_slots, total_superframe_slots, total_data_slots);
    return Result(LoraMesherErrorCode::kInvalidState,
                  "Slot budget exceeded");
}
uint8_t sleep_slots = total_superframe_slots - total_active_slots;
```

Returning before reaching line 2201 (`slot_table_dirty_ = false`) means the
dirty flag stays set and the next beacon retries. No slot-table state is
committed on the error path.

Why it's worth keeping even after Fix A: the guard is ~4 lines, it turns a
silent 255-sleep-slot bug into a clear WARNING, and it catches any future
divergence source (not just inactive ghost entries).

## Fix C — preserve timing sync across slot-table failures

In `ProcessSyncBeacon()` at line ~2966-3001, reorder so
`PerformTimingSynchronization()` runs FIRST, unconditionally. Then attempt
`UpdateSlotTable()`. On failure, log a WARNING and continue — do NOT return.
`slot_table_dirty_` stays true so the next beacon retries.

`ForwardSyncBeacon()` continues to be invoked inside the `pre_start_action`
callback of `PerformTimingSynchronization()`.

Safety: `PerformTimingSynchronization()` reads only `total_slots`,
`slot_duration`, `superframe_duration` from the beacon message
(`network_service.cpp:840-847`). It never consults `slot_count_` or
`slot_table_`, so reordering is safe. `UpdateSuperframeConfig()` in
`superframe_service.cpp` has no dependency on the slot table either.

Residual behavior: a node running a stale slot table for 1-2 superframes
until retry succeeds may TX in data slots the NM has reassigned.
`my_control_slot_index_` only changes via `JoinResponse` (line ~1414), so
control-slot TX remains safe. Data-slot collisions are bounded and self-heal
when `RemoveInactiveNodes` (line ~153) prunes the divergence source —
typically 2-4 superframes bounded by `config_.route_timeout_ms`.

## Verification

- Unit test: seed an oversubscribed routing table so `active > total` even
  after Fix A's active-entry filter. Assert `UpdateSlotTable` returns
  `kInvalidState` with the "Slot budget exceeded" message and that
  `slot_table_dirty_` remains true.
- Unit test: in `ProcessSyncBeacon`, force `UpdateSlotTable` to fail. Assert
  that `PerformTimingSynchronization` was still invoked (superframe service
  synchronized) and that `ProcessSyncBeacon` returns Success.
- Grep hardware logs after a multi-node run: zero occurrences of
  `"SLEEP slot index .* out of bounds"` or `"Failed to update slot table"`.

## Why not now

Fix A alone removes the only reproduction path we've observed. Landing it
first keeps the diff small and lets us verify on hardware whether the
`experiments3/` symptom disappears — which is the cleanest test of the
root-cause hypothesis. Fixes B+C are pure defense-in-depth and can land as a
follow-up once Fix A is confirmed.

## Files touched when these land

- `src/protocols/lora_mesh/services/network_service.cpp` — `UpdateSlotTable()`
  and `ProcessSyncBeacon()`.
- `test/protocols/lora_mesh/services/test_unit_network_coverage/network_service_coverage_test.cpp`
  — unit tests for the guard and the reorder.
