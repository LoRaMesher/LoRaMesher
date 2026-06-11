# TODO: RadioLib LDRO / Time-on-Air follow-up

Tracks the cleanup to do **once the LDRO bug is fixed upstream in RadioLib**.
Background and the upstream patch are in
[`radiolib_ldro_upstream_fix.md`](./radiolib_ldro_upstream_fix.md);
the original report is LoRaMesher issue #111.

## The issue (summary)

RadioLib's `SX1278::setSpreadingFactor()` (and `SX1276`, likely `SX1272`)
writes the Low Data Rate Optimization *register* but never updates the cached
`this->ldroEnabled` member. `getTimeOnAir()` reads that member, so after
`begin()` (which sets bandwidth before the spreading factor) the reported
time-on-air is one code block too short at high SF — e.g. SF12/BW125, 24 B:
1582 ms reported vs ~1810 ms actual. LoRaMesher sizes TDMA slots from that
value, so nodes drift out of sync and drop messages. Present in RadioLib
7.6.0, 7.7.1, and `master`.

## What we shipped as the interim fix

- **Wrapper workaround** in all four radio modules
  (`src/hardware/radiolib/radiolib_modules/sx1278.cpp`, `sx1276.cpp`,
  `sx1262.cpp`, `sx1268.cpp`): re-apply `setBandwidth(config.getBandwidth())`
  right after `radio_module_->begin(...)` so RadioLib recomputes the LDRO/ToA
  state for the configured spreading factor. Version-independent.
- **Own Semtech ToA helper** `src/utils/lora_airtime.hpp`
  (`ShouldEnableLdro`, `CalculateTimeOnAirMs`).
- **Runtime self-check** `RadioLibRadio::CheckTimeOnAirConsistency()` — warns
  over serial if the driver's ToA disagrees with our reference (no oscilloscope
  needed to detect a regression).
- **Removed the redundant `NetworkService::toa_cache_`** (an unordered_map on
  top of the already-cached, SPI-free `RadioLibRadio::toa_cache_ms_`).
- **Native regression test**
  `test/protocols/lora_mesh/services/test_unit_superframe_timing/lora_airtime_test.cpp`.

## Next steps once RadioLib is fixed

1. **Land the upstream fix.** Open the issue + PR on `jgromes/RadioLib` using
   `radiolib_ldro_upstream_fix.md` (add `this->ldroEnabled = …` inside the
   `ldroAuto` block of `SX1278::setSpreadingFactor()` / `SX1276` / `SX1272`).
2. **Bump the pin.** When a RadioLib release contains the fix, update
   `platformio.ini` (`jgromes/RadioLib#7.6.0`) and `library.json` to that
   version.
3. **Remove the wrapper workaround.** Delete the `setBandwidth()` re-apply block
   from the four `radiolib_modules/*.cpp` `Begin()` methods. Confirm
   `getTimeOnAir(24)` at SF12/BW125 still reports ~1810 ms with the workaround
   gone (i.e. RadioLib now does it correctly on its own).
4. **Keep the self-check** (`CheckTimeOnAirConsistency`) — it is cheap and
   guards against future RadioLib regressions or version drift. Only remove it
   if it ever produces false positives against a correct driver.

## Optional larger consolidation (independent of the RadioLib fix)

Tracked by the `TODO` in `RadioLibRadio::CheckTimeOnAirConsistency()`:
make `airtime::CalculateTimeOnAirMs()` the **single source of truth** and retire
the driver's `getTimeOnAir()` path entirely, so `toa_cache_ms_` is populated
from our own calculation. This removes the dependence on RadioLib's buggy
cached state altogether — making step 3 above unnecessary — and leaves exactly
one ToA computation in the codebase.

## Verification checklist

- `pio test -e test_native --filter "protocols/lora_mesh/services/test_unit_superframe_timing"`
- `pio run -e esp32` (compile check)
- On hardware: print `radio.getTimeOnAir(24)` at SF12/BW125 → ~1810 ms; the
  self-check logs no warning; two-node slot offset stays within the guard time
  (verify via the timestamped serial logs / `log_analyzer.html`).
