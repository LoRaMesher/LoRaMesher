# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/LoRaMesher/LoRaMesher/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/LoRaMesher/LoRaMesher/releases/tag/v1.0.0
[0.0.11-alpha]: https://github.com/LoRaMesher/LoRaMesher/releases/tag/v0.0.11-legacy
