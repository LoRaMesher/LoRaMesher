# Hardware Radio Modules — verified duplication & template design

> The four `radiolib_modules/sx12xx.{hpp,cpp}` drivers are the highest-confidence,
> lowest-risk consolidation target in the codebase. Drives WS-1 (done first).

## The finding (verified by `diff`)

Normalizing line endings (one file is CRLF, the rest LF) and the chip-name token
(`1262`/`1268`/`126x`, `1276`/`1278`/`127x`):

| Comparison | Differing lines | Meaning |
|---|---|---|
| `sx1262.cpp` vs `sx1268.cpp` | **0** | byte-for-byte identical except the chip name |
| `sx1276.cpp` vs `sx1278.cpp` | **0** | byte-for-byte identical except the chip name |
| `sx1262.cpp` vs `sx1276.cpp` (cross-family) | **36 / ~334** | ~89% identical; a handful of real API differences |

So `radiolib_modules/` (~1,350 LOC across 8 files) is effectively **two** implementations
(SX126x and SX127x), each duplicated once, with the two families sharing ~90% of their code.

## What actually differs between families (the 36 lines)

All within `Begin` / config helpers — genuine RadioLib API differences:

- **Modem init:** SX126x calls `begin(..., tcxoVoltage, useRegulatorLDO=false)`; SX127x calls
  the shorter `begin(...)` without TCXO/LDO args.
- **CRC:** SX126x uses `uint8_t` mode constants (`RADIOLIB_SX126X_LORA_CRC_ON/OFF`); SX127x
  passes a `bool`.
- **Current limit:** SX126x passes the value directly; SX127x needs a `static_cast<uint8_t>`.
- A few comment/typo differences (noise).

Everything else — construction, `Send`, `StartReceive`, the ~12 "not supported" stub methods
that throw `std::runtime_error`, RSSI/SNR/state accessors — is identical.

## Target design: one template base + family customization points

```
RadioLibModuleBase<RadioLibType>          // ~300 L: all shared logic + the 12 stubs
        ▲                    ▲
SX126xFamily<T>          SX127xFamily<T>   // ~20 L each: ConfigureModem / SetCrc / SetCurrentLimit
        ▲   ▲                ▲   ▲
   SX1262  SX1268        SX1276  SX1278    // ~10 L each: chip name + RadioLibType alias
```

- `RadioLibModuleBase<RadioLibType>` (CRTP or plain template) holds every shared body and
  calls protected customization hooks for the three family-specific operations.
- Two family layers implement those three hooks once each.
- Four leaf types supply only `using RadioLibType = …;` and the chip-name string.

`~1,350 L → ~350 L`. The four leaf drivers become trivial.

## Why this is safe to do first

- **Isolated:** nothing in the mesh/protocol layer depends on driver *internals*, only on the
  `IRadio` interface they implement.
- **Mechanically verifiable:** the post-refactor `.cpp` for each chip must produce the same
  behavior as today's identical copies; a diff of generated behavior is trivial to reason
  about.
- **Native tests don't exercise these** (they use `MockRadio`), so the real gate is the
  **ESP32 compile** (`pio run -e esp32`), which must pass for all four chips.

## Flash-size note

Templates instantiate per chip, so the *generated* code is ~the same size as today (we are
removing source duplication, not necessarily flash). In practice only one chip is compiled
into a given firmware, so per-build flash is unchanged or smaller. Verify with a
before/after `pio run -e esp32` size report.

## Execution checklist (WS-1)

1. Normalize all four `.cpp`/`.hpp` to LF.
2. Add `radiolib_module_base.hpp` (template base + the 12 shared stubs + shared `Begin`/`Send`/
   `StartReceive`) with three protected hooks.
3. Add `sx126x_family.hpp` / `sx127x_family.hpp` implementing the hooks.
4. Reduce `sx1262/68/76/78.{hpp,cpp}` to leaf specializations.
5. Gate: native build (`pio test -e test_native --without-testing`) + `pio run -e esp32` for
   each supported board; confirm firmware size is not worse.

## Validation
- `diff` results reproduced via:
  `sed 's/126[28]//g;s/126x//g;s/127[68]//g;s/127x//g'` + `tr -d '\r'` on each pair, then
  `diff | grep -c '^[<>]'` → 0 (within family), 36 (cross family).
