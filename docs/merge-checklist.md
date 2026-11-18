# Pre-Merge Checklist: `new_loramesher` → `main`

## CI

- [ ] All CI jobs green (no red steps in the latest push to `new_loramesher`)
- [ ] No test failures (unit + integration test suites both pass)

## Coverage

- [ ] Coverage badge updated in the Gist after the latest push
- [ ] Line coverage ≥ current `main` baseline (no regression)
- [ ] No critical paths (routing, superframe, slot allocation) show a drop

## Protocol

- [ ] `PROTOCOL_SPEC.md` updated if any protocol message format or state-machine transition changed
- [ ] Wire format changes are backward-compatible, or the spec documents the new version

## README / API

- [ ] `README.md` API usage examples still compile and reflect current public API
- [ ] Builder-pattern interface unchanged (or documented)

## Memory / ESP32

- [ ] No new heap allocations in hot paths (message handling, slot loop)
- [ ] `BaseMessage`, `RoutingTableMessage`, `SlotTable` still use static storage (no `std::vector` regressions)
- [ ] ESP32 runtime tested or heap-usage analysis reviewed

## Sanitizers

- [ ] ASAN build: no new memory errors or leaks
- [ ] TSAN build: no new data races or lock-order inversions (check `CapabilityPropagationThroughNetwork` is still the only known skip)

## Implementation Status

- [ ] `Implementation_status.md` updated for any newly completed or changed features
- [ ] Any features removed or deprecated are noted

## Dependencies

- [ ] Latest `espressif32` platform and RadioLib releases checked; the "Dependency drift" workflow is green
- [ ] Pins raised to the versions tested on hardware: `platform = espressif32@…` in `examples/*/platformio.ini` and `platformio.ini`, RadioLib in the package manifests
- [ ] Versions named in the `README.md` Quick Start match the pins

## Housekeeping

- [ ] Badge condition in `.github/workflows/test.yml` reverted to `main`-only after merge
- [ ] Temporary docs (analysis files, scratch notes in repo root) cleaned up
- [ ] `CLAUDE.md` reflects the current state of the codebase
