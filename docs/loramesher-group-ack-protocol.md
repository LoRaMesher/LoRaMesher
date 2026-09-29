# LoRaMesher 1.0.0 — GROUP Multicast + Reliable ACK Delivery

**Status:** Proposed specification / cross-repo implementation handoff
**Target repo:** `loramesher` (LoRaMesher 1.0.0), implemented by a separate engineer/agent
**Consumer:** PingR firmware (`src/lora/lora_manager.cpp`)
**Audience:** the implementer working inside the LoRaMesher codebase
**Last updated:** 2026-06-26

---

## 1. Purpose & context

PingR is a LoRa mesh messaging/navigation product. Today it hand-rolls two things **on top of**
LoRaMesher, at the application layer:

1. **Group messaging** — a 16-bit `group_id` is embedded in every app packet; every node receives
   every broadcast and *filters by `group_id` in software* (`lora_manager.cpp`, `GroupPacket`).
2. **Acknowledged delivery** — the sender keeps a pending table and **retransmits up to 3× over 30 s**
   in a dedicated FreeRTOS task (`taskRetryAck`), matching `GroupAck` replies by `msg_id`.

We want both to become **first-class LoRaMesher features** so that reliability is owned, tested, and
reused at the library layer (and so the Phase 3 distributed delivery-test harness can consume
library-level delivery callbacks instead of bespoke app logic). This document specifies the required
behaviour, a concrete wire format and API, the exact extension points in the LoRaMesher source, and
the `VirtualNetwork` acceptance tests that define "done".

This spec is the **contract** between the two repos. PingR will migrate `lora_manager.cpp` against the
API defined in §8. If the library work lags, PingR can keep its app-layer shim temporarily, but the
API surface here should be treated as stable.

---

## 2. What PingR does today (the behaviour being pushed down)

From `PingR/src/lora/lora_manager.cpp` and `lora_types.h`:

- **Group send (`GroupPacket`)**: `dst = BROADCAST_ADDR`, fields include `group_id`, `msg_id`,
  `hop_count = 3`, and a text payload (`"lat,lon"`). Sent via the old `sendGroupPacket()`. Every node
  receives it; RX handler **drops packets whose `group_id` ≠ local group**.
- **ACK (`GroupAck`)**: a receiver that accepts a `GROUP_MSG_P` replies with a unicast `GroupAck`
  (`type = GROUP_ACK_P`, `msg_id`) back to the source.
- **Retry (`taskRetryAck`)**: the sender tracks up to 4 pending messages; every 5 s it retransmits any
  message older than 30 s that has not been ACKed, up to 3 attempts, then gives up.
- The sender **displays which peers ACKed** (history/ACK pages). It does **not** know a fixed expected
  recipient set — it reports whoever replies. This nuance matters for §6.

**Implication for the design:** PingR needs (a) **reliable unicast** (true ACK + retransmit to a known
destination) and (b) **group multicast with per-recipient ACK reporting** (members that receive it ACK
back; the sender reports the responding set). These are two related but distinct capabilities.

---

## 2A. Context: PingR's current behaviour (reference only)

**This spec designs F1/F2 natively for LoRaMesher 1.0.0** (`/mnt/d/Projects/ESP32/loramesher`). It is
**not** a port of any older code. For background only, PingR's *current* group/ACK behaviour — an
older app-layer/fork implementation — is documented in `PingR/LMFork/FIRMWARE_REPORT_EN.html`. Read it
to understand what PingR does today; do **not** copy its mechanisms into 1.0.0 (1.0.0's routing, dedup
and protocol model differ, and several fork workarounds — e.g. return-route injection — are unnecessary
here because 1.0.0 makes all nodes routable). One useful corroboration from that report: its §11.12 #3
independently proposes the *same* generic "PendingAck + retry primitive" this spec specifies as the
`ReliableDelivery` component (§6.0).

---

## 3. Scope

In scope for this work item:

- **F1 — Group (multicast) addressing**: join/leave logical groups; send to a group; only members
  deliver to their app callback.
