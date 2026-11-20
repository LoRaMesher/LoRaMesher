# Review of spec version 2

Code read at `loramesher/release/2.0.0` (368c359), read only.

## Status of the 12 findings

| # | Status | Section |
|---|---|---|
| 1 | FIXED, opens N1 | S6.6 step 5 |
| 2 | PARTLY, cap declined, accepted | S5.3, S14.3 |
| 3, 4 | FIXED | S5.1, S7.2, S7.6 |
| 5 | PARTLY, see N2 to N7 | S9 |
| 6, 7, 8, 10, 12 | FIXED | S6.6, S2.3, S9.2, S1.2, S7.4 |
| 9 | FIXED, see N9 | S4.5 |
| 11 | FIXED, see N10 | S7.2, S7.5 |

## New findings

| # | Sev | Section | Problem | Smallest fix |
|---|---|---|---|---|
| N1 | MAJOR | S6.6 step 5 | Relays forward any replayed E = 1 frame. The duplicate cache holds 32 entries and a relay has 2 data slots per 77 s. An outsider who replays two old recordings per superframe fills a relay's slots, and each relay down the path forwards them. | Relays keep a second hop window for E = 1 DATA, DATA_RELIABLE and ACK. Only 0x51, 0x52 and 0x55 skip it, with a forward cap. This adds per-peer state, so decide before step 2c. |
| N2 | MAJOR | S9.5 step 3 | The unsynchronised request lands in the sync slot of the next hop layer. There M sleeps (`slot_scheduler.cpp` phase 1) and M's children send beacons. | See point 3. |
| N3 | MAJOR | S9.5 step 4 | Source addresses are free, so only the limit of one per superframe counts. One 19-byte frame per member per superframe costs each member half its uplink, and the KEY_RELAY frames converge near the gateway. | M forwards one request per 16 superframes. Relays cap forwarded 0x55 frames (the type is visible). |
| N4 | MAJOR | S9.5 step 7, S8.2 | A node with held_epoch 255 accepts any epoch and saves limits 0. If its counter record already names that epoch, it reuses nonces under K_net. | Reset limits only when new_epoch is above the record epoch. Keep them when equal. Refuse when below. |
| N5 | MAJOR | S9.5 | ORPHAN has no exit without a grant. Eight junk frames in a quiet or jammed period orphan a healthy node. A grant for its own epoch is refused. | Leave ORPHAN on the first frame that verifies under a held key. |
| N6 | MAJOR | S9.2 | PENDING nodes switch after half the grace period, with no tie to `rekey_distribution_timeout`. 35 updates through 2 slots per superframe need at least 23 minutes. Early nodes switch and become unreadable to nodes still waiting. | MUST: distribution timeout at most a quarter of the grace period. |
| N7 | MAJOR | S9.2, S9.3 item 4 | A node in GRACE must answer under epoch e, yet its epoch e counter is gone. Using the restarted counter reuses a nonce under K_ke[i,e]. | Nodes always seal under their transmit epoch. |
| N8 | MINOR | S9.5 | Four spoofed requests flush M's table. G refuses an equal `c_rec`, so a replay through a far member steals the grant. Early retries void a grant in flight. | Drop the table. G repeats the grant for the same `c_rec`. O accepts any `c_rec` of the current episode. |
| N9 | MINOR | S4.5 | Only the latest nonce verifies. Overhearers count the final response as an auth failure. | Accept the last two nonces. Skip that count. |
| N10 | MINOR | S7.2 rule 5 | DISCOVERY and JOINING have no SLEEP slot, so the first save has no defined time. | Save at state entry. |

## Point 2: jump cap

The author is right. With a 32-bit MIC the beacon carries the same risk as ROUTE_TABLE, where I asked for no cap. Ship the plain strict rule and leave the candidate form out.

## Point 3: recovery

No key leaks. A revoked node gets no grant. An old grant is refused. `C_rec` reuse is harmless, because the grant plaintext is fixed per `(O, c_rec, epoch)`. The rate limits are enforceable, since M and G count on their own synchronised clocks. They are too loose (N3).

The timing as written cannot work reliably. Members listen in the sync slot of their parent layer, in control slots, in neighbours' data slots and in DISCOVERY_RX slots, which are always the last `2 × (max_hops + 1)` slots of the superframe. Smallest workable alternative: O measures the superframe period from two beacons of M, using only the clear header. It sends inside M's discovery band by counting back from M's next expected beacon. For M at layer h the band covers `h` to `h + 2 × (max_hops + 1)` slots before that beacon. O rotates offsets of 2, 5 and 9 slots across attempts. This needs no K_net and no new frame.

## Verified independently

- V1, V5a, V5b, V5c, V7, K_re and K_rm reproduce byte for byte with my own AES and CMAC (`spec-review/v2.py`).
- Ramp: simulated to n = 5,000, never above `3n + 1`, always persisted before use. 13.6 years and 324 days confirmed.
- C_data (2^30 − 1 values): 1,310 years at 2 frames per superframe, 32 years at one frame per 950 ms slot, 8.7 years at back to back 255 ms frames.
- Sizes: beacon 26, JOIN_REQUEST 26 and 22, JOIN_RESPONSE 25, KEY_REQUEST 19 (226 ms), KEY_GRANT 31 (312 ms), KEY_RELAY 38 and 50 (341 and 427 ms). The 115 limit holds.
- Not verified: the subslot band model.

## Can steps 1 and 2 start?

Yes. Steps 1a, 1b, 2a and 2b can start against version 2 as it stands. Settle N1 before step 2c fixes the snapshot layout and before step 3. S9 needs N2 to N7 before step 5.
