# Upstream RadioLib fix: SX127x `setSpreadingFactor()` leaves `ldroEnabled` stale

This documents the root-cause RadioLib bug behind LoRaMesher issue #111 and the
patch to submit upstream. LoRaMesher already carries a self-contained workaround
(re-applying `setBandwidth()` after `begin()` in each radio wrapper), so this
upstream fix is the proper root fix, not a blocker.

## The bug

In `src/modules/SX127x/SX1278.cpp` (also `SX1276`, and likely `SX1272`):

- `setBandwidth()` updates **both** the LDRO hardware register **and** the
  `this->ldroEnabled` member.
- `setSpreadingFactor()` updates **only** the register — it never assigns
  `this->ldroEnabled`.
- `getTimeOnAir()` derives airtime from the **member**
  (`pc.lora.ldrOptimize = this->ldroEnabled`).

`begin()` runs `setBandwidth(bw)` first (while the spreading factor is still the
default 9, so `ldroEnabled` is computed as `false`), then `setSpreadingFactor(sf)`
writes the register ON for SF11/SF12 at BW125 but leaves `ldroEnabled = false`.
Result: the radio transmits with LDRO correctly enabled, but `getTimeOnAir()`
under-reports by one code block (e.g. ~229 ms for a 24-byte payload at
SF12/BW125/CR4-7: 1582 ms reported vs 1810 ms actual).

This is present in 7.6.0, 7.7.1, and `master`.

## Reproduction

```cpp
SX1278 radio = new Module(...);
radio.begin(434.0, 125.0, 12, 7);           // SF12, BW125, CR4-7
Serial.println(radio.getTimeOnAir(24));      // prints ~1582000 us (wrong)
radio.setBandwidth(125.0);                    // recomputes ldroEnabled for SF12
Serial.println(radio.getTimeOnAir(24));      // prints ~1810000 us (correct)
```

## Patch

In `SX1278::setSpreadingFactor()`, mirror what `setBandwidth()` already does —
assign the member alongside the register write:

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

Apply the same change to `SX1276::setSpreadingFactor()` and check
`SX1272::setSpreadingFactor()`.

## Notes for the PR

- SX126x (SX1262/SX1268) routes both `setSpreadingFactor()` and `setBandwidth()`
  through `setModulationParams()`, which recomputes `ldrOptimize` from the
  current SF and BW each call, so the final call in `begin()` leaves it
  consistent — that family is not affected by this specific bug.
- Closed issue jgromes/RadioLib#1591 is a different, SX126x-only LDRO problem.
- No existing open issue covers this SX127x member-vs-register inconsistency.
