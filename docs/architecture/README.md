# LoRaMesher Architecture Documentation

A validated architectural review of the LoRaMesher library and the staged plan to make it
cleaner, lighter, and free of duplication. Every quantitative claim was checked against the
source (`wc`/`grep`/`diff`).

## Read in order

| Doc | What it covers |
|---|---|
| [00-overview.md](00-overview.md) | Layered architecture, dependency rules, behavior-preservation invariant, the closure-injected component pattern |
| [01-module-responsibilities.md](01-module-responsibilities.md) | Per-folder / per-file responsibility contract: should-own vs. actually-contains vs. deviation |
| [02-network-service-decomposition.md](02-network-service-decomposition.md) | The 9-responsibility god class, the component split, and state-ownership rules |
| [03-message-serialization.md](03-message-serialization.md) | Message-type boilerplate catalogue and the CRTP/template consolidation |
| [04-hardware-radio-modules.md](04-hardware-radio-modules.md) | The verified duplicate radio drivers and the template-base design |
| [05-memory-model.md](05-memory-model.md) | End-to-end per-message RAM trace and the copy-chain elimination |
| [06-refactor-roadmap.md](06-refactor-roadmap.md) | The sequenced, test-first execution plan with per-phase gates |
| [07-decisions-and-extraction-spec.md](07-decisions-and-extraction-spec.md) | Log of every non-trivial decision made + the vetted, ready-to-execute spec for the remaining extractions |

## TL;DR

- **Biggest structural problem:** `network_service.cpp` (4,713 L) fuses nine responsibilities
  → decompose into a ~700 L coordinator + 5 closure-injected components (`02`, WS-5).
- **Biggest easy win:** the four `radiolib_modules/sx12xx` drivers are duplicate copies
  (within-family `diff` = 0) → one template base (`04`, WS-1).
- **Cleanliness:** 14 message types repeat the same serialize/deserialize skeleton
  (~2,177 L) → shared CRTP base (`03`, WS-3).
- **Lighter RAM:** the TX/forward path triple-copies every message → in-place build +
  span-backed payloads (`05`, WS-4).
- **Correctness of structure:** two upward `types/ → protocols/` includes → move the shared
  enum/config down into `types/` (`01`, WS-2).

## Invariant

The protocol is timing-sensitive. Every change is **behavior-preserving** and gated by the
existing test suite; the public API and `GetNetworkServiceForTest()` stay stable until the
explicit interface-segregation phase (WS-6).