- **F2 — Reliable delivery (ACK + retransmit)**: library-owned delivery confirmation for unicast, with
  bounded retransmission and a per-message outcome callback (delivered / failed) including latency.
- **F3 — App-visible message IDs**: expose a stable per-message identifier so the application and the
  delivery-test harness can correlate sends, ACKs, loss and ordering.
- **F4 — Acceptance tests** in the `VirtualNetwork` simulator (§10).

Out of scope (see §11): routed (non-flood) multicast, group-membership propagation across the mesh,
encryption/auth, fragmentation of payloads larger than one MTU.

---

## 4. Relevant LoRaMesher internals (map for the implementer)

All paths relative to the `loramesher` repo. Verified against the current tree.

**LoRaMesh protocol & service architecture (read first)**
- `LoRaMeshProtocol` (`src/protocols/lora_mesh_protocol.{hpp,cpp}`) is the mesh protocol all of F1–F3
  attach to. `Init` (`lora_mesh_protocol.cpp:101-117`) constructs and **injects** its services by
  `shared_ptr`: `MessageQueueService`, `SuperframeService`, `NetworkService` (+ a
  `DistanceVectorRoutingTable`). Interfaces: `i_message_queue_service.hpp`, `i_network_service.hpp`,
  `i_superframe_service.hpp`, `i_routing_table.hpp`. New reliability/group state composes into
  `NetworkService` (see §6.0).
- Transport is **TDMA slot-gated**: a send does not hit the radio synchronously — it enqueues to a TX
  slot (`message_queue_service_->AddMessageToQueue(SlotType::TX, …)`, `network_service.cpp:1835`) and is
  transmitted later under slot control. A reliability mechanism therefore must **not** wrap the radio —
  it must drive the mesh's slot-gated send (see §6).

**Addressing & headers**
- `src/types/messages/base_header.hpp`: `using AddressType = uint16_t;`,
  `kBroadcastAddress = 0xFFFF` (line ~23). BaseHeader is 6 bytes:
  `[dest:2][src:2][type:1][payload_size:1]` (`Size()` ~line 128; `Serialize()` in
  `base_header.cpp:39`).
- `src/types/messages/message_type.hpp`: `enum class MessageType : uint8_t` (lines ~24-57). Categories
  `DATA_MSG 0x10`, `CONTROL_MSG 0x20`, `ROUTING_MSG 0x30`, `SYSTEM_MSG 0x40`. **`ACK = 0x21` is already
  defined but unused.** `DATA = 0x11`, `DATA_BROADCAST = 0x12`.
- `src/types/messages/loramesher/data_header.hpp`: DataHeader adds `next_hop:2`, `ttl:1`, `seq_num:1`
  (10-byte wire format; fields ~lines 103-105).
- `src/types/messages/loramesher/broadcast_message.hpp`: flood format
  `[next_hop=0xFFFF:2][ttl:1][seq_num:1][payload...]`; `Create(src, ttl, seq_num, payload)`.
- Validation gate when adding a type: `base_header.cpp:93` (`IsValidMessageType`).

**Send path**
- `LoRaMeshProtocol::SendData(dest, data)` → `NetworkService::SendData()`
  (`src/protocols/lora_mesh/services/network_service.cpp:1767`): validates MTU, `FindNextHop(dest)`,
  `message_seq_++`, builds `DataMessage::Create(dest, src, next_hop, data, ttl, seq)`, queues to the
  `TX` slot via `message_queue_service_->AddMessageToQueue(...)`.
- `LoRaMeshProtocol::SendBroadcast(span)` → `NetworkService::SendBroadcast()`.
- Per-node sequence counter: `message_seq_` (`network_service.hpp:~1282`), shared by unicast+broadcast.

**Receive path**
- Radio ISR → `radio_event_queue_` → protocol task → `ProcessDataMessage()`
  (`network_service.cpp:1642`).
