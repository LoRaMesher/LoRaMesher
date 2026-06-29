# Memory Model — making the code lighter

> ESP32 RAM is scarce and heap fragmentation has caused field crashes (a prior fix already
> moved `BaseMessage` payloads to a fixed buffer and queues to fixed arrays). This doc traces
> per-message RAM cost end-to-end and targets the remaining copy chain. Drives WS-4.

## What is already good (do not regress)

- `BaseMessage` payload is a fixed `uint8_t payload_data_[255]` + `uint8_t payload_size_` —
  no per-message heap for the payload itself (`base_message.hpp:233`, `kMaxPayloadSize=255`).
- `NetworkService::slot_table_` is `std::array<SlotAllocation,256>` — no heap.
- `message_cache_` (32), `reliable_dest_`, `group_windows_`, `groups_` (8) are fixed arrays.
- Radio RX uses a single static `rx_buffer_[256]`.
- Message queues are fixed arrays indexed by slot type.

## The remaining problem: the TX/forward triple copy

A typed message still carries a **heap** `std::vector<uint8_t> payload_`
(`data_message.hpp`, `broadcast_message.hpp`, `group_message.hpp`), and sending/forwarding
copies the payload three times:

```
1. typed message holds   std::vector<uint8_t> payload_      (heap, ~payload bytes)
2. ToBaseMessage()       std::array<uint8_t,255> buf{}      (255 B stack, every call)
3. make_unique<BaseMessage>(... )                            (heap, copies 255 B in)
```

There are **~23** `make_unique<BaseMessage>(typed.ToBaseMessage())` sites in
`network_service.cpp` plus a few elsewhere. On a forwarded packet, peak transient RAM is
~1.4 KB and each hop does an allocate/free cycle — the churn that drives fragmentation.

### Per-message RAM trace (forward path)

| Stage | Live allocations | Approx bytes |
|---|---|---|
| RX | `rx_buffer_` (static) + queued `BaseMessage` | 256 + ~310 |
| Process | `BaseMessage` + typed message (`vector` payload + object) | ~310 + ~200 + ~60 |
| Forward | new typed message + 255-B stack array + new `BaseMessage` | ~260 + 255 + ~310 |
| **Peak** | | **~1.4 KB transient + 2 heap alloc/free per hop** |

## Optimizations (WS-4), highest impact first

### 1. One `EnqueueMessage` helper, constructed in place (kills sites #3)
Replace the 26 `make_unique<BaseMessage>(msg.ToBaseMessage())` call sites with a single
helper that builds the `BaseMessage` directly into the queue entry (move, not copy).
Removes one 310-B heap copy per send. *(This helper is also the seam used by WS-5 phase 2.)*

### 2. Typed-message payload as a span/offset, not a heap vector (kills sites #1)
Where the typed message's lifetime is bounded by the owning `BaseMessage` (the common
process/forward case), back the payload with `std::span<const uint8_t>` into the
`BaseMessage` payload instead of a `std::vector` copy. Removes the per-message heap vector
for `DataMessage` / `BroadcastMessage` / `GroupMessage`. Keep a `vector`-owning constructor
only for the application-origin path where the caller's buffer isn't retained.

### 3. Avoid the 255-B stack array in `ToBaseMessage` (mitigates #2)
Serialize fields directly into the destination `BaseMessage` buffer (the `EnqueueMessage`
helper exposes it) rather than into a temporary `std::array<uint8_t,255>`. Saves 255 B of
stack per conversion — relevant on small FreeRTOS task stacks.

### 4. Return spans/const-refs from copying getters
`GetNetworkNodesCopy()` returns a full `std::vector<NetworkNodeRoute>` (~3 KB for 50 nodes).
Provide a span/const-ref accessor for read-only callers; keep the copy only where the caller
needs an owned snapshot.

### 5. (ping_pong, low priority) replace the nested `unordered_map`
`ping_pong_protocol.hpp` tracks pending pings in
`unordered_map<AddressType, unordered_map<uint16_t, PendingPing>>`. For a test/example
protocol this is acceptable, but a fixed-capacity table removes the hash-map heap + nesting.

## Sequencing & safety

- WS-4 overlaps WS-5 phase 2: build `EnqueueMessage` **once** (here) and reuse it in the
  decomposition.
- These are behavior-preserving on the wire; gate with the unit nets + one slow integration
  run on the forward path. Add a targeted test asserting forwarded-message bytes are
  unchanged.
- Span-backed payloads introduce **lifetime constraints** — the typed message must not
  outlive its `BaseMessage`. Audit each call site; keep the owning-vector constructor for the
  application-origin path. This is the one place to move carefully.

## Estimated effect
Per forwarded packet: from 2 heap alloc/free + ~1.4 KB transient down to ~1 (or 0) heap
alloc + ~565 B less transient. Largest win is reduced **fragmentation pressure**, the actual
historical failure mode — not just byte count.

## Validation
- `kMaxPayloadSize=255` and `payload_data_[255]` confirmed at `base_message.hpp:39,233`.
- Heap `std::vector<uint8_t> payload_` confirmed in data/broadcast/group message headers.
- 255-B stack arrays confirmed present in every `*_message.cpp` `ToBaseMessage`.
