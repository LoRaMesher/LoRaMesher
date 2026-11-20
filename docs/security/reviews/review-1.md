# Review of the LoRaMesher wire security spec

Spec: `security-spec.md`. Code read at `loramesher/release/2.0.0` (368c359), read only.

## Findings

| # | Sev | Section | What is wrong | Attack or failure | Smallest fix |
|---|---|---|---|---|---|
| 1 | BLOCKER | S6.6 step 5, S7.4, S9 | The hop window is keyed on the origin counter and advanced with K_net alone. KEY_UPDATE passes through it. | A stolen node sends one DATA_BROADCAST with source = gateway and ctr = 0x7FFFFFFE. Its hmic and tag are valid under K_net keys. Every node sets `data_high[gateway]` to the maximum. Every later gateway frame is dropped as a replay, KEY_UPDATE included. One frame blocks revocation. The state survives deep sleep (S7.7). | Exempt types 0x51 and 0x52 from the hop window and let the destination use only the end-to-end window. Better: exempt all E = 1 unicast. State in S9.3 that an active insider can also blackhole by forged routing. |
| 2 | MAJOR | S5.3, S7.4 rule 2 and 6 | The 24-bit beacon MIC has no rate limit, no jump limit and no alarm. S5.3 understates the effect. | Continuous 255 ms attempts give 338,800 tries per day, so 49.5 days expected and 34 days for a 50 % chance. Any source and any counter can be claimed. The codec stores the counter before the parser runs, so one hit with ctr = 0xFFFFFE locks out that source's beacons for every node in range until reboot or rekey. The lock survives DISCOVERY and sleep. A fresh counter gives random plaintext, so a chosen shift needs a cold victim and a recorded nonce. | Cap the forward jump (accept only `c − last ≤ 4096`). Raise an event on an auth-failure rate. Consider finding 3. |
| 3 | MAJOR | S5.1, S14.1 #6 | The 3 + 3 split is self-imposed. `propagation_delay_ms` is a uint32, and 24 bits always suffice (255 slots × 65,535 ms < 2^24). | The weakest MIC in the design guards the frame that sets network time. | Send the delay in 3 bytes in secured mode. This gives a 3-byte counter and a 4-byte MIC at 26 bytes. |
| 4 | MAJOR | S7.2 rule 6, S7.3, S5.3 | Contradiction. S7.2 claims 262,144 reboots, but the gen ranges of S7.3 give C_bcn only 2^21 values, so 32,768 reboots. The "16 years" becomes 5.1 years at 77 s. | A brownout loop on a weak solar node burns 64 beacon counters and one NVS write per boot. At one reset per superframe the beacon space is gone in 29 days, at one per 5 s in 45 hours. The node then stops beaconing until a visit or a rekey. | Use `B_bcn = 8` for the boot reservation, or drop the gen split for C_bcn. Correct both claims. |
| 5 | MAJOR | S9.2, S9.3 | A node that misses KEY_UPDATE is lost after the 1 h grace. A STABLE(e) relay also strands the PENDING nodes behind it. The text does not say whether the gateway may keep sending KEY_UPDATE under epoch e after its own switch. Type 0x51 is visible in the clear header and the frame is 48 bytes, so it can be jammed selectively. | Sleeping nodes need a physical visit after every rekey. | State that the gateway retries under epoch e for the whole grace period. Make the grace period cover the longest sleep. Owner question 1. |
| 6 | MAJOR | S11.7 | A flood relay encrypts the plain body again with the origin's nonce. | Any change to the body between Open and Seal (a bug, or the reliable group prefix path) reuses a CTR nonce on different plaintext. | The relay MUST forward the received ciphertext bytes unchanged. |
| 7 | MINOR | S2.3, S9.3.5 | After two rekeys the parity matches the stale key. | The node counts `rx_auth_failed` and never `rx_unknown_epoch`, so the stated diagnosis is false. No key rollback is possible. | Document it. Raise an event on sustained auth failures from routed peers. |
| 8 | MINOR | S9.2, S11.1 | After a reboot in GRACE the provider still lists e as current while the counter record says e + 1. The fate of the grace timer is undefined. | Two implementers will differ. | Make the record epoch authoritative and restart the grace timer. |
| 9 | MINOR | S7.5, S14.1 #17 | No join nonce. | A replayed beacon plus a replayed JOIN_RESPONSE gives a cold node a stale slot. It then transmits out of phase. | Put a 4-byte nonce in JOIN_REQUEST (26 bytes, same 255 ms). The sponsor adds it to the MIC input of the final response without sending it. Zero bytes on the response. |
| 10 | MINOR | S1.2 | Jam and delayed replay of a genuine beacon shifts timing with no forgery. The existing "too soon" filter at `network_service.cpp` near line 3150 then drops the genuine one. | Subtree shift by an outsider with two radios. | List it as an accepted limit, or bound the shift a synced node accepts. |
| 11 | MINOR | S7.2, S7.5 | The write estimate is low. `SaveRxFloor` with a stride of 64 on the gateway counter fires on almost every received gateway frame. | Wear is harmless (below). Flash writes may stall the radio ISR. | Do saves in SLEEP slots. Measure in step 6. |
| 12 | MINOR | S4.4, S7.4, S7.2 rule 5 | "6 join fields", "9 join fields" and "7 header fields" are byte counts. A bitmap shift of 32 or more is undefined in C. Rule 5 drops the gen ceiling. | Divergent code. | Reword. Define the shift. Keep the gen ceiling. |