- **De-dup**: `IsMessageDuplicate(src, seq)` / `AddToMessageCache(src, seq)` (`network_service.hpp:1145`).
- **Destination filter (THE extension point for F1)**: `network_service.cpp:1681`
  ```cpp
  if (next_hop != node_address_) {                 // not the next hop → ignore
      LOG_DEBUG("DATA not for this node ...");
      return Result::Success();
  }
  ...
  if (final_dest == node_address_) {               // we are the final destination
      if (data_received_callback_) data_received_callback_(original_src, data_msg.GetPayload()); // ~1698
  } else {
      return ForwardDataMessage(data_msg);          // relay
  }
  ```
- App callback type: `i_network_service.hpp`:
  `using DataReceivedCallback = std::function<void(AddressType source, const std::vector<uint8_t>&)>;`


**Capability propagation (reference for any future group propagation)**
- Local caps stored in `local_capabilities_`; sent inside `RoutingTableMessage`
  (`network_service.cpp:750` send, `:200` receive via `GetSourceCapabilities()`).

**Protocol task / timer**
- Event-driven task (`ProtocolTaskFunction`) wakes on notifications and a periodic
  `QUEUE_WAIT_TIMEOUT_MS` (~100 ms). A retransmit check can piggyback here or add a
  `RETRANSMIT_CHECK` notification.

**Build/test**
- `CMakeLists.txt`: **C++20**, RadioLib **7.1.2** via FetchContent. Tests: `BUILD_DESKTOP=ON` →
  `ctest`, or `pio test`. Integration tests under
  `test/protocols/lora_mesh/services/test_integration/`. Harness: `test/utils/network_testing_impl.hpp`
  + `lora_mesh_test_fixture.hpp`.

---

## 5. F1 — Group (multicast) addressing

### 5.1 Address space
Partition the existing 16-bit `AddressType`:

| Range | Meaning |
|-------|---------|
| `0x0000` | reserved / invalid |
| `0x0001 – 0x7FFF` | unicast node addresses (unchanged) |
| `0x8000 – 0xFFFE` | **group / multicast addresses (new)** |
| `0xFFFF` | broadcast (unchanged) |

Add helpers in `base_header.hpp` (or an `address_utils.hpp`):
```cpp
constexpr bool IsGroupAddress(AddressType a)     { return a >= 0x8000 && a <= 0xFFFE; }
constexpr bool IsUnicastAddress(AddressType a)   { return a >= 0x0001 && a <= 0x7FFF; }
```
Node address generation (`GenerateAddressFromHardware`) must mask into the unicast range so a node can
never self-assign a group/broadcast address.

### 5.2 Membership (local-only in v1)
Group membership is **local state**, not propagated across the mesh (see §11 for why this is enough):
```cpp
// network_service.hpp (private)
std::unordered_set<AddressType> local_group_memberships_;
std::mutex group_memberships_mutex_;
```
A node delivers a group message to its app callback **iff** it is a member of that group.

### 5.3 Delivery semantics — flood + membership-gated delivery
Group data reuses the **flooding** transport (like `DATA_BROADCAST`) but carries a **group destination**
instead of `0xFFFF`:

- TX: `SendGroup(group, data)` builds a broadcast/flood message with `BaseHeader.destination = group`,
  `ttl` from config, `message_seq_++`.
- RX in `ProcessDataMessage` (`network_service.cpp:1681`): when `IsGroupAddress(final_dest)`:
  1. **De-dup** by `(original_src, seq)` exactly as today (prevents flood loops).
  2. **Forward** the flood to neighbours if `ttl > 0` (so the group message propagates regardless of
     local membership) — same relay path as broadcast.
  3. **Deliver** to `data_received_callback_(original_src, payload)` **only if**
     `IsMemberOfGroup(final_dest)`.

  Non-members relay but do not deliver. Members both relay and deliver. This is the minimal change to
  the single filter point and preserves existing loop-prevention.

