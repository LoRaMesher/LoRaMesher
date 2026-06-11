# Agent brief: Fix SX127x `getTimeOnAir()` LDRO bug in RadioLib and open a PR

Self-contained handoff for an agent (with its own GitHub auth) to implement the
upstream RadioLib fix behind LoRaMesher issue #111 and open the pull request.
Related local docs: [`radiolib_ldro_upstream_fix.md`](./radiolib_ldro_upstream_fix.md),
[`todo_radiolib_ldro_followup.md`](./todo_radiolib_ldro_followup.md).

## Goal

Fix a bug in RadioLib where `SX1278::setSpreadingFactor()` updates the Low Data
Rate Optimization (LDRO) hardware register but **not** the cached `ldroEnabled`
member that `getTimeOnAir()` reads — causing `getTimeOnAir()` to return a value
one LoRa code-block too short at high spreading factors. Then open a pull
request against `jgromes/RadioLib:master`.

## Repository

- Upstream: `https://github.com/jgromes/RadioLib` (branch `master`).
- Fork it under your account, clone, branch (e.g. `fix/sx127x-ldro-toa`),
  commit, push to your fork, open the PR against `jgromes/RadioLib:master`.
- This is a **standalone upstream fix** — do not reference or depend on any
  other repository's code. Motivation can be mentioned in the PR body, but the
  fix stands on its own.

## Root cause (confirmed in 7.6.0, 7.7.1, and current `master`)

In `src/modules/SX127x/SX1278.cpp`:
- `setBandwidth()` updates **both** the LDRO register **and** the member
  `this->ldroEnabled`.
- `setSpreadingFactor()` writes **only** the register, inside its
  `if(this->ldroAuto) { ... }` block — it never assigns `this->ldroEnabled`.
- `getTimeOnAir()` (in `src/modules/SX127x/SX127x.cpp`) derives airtime from the
  member: `pc.lora.ldrOptimize = this->ldroEnabled;`.
- `begin()` calls `setBandwidth(bw)` **before** `setSpreadingFactor(sf)`. During
  `setBandwidth`, the spreading factor is still the default (9), so
  `ldroEnabled` is computed as `false`. `setSpreadingFactor(sf)` then writes the
  register ON for SF11/SF12 but leaves `ldroEnabled = false`.

Net effect: the radio transmits with LDRO correctly enabled, but
`getTimeOnAir()` underestimates airtime by one code block. Example at
SF12 / BW125 kHz / CR 4/7 / explicit header / CRC on, 24-byte payload:
`getTimeOnAir()` returns ~1582 ms; correct airtime is ~1810 ms (a 7-symbol /
~229 ms block). LDRO is auto-enabled when the symbol time `2^SF / BW >= 16 ms`,
i.e. SF12/BW125, SF12/BW250, SF11/BW125.

## The fix

In `SX1278::setSpreadingFactor()`, mirror what `setBandwidth()` already does —
set the member alongside the register write:

```diff
   if(this->ldroAuto) {
     float symbolLength = (float)(uint32_t(1) << SX127x::spreadingFactor) / (float)SX127x::bandwidth;
     Module* mod = this->getMod();
     if(symbolLength >= 16.0f) {
+      this->ldroEnabled = true;
       state = mod->SPIsetRegValue(RADIOLIB_SX1278_REG_MODEM_CONFIG_3, RADIOLIB_SX1278_LOW_DATA_RATE_OPT_ON, 3, 3);
     } else {
+      this->ldroEnabled = false;
       state = mod->SPIsetRegValue(RADIOLIB_SX1278_REG_MODEM_CONFIG_3, RADIOLIB_SX1278_LOW_DATA_RATE_OPT_OFF, 3, 3);
     }
   }
```

## Scope — apply to the whole SX127x family

The `SX1278` class backs SX1276/77/78/79; the `SX1272` class backs SX1272/73.

1. **`src/modules/SX127x/SX1278.cpp`** — `setSpreadingFactor()` (the confirmed
   case). Apply the diff above.
2. **`src/modules/SX127x/SX1272.cpp`** — open `setSpreadingFactor()` and check
   whether its `ldroAuto` block has the same omission. If so, apply the
   analogous fix using the `RADIOLIB_SX1272_*` register macros (the
   `MODEM_CONFIG` register/bit names differ on SX1272 — match what that file's
   `setBandwidth()` already uses).
3. Grep both files to confirm their own `setBandwidth()` does set
   `this->ldroEnabled` (the reference behavior you're matching), and that no
   other method needs the same treatment. The member is declared in
   `src/modules/SX127x/SX127x.h` (`bool ldroEnabled`).
4. Do **not** touch SX126x — there, both `setSpreadingFactor()` and
   `setBandwidth()` route through `setModulationParams()`, which recomputes
   `ldrOptimize` from current SF+BW each call, so it is not affected.

## Constraints

- Keep the change minimal and match RadioLib's existing code style/indentation
  (2-space indent, brace placement as in the surrounding code).
- Do not reorder `begin()` or change public APIs.
- No behavior change to actual transmission (the register was already correct) —
  this only fixes the cached state that `getTimeOnAir()` reads.

## Verification

RadioLib has no host-runnable unit-test harness for this, so verify by
reasoning + a minimal sketch in the PR description (hardware optional):

```cpp
SX1278 radio = new Module(/* pins */);
radio.begin(434.0, 125.0, 12, 7);        // SF12, BW125, CR4-7
Serial.println(radio.getTimeOnAir(24));   // before fix: ~1582000 us; after: ~1810000 us
```

Also confirm the library still compiles (`pio run` against an example, or the
repo's CI). The fix should make `getTimeOnAir()` after `begin()` equal to
calling `setBandwidth(bw)` again afterward (the known-good workaround).

## PR metadata

- **Branch:** `fix/sx127x-ldro-toa`
- **Title:** `fix(SX127x): update ldroEnabled in setSpreadingFactor so getTimeOnAir is correct at high SF`
- **Body (outline):**
  - Symptom: `getTimeOnAir()` underestimates airtime by one code block at
    SF11/SF12 (e.g. SF12/BW125, 24 B -> 1582 ms vs ~1810 ms).
  - Root cause: `setSpreadingFactor()` writes the LDRO register but not the
    cached `ldroEnabled` member; `getTimeOnAir()` reads the member; `begin()`
    runs `setBandwidth()` (with default SF) before `setSpreadingFactor()`,
    leaving the member stale.
  - Fix: set `this->ldroEnabled` in `setSpreadingFactor()` for SX1278 (and
    SX1272), mirroring `setBandwidth()`.
  - Repro + before/after `getTimeOnAir()` values; note the existing workaround
    (re-calling `setBandwidth()` after `begin()`) and that this removes the need
    for it.
  - Note SX126x is unaffected and reference closed issue #1591 as a distinct
    prior LDRO fix on that family.
- Follow the repo's `CONTRIBUTING.md` / PR template if present; target `master`.

## Useful references

- `src/modules/SX127x/SX1278.cpp` — `setSpreadingFactor()`, `setBandwidth()`.
- `src/modules/SX127x/SX1272.cpp` — verify `setSpreadingFactor()`.
- `src/modules/SX127x/SX127x.cpp` — `getTimeOnAir()` / `getNumSymbols()` reading
  `this->ldroEnabled`.
- `src/modules/SX127x/SX127x.h` — declaration of `bool ldroEnabled` and
  `bool ldroAuto`.
