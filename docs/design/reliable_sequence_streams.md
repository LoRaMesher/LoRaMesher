# Reliable Sequence Streams and Group Retries — Design Rationale

Status: **implemented (2026-09-29)** — commits `9c3c00f`, `6d114f6`, `bdebcd8`,
`e613e3b`; PROTOCOL_SPEC §3.2.6 updated. Follows the branch review of
`feature/load-aware-slot-allocation`.

## Problem

One 8-bit per-node counter (`MessageCache::NextSeq`) numbers every packet a node
sends: best-effort DATA, BROADCAST, GROUP, the reliable `msg_seq`, and the fresh
link seq of each retransmission. Reliable delivery is de-duplicated at the
destination in the same 32-entry FIFO `(source, seq)` cache used for link-level
dedup, with no message type and no age.

- **False Delivered.** A quiet receiver D keeps `(S, M)` while S spends 255
  sequence numbers on traffic D never records. S wraps, sends new reliable data
  with `msg_seq = M`, D ACKs it (ACK-before-dedup is intentional, so a lost ACK
  is re-sent) and drops it as a duplicate. S reports `Delivered`; the app never
  sees the data.
- **Best-effort loss.** D records the reliable `msg_seq` in the link cache, so a
  later best-effort DATA/BROADCAST/GROUP from S whose link seq equals it is
  dropped.
- **Why not an age bound.** The retry lifetime is ~(10h+1) superframes with
  backoff, or any length with a caller `timeout_ms`; S can wrap inside it. No
  age bound both outlives every retransmission and expires before a wrap.

`GroupSendOptions.max_retries` is accepted but never used:
`ReliableDelivery::Tick` skips group (`collect_multiple`) entries. Re-flooding
safely needs a delivery dedup that is independent of the link seq, which is the
same mechanism the fix above introduces.

## Design (no wire-format change)

### Sequence streams at the sender
- The node-wide counter numbers **link packets only**. Every attempt of a
  reliable message, including the first, takes a fresh link seq, so relays
  forward each attempt.
- `msg_seq` comes from a **stream**: one counter per unicast destination, and
  one shared counter for all acknowledged group sends (the ACK payload carries
  no group address, so group ACKs are matched by `was_group` + seq).
- The stream table holds every possible peer (254 entries, ~760 B) with no
  eviction: losing a sender counter risks a false `Delivered`. New streams start
  at a random value.
- **Span guard.** A new message is refused (`kInvalidId`, "stream busy") when
  `(uint8_t)(new_seq − oldest_pending_seq_of_stream) >= 32`, so every
  retransmission stays inside the receiver window. `kMaxPending = 8` alone does
  not bound the span: one message can sit in backoff while others to the same
  destination complete.

### MessageId
`MessageId{source, seq, dest}`: `dest` is the unicast destination or the group
address (at a receiver: the local node or the group). Sequence numbers are no
longer unique per source, so `ReliableDelivery` keys entries on all three
fields. `value()` becomes 64-bit; existing `{src, seq}` initialisers still
compile.

### Delivery windows at the receiver
`reliability::DeliveryWindows`: per `(source, kind)` anti-replay window,
`kind ∈ {unicast, group}`, 32 entries with LRU, window 32 (one `uint32_t`
bitmap).

```
Accept(src, kind, seq, ts, regression_ms) -> new?
  no entry            -> create {highest=seq, bitmap=1}; new
  ts < last_ts - regression_ms -> sender restarted: reset; new
  d = (uint8_t)(seq - highest)
  d == 0              -> duplicate
  d < 128             -> ahead: shift bitmap, highest = seq; new
  b = (uint8_t)(highest - seq)
  b >= 32             -> cannot be a retransmission (span guard): reset; new
  bit b set           -> duplicate
  else                -> set bit b; new
```

- Losing a receiver window only risks a duplicate delivery (the safe
  direction), so LRU eviction is acceptable there.
- `send_ts` (sender uptime, already in the reliable prefix) detects a sender
  reboot; `regression_ms` is `MaxReliableTimeout()`.
- Residual risk: a false duplicate still needs 224+ consecutive losses of one
  stream to one receiver.
- Link-level dedup (relays, flood loop prevention, best-effort delivery) keeps
  using `MessageCache` unchanged. The reliable prefix `msg_seq` is no longer
  recorded there.

### Group retries
- `interval = max(window_ms / (max_retries + 1), superframe)`, no backoff.
  Attempts at 0, I, …, R·I, each a full re-flood with a fresh link seq. The
  window still closes at `window_ms`.
- `Tick` retransmits group entries while `retries_left > 0` and never reports
  `Failed` for them.
- Retries never stop early: the sender does not know the member set.
- Members ACK every copy and deliver once (delivery window); each responder is
  counted once (`RecordResponder`).
- Cost: (R+1) floods plus up to (R+1)·members ACKs.

### Locking
`DeliveryWindows` and the stream table live in `ReliableMessaging` under its
mutex. `AcceptReliable` calls no host closure, so it adds no lock-order edge.
Streams and windows are not reset in `ResetNetworkState` (which holds
`network_mutex_`; `ReliableMessaging::mutex_ → network_mutex_` already exists
via `FindNextHop`), and keeping the counters across a network reset is the safe
direction anyway.

## Commits
1. `MessageId` carries the destination.
2. Receiver `DeliveryWindows` (standalone, unit-tested).
3. Per-destination `msg_seq`, fresh link seq per attempt, window-based dedup.
4. Group retransmission inside the ACK window.
5. PROTOCOL_SPEC / group-ACK spec / README.
