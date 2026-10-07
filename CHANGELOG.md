# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [2.0.0] - Unreleased

The on-air protocol changes again: every node of a network must run 2.x.
Upgrading from `1.x`? See [MIGRATION.md](MIGRATION.md).

### Added
- Reliable (acknowledged) unicast: `SendReliable(dst, data, ReliableOptions)`
  returns a `MessageId`, and `SetDeliveryCallback(...)` reports each message
  as delivered (with round-trip time) or failed. Retransmission timeouts adapt
  to the measured round-trip time per destination.
- Group multicast: `JoinGroup` / `LeaveGroup` / `IsMemberOfGroup` /
  `GetGroups` and `SendGroup(group, data, GroupSendOptions)`. An acknowledged
  group send reports every responder and closes with the responder count;
  `GroupSendOptions::max_retries` re-floods the message inside the ACK window,
  and members still deliver it once.
- `SetDataCallbackEx(const ReceivedData&)` reports each received message
  with its destination, sender sequence number and hops travelled.
- `examples/reliable_example` and `examples/group_example`.
- Routing-table broadcasts are sliced across superframes, so large tables fit
  in high-SF packets.
- `max_data_slots` configuration: the data-slot budget is separate from the
  node-count cap.
- SX1278 and SX1268 radios in the radio factory.
- Deterministic (address-hash) sync-beacon subslot assignment.
- `IRoutingTable::SetMaxHops()`: the routing table accepts routes up to the
  configured `max_hops` (custom routing tables must implement it).
- Network stress test suite (`test_network_stress`).

### Changed
- Wire format: new message types `DATA_RELIABLE`, `DATA_GROUP` and `ACK`
  (with a 6-byte ACK payload: acknowledged sequence, flags, echoed
  timestamp); see `PROTOCOL_SPEC.md` §3.2.6 and §7.2.
- TDMA data slots are assigned by control-slot index, and `ROUTE_TABLE`
  headers carry the sender's control-slot index (6 → 7 bytes).
- `default_data_slots` defaults to 2 (was 1) and must be the same on every
  node; `max_data_slots` defaults to 100.
- `max_packet_size` is capped to the physical limit of the spreading factor,
  and subslots are sized from time-on-air.
- `LoRaMeshProtocol::GetNetworkNodes()` returns a snapshot copy;
  `GetNetworkNodesCopy()` is removed.
- `GetSlotTable()` (`LoraMesher` and `LoRaMeshProtocol`) returns a
  `std::vector<SlotAllocation>` snapshot instead of a `std::span` into the
  live table.
- A node address must be unicast (`0x0001`–`0x7FFF`, or 0 to auto-assign);
  `LoRaMeshProtocolConfig::Validate()` rejects group and broadcast addresses,
  and `SendReliable()` refuses non-unicast destinations.
- Addresses generated from the hardware ID are folded into the unicast range:
  a MAC whose derived address had the top bit set now yields that address
  with the top bit cleared.
- The sync-beacon subslot assignment defaults to `ADDRESS_HASH` (was
  `ADDRESS_MODULO`).
- `SubslotAssignment::HOP_BASED` is removed; `SubslotConfig` defaults to
  `ADDRESS_HASH`.
- `RadioType::kSx1268` is inserted before `kMockRadio`, shifting
  `kMockRadio`'s numeric value.
- `RTOS::CreateTask()` takes the stack size in bytes on every FreeRTOS port.
- The interfaces `IJoinService`, `INetworkDiscoveryService` and
  `ISlotManagementService` are removed (they had no implementation).
- Stopping the protocol abandons pending reliable messages and reports each
  one through the delivery callback (`Failed`, or `GroupWindowClosed` for a
  group send).
- Route-update callbacks run while the routing table is locked and must not
  call back into the routing table or the network service.
- `battery_level` is removed from `JOIN_REQUEST` and `NM_CLAIM` (and from
  `JoinRequestMessage::Create()` / `NMClaimMessage::Create()`).
- Cross-network Network Manager merge is disabled.
- Joining: a node sends its `JOIN_REQUEST` in a random even discovery slot
  (sponsored joins in the first one) and the Network Manager answers in the
  next slot; unanswered requests back off 0–3 superframes, the retry count
  survives a rejoin, and the join timeout is 13 superframes (was 3).

### Fixed
- ESP32 task stacks: sizes were divided by 4 for an API that takes bytes, so
  tasks ran with a quarter of the configured stack and the stack monitor
  reported four times the real free space. The configured sizes now state
  the real stacks (the effective stack of each task is unchanged), and the
  monitor reports bytes.
- Reliable delivery: retransmissions reach distant destinations under load; a
  message is no longer acknowledged but dropped after the sequence number
  wraps; an attempt that can never be queued fails instead of retrying
  forever; ACKs are accepted only from the destination; deadlines survive the
  32-bit tick wrap.
- Thread safety: send APIs are safe to call from the application thread, the
  routing table is only read under its lock, and the slot table, protocol
  state and Network Manager address are synchronized with the protocol task.
- Stop/Start: a network reset no longer deadlocks when a state-change or
  route callback calls back into the library, clears election, sponsor and
  sync-beacon state, and keeps the packet sequence counter so neighbours do
  not drop the restarted node's first packets as duplicates.
- Reliable group sends: a send whose attempts could not be queued no longer
  leaves its acknowledgement window open and permanently uses up a pending
  slot.
- Routing: route flapping on marginal links is damped; link quality holds
  across sliced broadcasts; gateways stay reachable for far nodes at SF12;
  links are not judged unidirectional before the peer could hear this node.