**ACK routing.** The per-recipient ACK (§6.2) and reliable-unicast ACK (§6.1) are ordinary unicasts back
to `original_src`. In 1.0.0 every node becomes routable once the routing table converges, so the ACK
routes back via the **normal routing table** — no special reverse-routing mechanism is needed.

### 5.4 API (see also §8)
```cpp
Result JoinGroup(AddressType group);     // group must satisfy IsGroupAddress()
Result LeaveGroup(AddressType group);
std::vector<AddressType> GetGroups() const;
bool   IsMemberOfGroup(AddressType group) const;
Result SendGroup(AddressType group, std::span<const uint8_t> data, GroupSendOptions opts = {});
```

### 5.5 Edge cases
- Sending to a group the sender is not a member of **is allowed** (a coordinator may push to a group it
  doesn't belong to). The sender does not self-deliver.
- `JoinGroup` with a non-group address → `Result` error (`kInvalidArgument`).
- Membership changes take effect immediately and locally; no settle time.

---

## 6. F2 — Reliable delivery (ACK + retransmit)

### 6.0 Architecture — reliability is a standalone injected component, not protocol code

**Do not** bake the pending-table / retransmit logic inline into `NetworkService`. Extract the
reliability *state machine* into a small, standalone, dependency-injected component with **no**
dependency on the radio, routing, the message queue, or the superframe. Everything mesh-specific stays
in **host-supplied closures** that `NetworkService` provides, so the component leaks nothing and stays
unit-testable in isolation.

```cpp
// src/protocols/reliability/reliable_delivery.hpp   (new leaf module; no mesh/radio includes)
namespace loramesher::protocols::reliability {

struct MessageId { AddressType source; uint8_t seq; AddressType dest; uint64_t value() const; };
enum class Outcome { Delivered, Failed };
struct DeliveryResult { MessageId id; Outcome outcome; AddressType by = 0; uint32_t rtt_ms = 0; };
using DeliveryCallback = std::function<void(const DeliveryResult&)>;

// The ONLY contract a host protocol must satisfy. Both map to primitives that
// already exist (see table below) — the component adds nothing new to the host.
struct Host {
    // Transmit ONE attempt of a tracked message. The mesh host implements this by
    // building a DATA message (FindNextHop + message_seq_) and AddMessageToQueue(TX,…) — slot-gated.
    // Must be non-blocking and must NOT transmit synchronously from an RX context.
    std::function<Result(const MessageId&, std::span<const uint8_t>)> send_attempt;
    std::function<uint32_t()> now_ms;   // monotonic clock (getTickCount / steady_clock)
};

struct Policy { uint32_t timeout_ms = 4000; uint8_t max_retries = 3;
                bool collect_multiple = false; };  // false=unicast erase-on-first-ack; true=group window

class ReliableDelivery {
   public:
    explicit ReliableDelivery(Host host);
    // Host SEND path calls this AFTER it has chosen the seq it will put on the wire.
    Result Track(MessageId id, std::span<const uint8_t> payload, Policy p, DeliveryCallback cb);
    // Host INBOUND path calls this when an ACK frame arrives. Returns false if unmatched (dup/unsolicited).
    bool   OnAck(MessageId acked, AddressType by);
    // Host's EXISTING periodic tick calls this (no new task). Retransmits or fails-out expired entries.
    void   Tick();
    size_t PendingCount() const;        // for leak assertions in tests
};
}
```

The component owns **only**: the pending table keyed by `MessageId`, the retransmit countdown,
ACK-matching, RTT computation, the retained payload, and the outcome callback. It owns **nothing**
else. The four things it needs all already exist in the host, so wiring is a one-line call at four
existing points:

| Hook | Mesh host (`NetworkService`) binds to |
|------|----------------------------------------|
| `send_attempt` | build DATA via `FindNextHop` (`network_service.cpp:312`) + `message_seq_` (`:1813`) → `AddMessageToQueue(TX,…)` (`:1835`) |
| inbound → `OnAck` | new `case MessageType::ACK` in the dispatch switch (`network_service.cpp:586`/`:609`) |
| tick → `Tick` | existing protocol-task ~100 ms loop (`lora_mesh_protocol.cpp` timeout branch) |
| `now_ms` | `GetRTOS().getTickCount()` |

**Placement & ownership.** The component lives in its own leaf dir (`src/protocols/reliability/`).
`NetworkService` (the LoRaMesh protocol's service) **owns one instance**
(`reliability::ReliableDelivery reliable_;`) and supplies the two mesh closures. It is an internal
collaborator of `NetworkService`, surfaced through the public API in §8. Because the component holds no
`IHardwareManager` reference, it is **structurally incapable** of transmitting from an RX context — the
TDMA "no synchronous TX from RX" rule is enforced by construction. Keeping it standalone (rather than
inline in `NetworkService`) is what makes the §10 Tier-A isolation tests possible.

### 6.1 Reliable unicast (true ACK + retransmit)
- `SendReliable(dest, data, opts)` (on `NetworkService`): run the existing `SendData` body — MTU check
  (`network_service.cpp:1789`), `FindNextHop` (`:1802`), `message_seq_++` (`:1813`) — then
  `reliable_.Track({node_address_, message_seq_}, data, policy, cb)`. `Track` records the entry and
  performs attempt #1 via the `send_attempt` closure (which runs the existing TX-slot enqueue at
  `:1835`).
- **On ACK received**: a new `ProcessAckMessage()` handler (added to the dispatch switch at
  `network_service.cpp:609`) deserializes the ACK and calls
  `reliable_.OnAck({original_src, acked_seq}, by)`; the component computes RTT, fires
  `Delivered{by, rtt_ms}`, and erases. ACKs are a distinct `MessageType` and never enter the DATA
  de-dup path.
- **Retransmit / fail**: the existing protocol-task tick calls `reliable_.Tick()`; expired entries are
  re-sent via `send_attempt` (re-enqueued to the TX slot — never a synchronous TX) or, when retries are
  exhausted, fire `Failed` and are erased.
- **Receiver auto-ACK**: at the single delivery point (`network_service.cpp:1697-1698`, only when
  `final_dest == node_address_`, i.e. *after* the de-dup gate so it fires exactly once), the library
  enqueues an `AckMessage` (type `0x21`) back to `original_src` with the acked `seq` and the echoed
  send-timestamp for RTT. App callback firing is the trigger; the ACK is library-generated and
  invisible to the app.

### 6.2 Reliable / acknowledged group send (per-recipient ACK reporting)
For groups the sender does **not** know the member set, so this is **best-effort multicast with
per-recipient ACK reporting**, matching PingR's current behaviour:

- `SendGroup(group, data, {request_acks = true})`: each member that delivers the message **also
  auto-sends a unicast `AckMessage`** back to `original_src` (carrying `seq` and the responder's
  address).
- The sender fires the `DeliveryCallback` **once per received ACK**:
  `callback(MessageId, Outcome::Delivered{by = responder, rtt_ms})`. The application accumulates the
  responder set (this is exactly PingR's "who ACKed" display and the delivery-test harness's per-node
  ratio).
- **Component reuse**: group per-recipient ACKs flow through the **same** `ReliableDelivery` component
  via `Policy::collect_multiple = true` — in that mode `OnAck` fires `Delivered{by}` once per distinct
  responder **without erasing** the entry. Group is otherwise invisible to the component (it never sees
  a group address; the responder ACKs are ordinary unicast ACKs).
- **Retransmit / completion**: the member set is unknown, so there is no per-member guarantee.
  `opts.max_retries` (default 0) re-floods the whole group R times, attempts spread evenly over the
  window and at least one superframe apart; retries do not stop early. Members ACK every copy and
  deliver once (per-stream delivery window, see `docs/design/reliable_sequence_streams.md`); each
  responder is counted once. The host owns the ACK-collection **window timer**; when
  `opts.timeout_ms` elapses it closes the entry and fires
  `Outcome::GroupWindowClosed{ack_count}`. "How many ACKs is enough" is a host policy decision, kept
  out of the generic component.

### 6.3 ACK message wire format
New `AckMessage` (type `MessageType::ACK = 0x21`, already reserved). Payload after BaseHeader:
```
[acked_seq:1][flags:1][echo_timestamp:4]
```
- `BaseHeader.destination` = original message's `source` (unicast back, routed normally).
- `BaseHeader.source` = the acknowledging node.
- `acked_seq` = the `seq_num` of the DATA/group message being acknowledged.
- `flags` bit0 = `was_group` (so the sender routes the callback to the right pending structure).
- `echo_timestamp` = the timestamp the sender stamped in the DATA message; the sender computes
  `rtt = now - echo_timestamp` on ACK receipt. (Requires carrying a 4-byte send timestamp in reliable
  DATA — add it to the reliable-DATA payload framing, or to DataHeader behind a "reliable" flag; the
  implementer chooses, but it must round-trip.)

`ProcessAckMessage()` handler is registered alongside the existing data/routing handlers (the dispatch
switch reached from `ProcessDataMessage`/the protocol task; cf. `network_service.cpp:610` handler
registration area). The ACK is a plain unicast back to `original_src`, routed by the **normal routing
table** (§5.3 — all nodes routable after convergence); the library auto-generates and auto-matches it
invisibly to the app, with the pending/retransmit/outcome logic in the `ReliableDelivery` component
(§6.0).

---

## 7. F3 — App-visible message IDs

Expose a stable identifier so the app and tests can correlate send → ack → loss → order.

```cpp
struct MessageId {
    AddressType source;   // originator (this node for sends)
    uint8_t     seq;      // sequence within the sender's stream to dest
    AddressType dest;     // unicast destination or group address
    // convenience: uint64_t value() const { return (source << 24) | (dest << 8) | seq; }
};
```
- `SendReliable` / `SendGroup` **return** the assigned `MessageId` (or deliver it via the first
  callback) so the caller can track it.
- The inbound app callback gains an overload (or a new callback) that also reports the sender's
  `MessageId` and hop count, enabling loss/ordering detection at the receiver:
  ```cpp
  using DataReceivedExCallback =
      std::function<void(AddressType source, MessageId id, uint8_t hops,
                         const std::vector<uint8_t>& data)>;
  ```
  Keep the existing `DataReceivedCallback` working for backward compatibility.

---

## 8. Public API additions (the contract PingR codes against)

On `LoraMesher` (`loramesher.hpp`) and its `Builder`:

```cpp
// --- Groups ---
Result JoinGroup(AddressType group);
Result LeaveGroup(AddressType group);
bool   IsMemberOfGroup(AddressType group) const;
std::vector<AddressType> GetGroups() const;

// --- Sending ---
struct ReliableOptions { uint32_t timeout_ms = 4000; uint8_t max_retries = 3; };
struct GroupSendOptions { uint8_t ttl = 0 /*=default*/; bool request_acks = false;
                          uint32_t timeout_ms = 8000; uint8_t max_retries = 0; };

MessageId SendReliable(AddressType dest, std::span<const uint8_t> data, ReliableOptions = {});
MessageId SendGroup(AddressType group, std::span<const uint8_t> data, GroupSendOptions = {});

// --- Delivery outcomes ---
enum class DeliveryStatus { Delivered, Failed, GroupWindowClosed };
struct DeliveryOutcome {
    MessageId      id;
    DeliveryStatus status;
    AddressType    by;        // acking node (Delivered); 0 otherwise
    uint32_t       rtt_ms;    // valid when Delivered
    uint8_t        ack_count; // valid for GroupWindowClosed
};
using DeliveryCallback = std::function<void(const DeliveryOutcome&)>;
void SetDeliveryCallback(DeliveryCallback cb);

// --- Inbound with metadata (additive; existing SetDataCallback still works) ---
void SetDataCallbackEx(DataReceivedExCallback cb);
```

Builder additions: `.withGroups({0x8001, ...})` (optional convenience),
`.withReliableDefaults(ReliableOptions)`.

Config (`LoRaMeshProtocolConfig`): `ack_timeout_ms`, `ack_max_retries`, `group_ack_window_ms`.

---

## 9. Constraints & compatibility

- **TDMA slot timing**: retransmits and auto-ACKs must respect the existing slotting — enqueue ACKs to
  the appropriate slot (`CONTROL_TX` or `TX`) and let `GetTimeUntilNextDataSlot()` gate actual TX. Do
  **not** transmit synchronously from the RX delivery path.
- **MTU**: the added per-message timestamp (for RTT) and any reliable framing must keep
  `data + overhead ≤ config_.max_packet_size` (checked at `network_service.cpp:1787`). Document the
  reduced app MTU for reliable/group-with-timestamp messages.
- **TTL / hops**: group floods and unicast retransmits use the existing `ttl`/`max_hops`. PingR's old
  per-send `hop_count` (e.g. survey used 1 hop) maps to `GroupSendOptions.ttl`; expose a per-send TTL.
- **Backward compatibility**: existing `Send`, `SendBroadcast`, `SetDataCallback`, `GetRoutingTable`,
  roles/capabilities must be unchanged. New types and methods are purely additive. `ACK = 0x21` was
  reserved, so no enum renumbering.
- **De-dup interaction**: ACKs are unicast DATA-class control messages and must not be dropped by the
  data de-dup cache; give ACK its own handling path.

---

## 10. Acceptance criteria — `VirtualNetwork` GoogleTests

Two test tiers are required — the standalone component (§6.0) makes the first one cheap.

**Tier A — `ReliableDelivery` isolation unit tests** (no radio, no mesh, no `VirtualNetwork`, no RTOS).
Drive the component with a fake `Host` (a lambda that records `send_attempt` calls + a manual `now_ms`
clock) and assert the full state machine deterministically in microseconds: attempt #1 on `Track`;
retransmit on timeout with decrementing retries; `OnAck` → `Delivered{by, rtt_ms}`; exhaustion →
`Failed`; `PendingCount() == 0` after terminal outcomes (no leak); `collect_multiple` fires once per
distinct responder without erasing until the host closes the window. Live under
`test/protocols/reliability/`.

**Tier B — end-to-end `VirtualNetwork` integration tests** under
`test/protocols/lora_mesh/services/test_integration/` (new file, e.g. `group_ack_test.cpp`), passing
deterministically under virtual time, using the existing harness API (`CreateNode`,
`GenerateLineTopology`/`GenerateStarTopology`, `SetLinkStatus`, `SetPacketLossRate`,
`SetDirectionalLinkLoss`, `AdvanceTime(..., condition)`, `HasReceivedMessageFrom`,
`GetReceivedMessages`, `GetReceivedMessageCount`):

1. **Group delivery to members only.** Star or line of ≥3 nodes; two join group `0x8001`, one does not.
   Sender `SendGroup(0x8001, payload)`. After `AdvanceTime`, the two members delivered the payload
   (`HasReceivedMessageFrom`), the non-member did **not** deliver it (but the relay still occurred —
   assert via received-count/topology that it forwarded).
2. **Group does not leak across group ids.** Members of `0x8001` do not receive a `SendGroup(0x8002,…)`.
3. **Reliable unicast success → Delivered callback with RTT.** Two connected nodes; `SendReliable`;
   assert the receiver delivered once and the sender's `DeliveryCallback` fired exactly once with
   `Delivered`, `by == receiver`, `rtt_ms > 0`. Assert **no** retransmit occurred
   (`GetReceivedMessageCount(receiver) == 1`).
4. **Reliable unicast retransmits under loss, then succeeds.** Inject directional loss on the first
   attempt(s) via `SetDirectionalLinkLoss`; assert the message is eventually delivered and the callback
   reports `Delivered`, and that more than one TX attempt was made.
5. **Reliable unicast fails after max retries.** Sever the link (`SetLinkStatus(false)` or 100% loss);
   assert the callback fires `Failed` after `max_retries`, and `pending_acks_` is empty afterward
   (no leak).
6. **Group with `request_acks` reports each responder.** ≥3 members; `SendGroup(g,…,{request_acks=true})`;
   assert the sender's `DeliveryCallback` fires once per member with distinct `by`, then a
   `GroupWindowClosed` with `ack_count == member_count`.
7. **Message IDs are stable & observable.** Assert the `MessageId` returned by a send equals the id
   reported to the receiver's `SetDataCallbackEx`, and that sequence numbers increase per source.
8. **No duplicate delivery under flood.** In a mesh with multiple paths, a single `SendGroup` delivers
   to each member exactly once (de-dup holds) — assert `GetReceivedMessages(member).size() == 1`.

Additionally: `cmake --build build && ctest` (or `pio test`) is green, and the new code keeps
`-Wall -Wextra -Wpedantic -Werror` clean (the repo builds with `-Werror`).

---

## 11. Out of scope / future work

- **Routed (non-flood) multicast** and **mesh-wide membership propagation** (via `RoutingTableMessage`
  or a new `GroupMembershipMessage` type `0x48`). v1 uses flood + local membership, which is sufficient
  for PingR (small groups, broadcast-style position sharing) and avoids membership-convergence
  complexity. Capability-propagation code (`network_service.cpp:750/200`) is the template if/when this
  is needed.
- **Guaranteed group delivery** to a known set (requires membership propagation + per-member pending
  tracking).
- **Payload fragmentation** for messages larger than one MTU.
- **Encryption / authentication** of group traffic.
- **Unified `getStats()` observability struct** (report §11.12 #4): a separate, optional library
  improvement — NOT part of this group/ACK contract. The Phase 3 delivery rig needs none of it; it
  uses the F2 `DeliveryCallback` (outcome + RTT), F3 message IDs, and the existing `GetRoutingTable()`/
  `GetNetworkStatus()`/`GetTx|RxQueueSize()`. Listed here only so it isn't conflated with this spec.

---

## 12. Open questions for the implementer

**Resolved (do not re-litigate):** reliability is a standalone injected `ReliableDelivery` component
owned by the LoRaMesh protocol's `NetworkService`, **not** inline mesh code (§6.0). Group is a separate
addressing concern at the single filter point (§5.3). The remaining open choices are tactical:

1. **Timestamp carriage for RTT**: add a 4-byte send-timestamp to reliable DATA via (a) a reliable
   framing prefix in the payload, or (b) a `DataHeader` extension behind a "reliable" flag bit? (a) is
   non-invasive to the header; (b) is cleaner but touches the 10-byte DataHeader layout. Recommend (a)
   for v1.
2. **ACK suppression / coalescing** for large groups to avoid ACK storms — acceptable to defer, but note
   whether a small random ACK jitter is added (PingR already jitters survey pings ±150 ms).
3. **Where the retransmit timer lives** — piggyback on the existing ~100 ms protocol-task tick vs a
   dedicated `RETRANSMIT_CHECK` notification. Either is fine; document the chosen cadence (affects the
   minimum useful `timeout_ms`).

---

### Appendix A — mapping PingR concepts → this spec

| PingR today | This spec |
|-------------|-----------|
| `GroupPacket.group_id` + software filter | group address `0x8000–0xFFFE` + `JoinGroup` membership |
| broadcast + app `group_id` match | `SendGroup(group, …)` flood + membership-gated delivery |
| `GroupAck` unicast reply, matched by `msg_id` | library auto-ACK (type `0x21`), matched by `MessageId{src,seq}` |
| `taskRetryAck` (3× / 30 s) | `ReliableOptions{timeout_ms, max_retries}` + library retransmit |
| "who ACKed" history/pages | per-responder `DeliveryCallback(Delivered{by})` + `GroupWindowClosed` |
| survey ping `hop_count = 1` | `GroupSendOptions.ttl` per-send |
