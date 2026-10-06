# Review follow-ups (2.0.0 branch)

Findings from the review of the 2.0.0 work that were not fixed with it. The
high-severity findings (thread safety of the slot table and protocol state,
reset behaviour, sequence reuse, group-window leak, address validation, SX1268
default, hop limit, election tick wrap) are fixed. Items are ordered by
priority within each section.

## Correctness

- **Runtime reconfiguration races the protocol task.** `ProtocolManager` can
  call `LoRaMeshProtocol::Configure()` while the protocol runs; it rewrites
  `config_` in `LoRaMeshProtocol` and most `NetworkService` fields without
  synchronization. Either reject `Configure()` while running or make it
  apply on the protocol task.
- **Route-update callbacks run under the routing-table lock.**
  `DistanceVectorRoutingTable::Clear()` / `UpdateRoute()` call
  `NotifyRouteUpdate()` with `table_mutex_` held, so a user callback that
  queries the routing table self-deadlocks. Collect notifications and fire
  them after unlocking (documented as a 2.0.0 restriction for now).
- **Delivery-window capacity vs. sender streams.** Senders keep up to 254
  per-destination streams; receivers keep 32 (`DeliveryWindows::kCapacity`,
  least-recently-used). An evicted stream that receives a retransmission is
  treated as new and delivered twice. Size the receiver for the expected
  peer count or document the limit.
- **Late flooded copies reset a delivery window.** A copy arriving 32 or more
  sequences behind restarts the stream (`delivery_windows.cpp`), which moves
  `highest` backwards. Drop far-behind copies unless the timestamp also
  shows a sender restart.
- **`ReliableMessaging` keeps a copy of the node address.**
  `Host::node_address` is captured at construction; `NetworkService::Configure()`
  can later change the address. Read it through a closure.
- **Wrap-unsafe timers outside the election.** `PerformDiscovery`,
  `PerformJoining` and the sync-beacon rate limiter compare tick counts with
  `<` and use 0 as "unset". Use `utils::TimeReached()` and `std::optional`.
- **`ReliableDelivery::OnAck()` reports any echoed timestamp.** A garbled or
  future `echo_ts` reports an RTT near 2^32 ms to the application. Clamp it,
  as `RecordRttSample()` already does.
- **`HOP_BASED` subslot assignment passes the node address** instead of the
  hop count (`LoRaMeshProtocol::ComputeSubslotIdentifier`).
- **Callback setters are unsynchronized.** `SetRouteUpdateCallback`,
  `SetDataReceivedCallback(Ex)`, `SetStateChangeCallback` and
  `SetLogRoutingCapabilities` assign while the protocol task may read them.
- **Pre-send callbacks capture raw `this`** in messages held by the shared
  message queue (`SyncBeaconService`, `NetworkService`); safe only while the
  queue is not drained after teardown.

## Embedded resources

- **Protocol task stack.** The protocol task has 4 KB. Reliable and group
  traffic add an `OutcomeBatch` (~380 B) and message construction frames on
  the send path. Measure the high-water mark on ESP32 with reliable/group
  traffic before the release.
- **`ReliableMessaging` footprint (~8.5 KB).** Every pending entry reserves a
  254-address responder array even for unicast. Keep responders only for the
  group windows.

## Decomposition (refactor roadmap)

- Remaining NetworkService extraction: `JoinService`, `NmElectionService`, and
  the sync-beacon receive path (`ProcessSyncBeacon`). See
  `docs/architecture/06-refactor-roadmap.md`.
- Dead code still in `NetworkService`: `SlotTableToSuperframe`,
  `UpdateNetworkTopology`, `NotifySuperframeOfNetworkChanges`,
  `ResetLinkQualityStats`, `CalculateLinkStability`, `UpdateNetwork`, the
  commented-out `BroadcastSlotAllocation` / `ProcessSlotAllocation` bodies,
  and the declared-but-undefined `GetNodeLinkQuality`.
- Reliable-receive handling is split: `NetworkService::ProcessDataMessage`
  parses the reliable prefix and calls `EnqueueAck`/`AcceptReliable`, the
  same sequence `ReliableMessaging::ProcessGroupMessage` runs. Move it into
  `ReliableMessaging`.
- The TTL rule `min(2 * max_hops, 255)` is repeated six times and
  `kDefaultTTL` is defined twice; `HopsFromTtl` depends on the copies staying
  identical. Use one helper.
- `SyncBeaconService` restores the TX slot through a callback into
  `NetworkService`; call `RestoreSyncBeaconTxSlot()` from the coordinator's
  `pre_start_action` instead.
- `LoRaMeshProtocol` holds a concrete `NetworkService` and calls methods that
  are not on `INetworkService`; the interface still requires no-op methods.
- Header hygiene in `network_service.hpp`: unused includes
  (`slot_allocation_message.hpp`, `ack_payload.hpp`, `group_message.hpp`,
  `<array>`), namespace-scope `static const` constants that should be
  `inline constexpr`.

## Group delivery at scale

- Reliable GROUP ACK completeness is ~28% in the 25-node stress cell
  (TODO-016); no test guards it.

## Test infrastructure

- CTest reports failing tests as passing: every test `main` returns 0
  regardless of `RUN_ALL_TESTS()`. PlatformIO is unaffected (it parses the
  output).
- CTest gives every `protocols/` test 60 s; multi-hop formation tests take
  longer. Give `test_routing*` and `test_tdma` their own limit.
- The RTOS mock's real-time limits (`kReblockTimeoutMs`, `DeleteTask`'s
  2 s wait, the 20 ms poll in virtual-time waits) can make loaded or
  sanitizer runs flaky; make them configurable.
- `test_native_tsan` is not run in CI; the concurrency tests are only
  meaningful under it.
- Two near-identical fixture names: `JoinContentionTests`
  (`test_routing`) and `JoinContentionTest` (`test_unit_network_coverage`).

## Packaging

- `library.properties` declares `includes=LoRaMesher.h`, which does not exist
  (the header is `loramesher.hpp`); the Arduino IDE support work should fix it.
- `library.json` requires RadioLib `^7.1.2` while `platformio.ini` pins 7.6.0,
  and the LDRO workaround is written against 7.6.0+.
- High-SF payload and contention redesign: see `sf12-high-sf-followup.md`.