- TDMA slots: corrupted slot values no longer collapse the network; band sizes
  cannot wrap at large network depth; the slot table is rebuilt when a
  control-slot index changes; slot grants keep node capabilities; the
  routing table honours the configured `max_network_nodes`; sync beacons with
  an impossible schedule are discarded.
- Radio: the receiver stays on for the whole listening slot; time-on-air is
  correct at high SF (LDRO); long SF12 frames are no longer rejected by the
  time-on-air sanity check; the SX1268 defaults to 433 MHz (inside its
  410–810 MHz band).
- Routing: routes longer than 10 hops are accepted up to the configured
  `max_hops` (at most 16).
- A Network Manager that surrendered in a merge stays committed to joining the
  winner.
- Network formation: nodes that start joining together no longer collide in
  lockstep, so dense networks form at SF10–SF12.
- Network Manager election: the election backoff expires at its own deadline
  instead of the next superframe start, and waiting nodes listen on every
  slot, so two candidates no longer claim together and both become manager.
  Election timers stay correct across the 32-bit tick wrap.

## [1.0.0] - 2026-05-15

Complete rewrite of LoRaMesher. The public API, configuration model, and wire
protocol have all changed and are not compatible with the `0.0.x` line.

- Upgrading from `0.0.11-alpha`? See [MIGRATION.md](MIGRATION.md).
- Protocol-level details (state machine, message formats, TDMA superframe,
  routing, NM election) live in [PROTOCOL_SPEC.md](PROTOCOL_SPEC.md).

### Added
- Builder-based initialization: `LoraMesher::Builder()...Build()` returning a
  `std::unique_ptr<LoraMesher>` (no more singleton).
- Callback-based receive API: `SetDataCallback(...)` replaces the
  task-handle + queue-poll pattern.
- TDMA-aware send timing helpers: `GetTimeUntilNextDataSlot()`,
  `GetDataSlotsPerSuperframe()`, `IsReadyToSend()` / `IsReadyToSend(dst)`.
- Capability discovery: `SetNodeCapabilities()`, `GetNodeCapabilities()`,
  `GetClosestGateway()`, `GetClosestNodeByCapability()`.
- Routing and network introspection: `GetRoutingTable()`, `GetNetworkStatus()`,
  `GetTxQueueSize()`, `GetRxQueueSize()`.
- Composed configuration objects: `PinConfig`, `RadioConfig`,
  `LoRaMeshProtocolConfig` — sensible defaults, override per-field.
- Documented board presets in `README.md` (TTGO T-Beam, T3 S3, Heltec V1/V3).
- Native desktop test harness via PlatformIO `test_native`, GoogleTest, and
  hardware mocks; ASAN/UBSAN, TSAN, LLVM coverage, and XRay profiling
  environments.
- Runtime role changes via `SetNodeRole(...)` (NODE_ONLY / AUTO /
  NETWORK_MANAGER) with safe transitions.
- `[[nodiscard]] Result` return type for fallible operations.
- Hardware-derived address helper `LoraMesher::GenerateAddressFromHardware()`.

### Changed
- Public header: `#include "LoraMesher.h"` → `#include "loramesher.hpp"`,
  symbols live under `namespace loramesher`.
- Lifecycle: single-phase `Build()` + `Start()` (was two-phase
  `LoraMesherConfig` + `begin()` + `start()`).
- Send API now returns `Result` instead of `void`; failures are no longer
  silent. Method renamed: `createPacketAndSend(...)` → `Send(dst, vector)` and
  new `SendBroadcast(...)`.
- Default `max_packet_size` is auto-derived from the active spreading factor
  and bandwidth; an explicit `setMaxPacketSize()` is preserved with a warning
  if it exceeds the SF-safe cap.
- `LoraModules` enum → `RadioType` enum (`kSx1276`, `kSx1262`, `kSx1278`, …).
- Build system: PlatformIO + CMake desktop targets; Conventional Commits
  enforced in CI; coverage and format checks gate PRs.

### Removed
- Singleton accessor `LoraMesher::getInstance()`.
- Task-handle-based receive: `setReceiveAppDataTaskHandle(...)`,
  `ulTaskNotifyTake`, `getReceivedQueueSize()`, `getNextAppPacket<T>()`.
- Manual packet lifetime management: `deletePacket(packet)` (now automatic).
- `standby()` / resume cycle — use `Stop()` and rebuild a new instance.
- `LoraMesher::LoraMesherConfig` flat configuration struct.
- Templated `AppPacket<T>` user-payload type — payloads are
  `std::vector<uint8_t>`; serialize your own types on top.

### Protocol
The 1.0.0 wire protocol is incompatible with `0.0.x`. Key changes:
distance-vector routing with link-quality metrics, TDMA superframe with
slot allocation, sponsor-based join, distributed NM election with
network-merge handling, and capability advertisement. A whole network
must be upgraded together. See [PROTOCOL_SPEC.md](PROTOCOL_SPEC.md).

## [0.0.11-alpha] - 2025-11-01

Final release of the pre-rewrite line. Tagged as `v0.0.11-legacy` for
reference; no further updates planned. Users on `0.0.x` should follow
[MIGRATION.md](MIGRATION.md) to move to `1.0.0`.

[Unreleased]: https://github.com/LoRaMesher/LoRaMesher/compare/v2.0.0...HEAD
[2.0.0]: https://github.com/LoRaMesher/LoRaMesher/compare/v1.0.0...v2.0.0
[1.0.0]: https://github.com/LoRaMesher/LoRaMesher/releases/tag/v1.0.0
[0.0.11-alpha]: https://github.com/LoRaMesher/LoRaMesher/releases/tag/v0.0.11-legacy