Choices 2, 4 and 8 held up. Resealing control frames is nonce safe. The header mask always has its 16-byte sample, has no short-frame case, and gives no oracle beyond a 2^-32 forgery. Overheard frames touch no state. Choice 9: a stolen node cannot derive K_net[e + 1] and cannot forge KEY_UPDATE, since that needs K_node of the target. Its only lever is finding 1 and routing lies.

## Verdict on choice 1

Keep CTR plus CMAC. The composition is sound. Keys are separate per purpose. Every MAC input starts with a distinct byte (0x01, 0x02, 0x03 to 0x05, 0x06, 0x08). The nonce and the header are inside every MAC. Keystream cannot repeat across layers, domains, directions or epochs as long as addresses are unique. I found no gap that CCM would close.

Byte cost: CCM with a 4-byte tag costs the same 8, 10 and 12 bytes on classes C1, C2 and D. On the beacon CCM needs a 4-byte tag, so 27 bytes, which is the broken band unless finding 3 is adopted. CCM also cannot serve as the hop MIC, because the origin nonce repeats across hops and CCM leaks under that. CMAC is needed anyway, so CCM would add code and save nothing.

Conditions: implement finding 6, use constant-time compares, and keep the RFC 4493 and SP 800-38A vectors in step 1a.

## NVS wear (choice 7)

A node at a 77 s superframe has 1,122 superframes per day. That gives 17.5 beacon writes, 4 to 9 control writes and about 4 data writes, so 26 to 31 writes per day. A gateway that ACKs 35 nodes adds about 153, so near 180 per day. Assuming four NVS entries per 36-byte record and a five-page partition, this is a few hundred erase cycles per page per year against 100,000. Wear is no concern. Exhaustion by reboot is the real risk (finding 4).

## Verified independently

- All six vectors (V1 to V6, both hops of V3) and the S12.1 keys reproduce byte for byte. I used a pure Python AES-128 and my own CMAC, written from the spec text and checked against FIPS 197 and RFC 4493. Script: `spec-review/v.py`.
- Airtimes: 26 bytes is 255 ms, 27 is 284, 101 is 742, 115 is 828.
- Frame layouts match the code: BaseHeader 6 bytes, DataHeader next_hop, ttl, seq_num, group flags at byte 3, beacon body 14 bytes, join and routing fields, the forwarders that pass `{}`, and the cited line ranges I opened.
- Byte counts in S10.1 and S10.2 are consistent. Type values fit the K bit. 2.0.0 rejects secured types.

## Not verified

The subslot band model of S10.6. The beacon and control frame rates per superframe (assumed one beacon and one to two control frames). The fraction of time a synced node listens. NVS page geometry and write latency on the board. RadioLib AES behaviour.

## Questions for the owner

1. Is a site visit acceptable for every node that misses a rekey, for example after sleeping more than one hour? If no, a recovery path under K_node must be designed before the format freezes.
2. Must revocation work against a stolen node that is actively used, or only against a lost device? The second needs only finding 1. The first needs a per-transmitter hop counter (18 bytes).
3. May the secured beacon body change (3-byte delay) to buy a 32-bit MIC, and may JOIN_REQUEST grow to 26 bytes for a nonce?

## Decisions on the owner questions (provisional, by the coordinator)

1. No site visits. A node that missed a rekey, for example after a long sleep, must recover over the air through a path protected by its own node key. Design it before the format freezes.
2. Revocation targets a lost or removed device in this version, so finding 1 is fixed with the exemption and data frames stay at 12 bytes. The limit against an actively used stolen node is stated in the spec, and the format must leave room to add a per-transmitter hop counter later.
3. Yes to both: the secured beacon carries the delay in 3 bytes to get a 3-byte counter and a 4-byte MIC at 26 bytes, and JOIN_REQUEST grows to 26 bytes for a 4-byte join nonce.
