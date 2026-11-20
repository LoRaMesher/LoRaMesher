# LoRaMesher Wire Security Specification

Version 3, 2026-11-20. Draft for implementation. Target branch `release/2.0.0` (checked at 368c359).
This text is written to become Section 12 of `PROTOCOL_SPEC.md`. Section numbers below are local (S1 to S14).

All multi-byte fields are little-endian unless a field says big-endian. Byte offsets start at 0. `‖` is concatenation. `X[a..b]` means bytes a to b inclusive. Hex strings list bytes in wire order.

Words used with a fixed meaning: **origin** is the node named in the BaseHeader source of a data frame. **Transmitter** is the node whose radio sends the frame on this hop. **Gateway** is the node that holds the per-node keys of the other nodes. **Orphan** is a node that holds its node key but no usable network key. **MUST**, **MUST NOT** and **MAY** are normative.

## Implementation status of the sections

| Sections | Status | Steps that may start |
|---|---|---|
| S1 to S7, S8.1, S10, S11, S12 (V1 to V5) | **Stable.** Two review rounds, all vectors reproduced independently. | 1a, 1b, 2a, 2b, 2c, 3 |
| S9.1 to S9.4 (rekey), V6 | **Provisional.** Formats are fixed. Timers and the gateway procedure may still change. | none yet (step 5a) |
| S9.5 (recovery), S8.2, V7 | **Provisional.** Formats are fixed. The request timing is derived from code and has not been simulated. | none yet (step 5b) |

Step 2 implements only what S9.4 lists from S9. The snapshot layout of S7.7 is final for step 2c.

## Change log, version 2 to version 3

Source: second review `reviews/review-2.md` (N1 to N10, no blocker).

| Finding | Change | Why |
|---|---|---|
| N1 | Relays keep a second hop window for E = 1 DATA, DATA_RELIABLE and ACK. Destinations do not use it. The key types skip every hop window and get a forwarding cap of 2 per superframe (S6.6, S7.4). Per-peer state grows by 8 bytes in RAM and 4 in the snapshot (S7.7). | An outsider could fill a relay's data slots with replayed recordings. The key types stay outside so the blocker of the first review does not return. |
| N2 | The orphan measures the superframe period from beacon frames of a member and sends inside that member's discovery band, counting back from the next expected beacon with offsets 2, 5, 9, 14 slots. The slots a member listens in are listed with file and line (S9.5). | The unsynchronised request landed in a slot where the member sleeps. |
| N3 | A member relays one request per 16 superframes. Relays cap forwarded key frames (S9.5, S6.6). | One request per superframe cost each member half its uplink. |
| N4 | A grant resets the counters only when its epoch is above the counter record epoch. Equal keeps them. Below is refused (S9.5 step 7). | A node without a network key could restart counters in an epoch it had already used: nonce reuse. |
| N5 | A node leaves ORPHAN on the first frame that verifies under a held key. A grant for its own epoch is accepted (S9.5). | Junk frames in a quiet period orphaned a healthy node for good. |
| N6 | `rekey_distribution_timeout` MUST be at most a quarter of the grace period. Defaults are now 1 h and 4 h (S9.2, S9.3). | Early nodes switched while others still waited for their update. |
| N7 | Every node, the gateway included, seals only under its transmit epoch. The gateway's retries under the old epoch are removed. Late nodes use recovery (S9.2, S9.3). | Answering under the old epoch after the counters restarted reused a nonce. |
| N8 | The member keeps no table. The gateway repeats a grant for an equal `c_rec`. The orphan accepts a grant for any of the last 8 `c_rec` values of its episode (S9.5). | Spoofed requests flushed the table, a replay stole the grant, a retry voided a grant in flight. |
| N9 | The joiner accepts its last two join nonces. A JOIN_RESPONSE for another node that fails its MIC is counted apart (S4.5). | A retry voided a response in flight, and overhearers raised false alarms. |
| N10 | The first reservation of a space is saved at the state entry that triggers it (S7.2 rule 5). | DISCOVERY and JOINING have no SLEEP slot. |
| Point 2 | The candidate form of the jump cap is removed. The strict rule ships as is (S14.3). | The reviewer agreed. |

## Change log, version 1 to version 2

Source: adversarial review `reviews/review-1.md` (12 findings) and three provisional owner decisions.

| Finding | Change | Why |
|---|---|---|
| 1 (blocker) | Unicast frames with E = 1 skip the hop replay window. Only the destination checks them, in the end-to-end window (S6.6, S7.4). | One forged broadcast with a maximal counter blocked every later gateway frame, KEY_UPDATE included. |
| 2 | Beacon MIC is 4 bytes (finding 3). New event `kAuthFailureBurst`. The effect of a forged counter is stated in full (S5.3). The jump cap is declined, see S14.3. | The 24-bit MIC had no alarm and its effect was understated. |
| 3 | The secured beacon sends the delay in 3 bytes. Trailer is 3-byte counter plus 4-byte MIC, still 26 bytes (S5.1). | The weakest MIC guarded network time. |
| 4 | Reservation blocks start at 1 and double. A boot that sends nothing costs nothing. The gen ranges are removed. Boot cost and worst-case lifetimes are tabled (S7.2, S7.6). | A brownout loop burned 64 beacon counters per boot, and two claims contradicted each other. |
| 5 | Over-the-air recovery under the node key (S9.5). The gateway retries KEY_UPDATE under the old epoch for the whole grace period. PENDING nodes switch on a timer as well (S9.2). | A node that missed a rekey needed a site visit. |
| 6 | A relay forwards the received ciphertext bytes. It never encrypts a body again (S6.6, S11.7). | Encrypting again with the origin nonce could reuse a nonce on changed plaintext. |
| 7 | The parity alias after two rekeys is documented. Sustained failures raise an event and lead to orphan detection (S2.3, S9.5). | The stated diagnosis was false. |
| 8 | The counter record epoch is authoritative at boot. The grace timer restarts (S9.2). | Behaviour after a reboot in GRACE was undefined. |
| 9 | JOIN_REQUEST carries a 4-byte join nonce (26 bytes). The sponsor binds it into the MIC of the final JOIN_RESPONSE, which stays 25 bytes (S4.5). | A replayed JOIN_RESPONSE gave a cold node stale slots. |
| 10 | Jam plus delayed replay of a genuine beacon is listed as an accepted limit (S1.2). | It was missing. |
| 11 | Storage saves run outside active slots. The receive floor is saved at most every 10 minutes per peer (S7.2, S7.5). | The write estimate was low and a flash write could stall the radio path. |
| 12 | Byte counts are named as bytes, the bitmap shift is defined for 32 and above, the ceiling survives an epoch switch (S4.4, S7.2, S7.4). | Divergent implementations. |
| Decision 2 | Bit 30 of the data counter field is reserved as the H flag for a later per-transmitter hop counter (S6.8). C_data is 30 bits wide. The limit against an actively used stolen node is stated (S1.2). | Room for the 18-byte variant without a format break. |

---

## S1. Scope, goals and limits

### S1.1 What is protected

| Property | Against a party without keys | Against a node that holds K_net (stolen or compromised member) |
|---|---|---|
| Application payload of unicast DATA, DATA_RELIABLE, ACK between a node and the gateway | secret, authentic, replay protected | secret, authentic, replay protected (per-node key) |
| Application payload of DATA_BROADCAST, DATA_GROUP and node to node unicast | secret, authentic, replay protected | readable and forgeable (group key by nature) |
| Control frames (beacon, routing table, join, claim, slot) | secret, authentic, replay protected | readable and forgeable until revocation |
| DataHeader fields next_hop, ttl, seq_num, group flags | hidden, authentic | readable and forgeable |
| Network key delivery (KEY_UPDATE, recovery grant) | secret, authentic | secret, authentic (per-node key) |
| BaseHeader (destination, source, type, payload_size) | visible, authentic | visible, forgeable |
| Frame length, timing, RSSI | visible | visible |

### S1.2 Accepted limits

1. The 6-byte BaseHeader is visible (owner decision 2). A listener learns who talks to whom and the frame type, including the key management types.
2. **Revocation targets a lost or removed device.** A stolen node that is actively used keeps every hop-layer power until its neighbours leave the old epoch. In particular it can:
   - forge beacons, routing tables and join frames, and so blackhole any traffic, KEY_UPDATE included,
   - advance the hop replay windows of any origin and so block that origin's broadcast, group, node to node and relayed end-to-end data frames until the next epoch (key management frames are outside these windows),
   - drop or delay what it relays.
   It cannot read or forge end-to-end traffic of other nodes, cannot move an end-to-end replay window, cannot obtain the next network key and cannot forge a KEY_UPDATE or a recovery grant. A node cut off by such an attacker recovers through S9.5 once it has a neighbour that is honest. Full protection needs the per-transmitter hop counter of S6.8.
3. A relay does not replay-check the key management types (0x51, 0x52, 0x55), so that a K_net holder cannot block them. An outsider can make relays forward a recorded key frame again. The forwarding cap of S6.6 bounds this to 2 frames per relay and superframe, and the destination drops them.
4. A receiver that boots cold has no replay history (S7.5).
5. Jam and delayed replay of a genuine beacon: an attacker with two radios jams a beacon at the victim, records it elsewhere and replays it later. The frame is genuine and its counter is new for the victim, so the victim shifts its timing by the delay. The existing "too soon" filter near `network_service.cpp:3150` may then drop the next genuine beacon. No cryptographic layer without a shared clock prevents this.
6. Selective jamming by frame type, and dropping by a relay, are out of scope.

### S1.3 Primitives

Only two primitives are used: the AES-128 block encryption function `AES(K, B)` (FIPS 197) and AES-CMAC (RFC 4493, NIST SP 800-38B) built on it. Encryption is AES in counter mode. Authentication is AES-CMAC truncated to its leftmost bytes. Every protected unit is encrypt-then-MAC with two separate keys. Every MIC and tag comparison MUST run in constant time.

AES-CCM is not used. CCM cannot serve as the hop MIC, because the origin nonce repeats across hops, so CMAC is needed in any case. CMAC is missing from the prebuilt Arduino 2.0.14 mbedTLS (`# CONFIG_MBEDTLS_CMAC_C is not set`). With this choice the library needs one platform function, the AES block, which exists on every core. CMAC and CTR are implemented once inside the library on top of that block function. The review confirmed this composition.

| Platform | AES block backend | Checked |
|---|---|---|
| ESP32, any Arduino or ESP-IDF core | `mbedtls_aes_crypt_ecb` (hardware AES, `CONFIG_MBEDTLS_HARDWARE_AES=y`) | sdkconfig of Arduino 2.0.14 and of the 3.3.x libs package |
| Native tests, other MCUs | `RadioLibAES128::encryptECB` from RadioLib `src/utils/Cryptography.h`, one private instance | header read in RadioLib 7.1.0, see S14 |
| Server and tools | Python `cryptography` (AES, CMAC, HMAC-SHA256) | used for the vectors in S12 |

---

## S2. Keys

### S2.1 Hierarchy

| Key | Size | Held by | Derivation |
|---|---|---|---|
| K_dep | 32 | server only | random |
| K_mesh | 32 | server, gateway | `HMAC-SHA256(K_dep, "LMM1")` |
| K_node[i] | 16 | node i, gateway, server | `HMAC-SHA256(K_mesh, "LMN1" ‖ addr_i(2, LE) ‖ kver(1))[0..15]` |
| K_net[e] | 16 | every member | `HMAC-SHA256(K_dep, "LMK1" ‖ e(1))[0..15]` |

`kver` is the node key version, 0 unless the server rotates the key of one node. `e` is the key epoch, an unsigned 8-bit number that starts at 0 and only grows. Epoch 254 is the last one. The value 255 means "no network key" in recovery messages. The labels are 4 ASCII bytes without terminator.

The four derivations above run on the server, the flashing tool and the gateway application. The library never sees K_dep or K_mesh. It receives K_net[e] and K_node through the key provider (S11.1).

### S2.2 Traffic keys (derived inside the library)

`KDF(K, label, x) = AES-CMAC(K, label(4 ASCII bytes) ‖ x(1))`, full 16-byte output.

| Key | Parent | Label, x | Use |
|---|---|---|---|
| K_he[e] | K_net[e] | `LMHE`, e | CTR encryption of control frame payloads |
| K_hm[e] | K_net[e] | `LMHM`, e | MIC of control frames, hop MIC of data frames |
| K_hp[e] | K_net[e] | `LMHP`, e | header mask of data frames |
| K_ge[e] | K_net[e] | `LMGE`, e | CTR encryption of data bodies with E = 0 |
| K_gm[e] | K_net[e] | `LMGM`, e | tag of data bodies with E = 0 |
| K_ee[i,e] | K_node[i] | `LMEE`, e | CTR encryption of data bodies with E = 1 |
| K_em[i,e] | K_node[i] | `LMEM`, e | tag of data bodies with E = 1 |
| K_ke[i,e] | K_node[i] | `LMKE`, e | CTR encryption of KEY_UPDATE, KEY_UPDATE_ACK and KEY_RELAY bodies |
| K_km[i,e] | K_node[i] | `LMKM`, e | tag and key check value of the same |
| K_re[i] | K_node[i] | `LMRE`, 0x00 | CTR encryption of the recovery grant (S9.5) |
| K_rm[i] | K_node[i] | `LMRM`, 0x00 | tag of recovery request and grant (S9.5) |

Every traffic key except K_re and K_rm is bound to the epoch. This lets the three traffic counters restart when the epoch changes (S7.2). The two recovery keys are epoch independent, because an orphan does not know the current epoch. They have their own counter, which never restarts.

A node holds at most two epochs at a time, so at most 20 derived keys (320 bytes). Derived keys are computed when a key is installed and are kept in RAM only.

### S2.3 Epoch on the wire and key selection

One bit of the type byte carries the epoch parity (S3.1). A receiver holds a current epoch `e_cur` and at most one other epoch (`e_cur + 1` before its own switch, `e_cur - 1` during the grace period). The two always differ in parity. The receiver selects the held epoch whose parity equals the frame bit. If no held epoch has that parity the frame is dropped and `rx_unknown_epoch` is incremented. No trial decryption with a second key ever happens.

Parity alias: a node that missed two rekeys holds epoch `e` while the network uses `e + 2`. The parity matches, the MIC fails, and the node counts `rx_auth_failed`. `rx_unknown_epoch` alone is therefore no complete diagnosis. Both counters feed the orphan detection of S9.5, which handles any distance between the epochs. A key of a lower epoch is never installed over a higher one.

Grace period: after a node switches its own transmit epoch to `e + 1` it keeps accepting frames under `e` for `epoch_grace_ms` (default 14,400,000 ms, configurable). Then it erases every key of epoch `e` and tells the provider. The grace period SHOULD be at least twice the longest sleep interval of any node. A node that sleeps longer recovers through S9.5.

---

## S3. Frame envelope

### S3.1 Type byte of a secured frame

| Bit | Name | Meaning |
|---|---|---|
| 7 | S | 1 = secured frame |
| 6..4 | category | as today (1 to 5) |
| 3 | K | parity of the key epoch (`e & 1`) |
| 2..0 | subtype | as today (1 to 7) |

`wire_type = 0x80 | ((e & 1) << 3) | plain_type` and `plain_type = wire_type & 0x77`.

Consequences that implementers MUST enforce:
- Subtypes are limited to 1..7 in every category. `message_type::IsValidType` rejects a subtype above 7. All 2.0.0 types already satisfy this.
- A 2.0.0 node without security rejects every secured frame in `BaseHeader::Deserialize`, because the category nibble is 9 or above.
- A node in secured mode drops every frame with S = 0 and increments `rx_plain_dropped`. There is no mixed mode.

New category and types:

| Value | Name | Class | Purpose |
|---|---|---|---|
| 0x50 | KEY_MSG | | key management category |
| 0x51 | KEY_UPDATE | D | next network key for one node (S9.1) |
| 0x52 | KEY_UPDATE_ACK | D | answer to KEY_UPDATE (S9.1) |
| 0x53 | KEY_REQUEST | R | orphan asks a neighbour for the current network key (S9.5) |
| 0x54 | KEY_GRANT | R | neighbour hands the gateway's answer to the orphan (S9.5) |
| 0x55 | KEY_RELAY | D | carries a request or a grant between the neighbour and the gateway (S9.5) |
| 0x56, 0x57 | reserved | | |

### S3.2 Frame layout

```
| BaseHeader H (6) | payload P (payload_size bytes) | trailer T (len by type) |
```

- `payload_size` on the wire is the number of payload bytes on the wire, trailer excluded. It equals the 2.0.0 value for every type except the sync beacon, whose wire payload is one byte shorter (S5.1).
- The on-air length MUST equal `6 + payload_size + TrailerLen(plain_type)`. A receiver drops any other length (`rx_malformed`).
- `H` is sent in clear and is authenticated in every frame.

### S3.3 Frame classes

| Class | Plain types | Trailer | Bytes |
|---|---|---|---|
| B (beacon) | SYNC_BEACON 0x46 | `ctr(3) ‖ mic(4)`, payload shortened by 1 | 7 − 1 = 6 net |
| C1 (control, source is the transmitter) | ROUTE_TABLE 0x32, NM_CLAIM 0x47, SLOT_REQUEST 0x44, SLOT_ALLOCATION 0x45, HELLO 0x31, SYNC 0x41, PING 0x23, PONG 0x24 | `ctr(4) ‖ mic(4)` | 8 |
| C2 (control, source preserved across hops) | JOIN_REQUEST 0x42, JOIN_RESPONSE 0x43 | `tx(2) ‖ ctr(4) ‖ mic(4)` | 10 |
| D (data format) | DATA 0x11, DATA_BROADCAST 0x12, DATA_RELIABLE 0x13, DATA_GROUP 0x14, ACK 0x21, KEY_UPDATE 0x51, KEY_UPDATE_ACK 0x52, KEY_RELAY 0x55 | `ctr(4) ‖ tag(4) ‖ hmic(4)` | 12 |
| R (recovery link frame) | KEY_REQUEST 0x53, KEY_GRANT 0x54 | none, the payload protects itself (S9.5) | 0 |

Any other plain type is dropped in secured mode.

### S3.4 The nonce block

Every counter-mode encryption and every nonce-bound MAC uses one 16-byte block `A`:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | domain `D` |
| 1 | 2 | sender address, LE |
| 3 | 4 | counter value, LE (24-bit counters are zero-extended, the E and H flags are never included) |
| 7 | 1 | zero, except domain 0x0A (granted epoch) |
| 8 | 6 | zero |
| 14 | 2 | block index `j`, big-endian. `A` itself has j = 0 |

| D | Used for | Address in A | Counter |
|---|---|---|---|
| 0x01 | class C1 and C2 | transmitter | C_ctl of the transmitter |
| 0x02 | class B | transmitter | C_bcn of the transmitter |
| 0x03 | class D body, E = 0 | origin | C_data of the origin |
| 0x04 | class D body, E = 1 | origin | C_data of the origin |
| 0x05 | KEY_UPDATE, KEY_UPDATE_ACK, KEY_RELAY body | origin | C_data of the origin |
| 0x06 | prefix byte of the hop MIC input (S6.4), no nonce block | | |
| 0x08 | prefix byte of the key check value (S9.1), no nonce block | | |
| 0x09 | recovery request tag | orphan | C_rec of the orphan |
| 0x0A | recovery grant | orphan | C_rec echoed from the request |

`CTR(K, A, M)`: for j = 1, 2, ... the keystream block is `S_j = AES(K, A[0..13] ‖ j(2, BE))`. The output is `M` XOR the first `len(M)` keystream bytes. Block index 0 is never used as keystream. Decryption is the same operation.

`MIC_t(K, M)` is the first `t` bytes of `AES-CMAC(K, M)`.

Nonce uniqueness rests on two rules. First, a node never uses the same counter value twice in one counter space under one key (S7). Second, node addresses are unique within a deployment. The flashing tool MUST enforce the second rule, since two nodes with one address would reuse nonces under the shared keys.

---

## S4. Control frames, classes C1 and C2

### S4.1 Sealing (transmitter)

Inputs: plain frame `H ‖ P`, transmitter address `tx`, epoch `e`, next counter `c` from C_ctl.

1. Set the type byte in `H` to `wire_type` (S3.1).
2. `A = 0x01 ‖ tx ‖ c ‖ 0^7 ‖ 0^2`.
3. `C = CTR(K_he[e], A, P)`.
4. `mic = MIC_4(K_hm[e], A ‖ H ‖ C ‖ B)`. `B` is empty except for the final JOIN_RESPONSE, where it is the 4-byte join nonce (S4.5).
5. Class C1 trailer: `c(4) ‖ mic(4)`. Class C2 trailer: `tx(2) ‖ c(4) ‖ mic(4)`.
6. Send `H ‖ C ‖ trailer`.

In class C1 `tx` is the BaseHeader source and is not repeated in the trailer.

### S4.2 Opening (receiver)

1. Check the length rule of S3.2 and select the epoch (S2.3).
2. Read `tx` (BaseHeader source for C1, trailer for C2) and `c`. Rebuild `A`.
3. Compute the MIC and compare. On mismatch drop, `rx_auth_failed`.
4. Replay check on `(tx, C_ctl)` (S7.4). On failure drop, `rx_replay`.
5. `P = CTR(K_he[e], A, C)`. Restore the plain type and hand `H ‖ P` to the parser.

A frame from `tx` equal to the node's own address is dropped before step 3.

### S4.3 Forwarding

A relay never forwards control ciphertext. It opens the frame, lets the protocol build the frame it wants to send (as 2.0.0 does today) and seals the result as a new frame under its own address and its own next C_ctl value. The construction is safe for any field the relay changes, since no nonce is ever reused with different content.

Fields a relay changes today, read from the code:

| Frame | Changed by the forwarder | Preserved | Code |
|---|---|---|---|
| JOIN_REQUEST | `next_hop`, `hop_count + 1`, additional info removed | destination, source (joiner), requested_slots, sponsor_address | `network_service.cpp:3661-3671` |
| JOIN_RESPONSE, relay toward the sponsor | `next_hop` | destination (sponsor), source (manager), network_id, allocated_slots, status, target_address, control_slot_index | `network_service.cpp:3829-3836` |
| JOIN_RESPONSE, sponsor to joiner | destination = joiner, `next_hop` = joiner, `target_address` = 0 | source (manager), network_id, allocated_slots, status, control_slot_index | `network_service.cpp:3748-3757` |

Both forwarders pass `{}` as additional info today. This behaviour is kept: the join nonce is needed only by the first receiver of the request (S4.5), so a forwarded JOIN_REQUEST is 22 bytes.

### S4.4 Per-type table

| Type | Clear and authenticated | Encrypted and authenticated | Mutable at a relay | Keys | Nonce | Trailer |
|---|---|---|---|---|---|---|
| ROUTE_TABLE | H | the 7 bytes of routing header fields and all entries | never relayed | K_he, K_hm | D 0x01, source, C_ctl | 8 |
| NM_CLAIM | H | priority, node count, network_id (4 bytes) | never relayed | same | same | 8 |
| SLOT_REQUEST | H | requested_slots (1 byte) | never relayed | same | same | 8 |
| SLOT_ALLOCATION | H | network_id, allocated_slots, total_nodes (4 bytes) | never relayed | same | same | 8 |
| HELLO, SYNC, PING, PONG | H | whole payload | never relayed (the mesh protocol does not send them today) | same | same | 8 |
| JOIN_REQUEST | H | the 6 bytes of join fields, and the 4-byte join nonce on the first hop | resealed per hop (S4.3) | same | D 0x01, `tx` from trailer, C_ctl of `tx` | 10 |
| JOIN_RESPONSE | H | the 9 bytes of join fields | resealed per hop (S4.3) | same | same | 10 |

### S4.5 Join nonce

1. A joiner draws a fresh random 4-byte `join_nonce` for every JOIN_REQUEST it sends, retries included. The request payload is the 6 bytes of join fields followed by the nonce (payload_size 10, 26 bytes on air).
2. A member that receives a JOIN_REQUEST directly from the joiner (trailer `tx` equals the BaseHeader source) stores `(joiner address, join_nonce)`. The table has 4 entries. An entry is replaced by a newer request of the same joiner and expires after the joining timeout.
3. When that member seals a JOIN_RESPONSE whose BaseHeader destination is a joiner in its table, it appends the stored nonce to the MIC input (`B` in S4.1 step 4). The nonce is not sent. Without a table entry the member seals with `B` empty.
4. A node in JOINING verifies every JOIN_RESPONSE addressed to itself with `B` = the nonce of its latest request and, if that fails, with the nonce of the request before it. It keeps exactly these two. Any other receiver verifies with `B` empty.
5. A JOIN_RESPONSE whose BaseHeader destination is another node and whose MIC fails is dropped and counted in `rx_join_unverified`. It does not count toward `rx_auth_failed` or `kAuthFailureBurst`, because an overhearer cannot know the nonce of a final response.

A recorded JOIN_RESPONSE therefore fails at the joiner, because its MIC was bound to an older nonce. A response sealed without the nonce fails as well, and the joiner retries.

---

## S5. Sync beacon, class B

### S5.1 Format

In memory the beacon keeps its 14-byte 2.0.0 payload. On the wire the most significant byte of `propagation_delay_ms` (plain payload offset 11) is omitted.

| Offset | Size | Field | Protection |
|---|---|---|---|
| 0 | 6 | BaseHeader: destination 0xFFFF, source = transmitter, type 0xC6 or 0xCE, payload_size 13 | clear, authenticated |
| 6 | 2 | network_id | encrypted, authenticated |
| 8 | 1 | total_slots | same |
| 9 | 2 | slot_duration_ms | same |
| 11 | 2 | network_manager | same |
| 13 | 1 | hop_count | same |
| 14 | 3 | propagation_delay_ms, low 24 bits, LE | same |
| 17 | 1 | max_hops | same |
| 18 | 1 | node_count | same |
| 19 | 3 | `ctr`: C_bcn of the transmitter, LE | clear, authenticated |
| 22 | 4 | `mic` | |

Total 26 bytes.

Sealing:
1. Take the 14-byte plain payload after the pre-send patch. Byte 11 MUST be zero. If it is not, sealing fails with `kInvalidParameter`. The delay is the time since superframe start, which is below `255 × 65,535 ms = 16,711,425 < 2^24`.
2. `P13` = plain payload without byte 11. Set payload_size to 13 and the type to `wire_type`.
3. `A = 0x02 ‖ source ‖ ctr(3) ‖ 0x00 ‖ 0^7 ‖ 0^2`, `C = CTR(K_he[e], A, P13)`, `mic = MIC_4(K_hm[e], A ‖ H ‖ C)`.

Opening follows S4.2 with the replay check on `(source, C_bcn)`. After decryption the codec inserts a zero byte at offset 11 and sets payload_size to 14, so the parser sees the 2.0.0 layout.

### S5.2 Relay and timing rules

1. A forwarded beacon is a new frame. `CreateForwardedBeacon` already sets the BaseHeader source to the forwarder (`sync_beacon_header.cpp:81-84`). The forwarder seals with its own address and its own next C_bcn value. The fields it changes (source, hop_count, propagation_delay_ms) need no special handling.
2. Order at the transmitter: `CanFitInSlot`, guard delay, pre-send callback that patches `propagation_delay_ms` in the plain 14-byte payload, then seal, then radio. Sealing happens inside `HardwareManager::SendMessage`, which runs after the patch (`sync_beacon_service.cpp:24-45`, `hardware_manager.cpp:113-132`). The callback and its offset constant stay unchanged. The counter value is taken at seal time.
3. The receiver subtracts the airtime of the **on-air length** (26 bytes, 255 ms at SF9 BW125 CR4/7) where `network_service.cpp:1550` uses `sync_beacon.GetTotalSize()` today. This fix ships in the same pull request as the trailer.
4. The receive timestamp is captured right after `readData` returns and before the codec opens the frame. The radio event carries that captured value (`radio_event.hpp:54` takes the tick in its constructor today, so the constructor gains a timestamp argument).
5. Sealing a beacon costs 4 AES blocks (1 keystream, 3 CMAC). The constant `kSerializationOverheadMs = 1` is kept. Step 6 measures the real cost.

### S5.3 Strength and remaining risk

- MIC: 32 bits, the same as every other frame. One forgery attempt occupies at least 255 ms of airtime, so the expected effort is `2^32 × 0.255 s`, about 35 years of continuous transmission.
- Effect of one forgery: the attacker may claim any source and any counter. The counter is stored before the parser runs. A forged counter of 0xFFFFFE locks out the beacons of that source at every node in range until the next epoch or a reboot of the victim. The lock survives DISCOVERY and deep sleep. With a fresh counter the decrypted fields are random, so a chosen timing shift needs a recorded nonce and a cold victim.
- Alarm: the event `kAuthFailureBurst` fires when 32 or more frames failed a MIC or tag within 10 minutes (S11.6).
- Counter: 16,777,215 beacons per node and epoch. A node sends one beacon per superframe. That is 16 years at a 30 s superframe and 41 years at 77 s. Exhaustion and reboot loops are handled in S7.6.

---

## S6. Data frames, class D

### S6.1 Format

| Part | Size | Content | On air |
|---|---|---|---|
| H | 6 | BaseHeader, source = origin, destination = final destination or group or 0xFFFF | clear |
| X | 4 (5 for DATA_GROUP) | DataHeader extension: next_hop(2), ttl(1), [flags(1) for DATA_GROUP], seq_num(1) | masked (S6.5) |
| body | payload_size − len(X) | reliable prefix and application data, or ACK payload, or key message body | encrypted (S6.3) |
| ctr | 4 | bit 31 = E flag, bit 30 = H flag (MUST be 0, S6.8), bits 29..0 = C_data of the origin | clear |
| tag | 4 | body tag (S6.3) | clear |
| hmic | 4 | hop MIC (S6.4) | clear |

Overhead: 12 bytes for every class D frame.

`X_im` is `X` with bytes 0, 1 and 2 (next_hop and ttl) set to zero. It is the part of `X` that no relay changes.

### S6.2 The E flag and the choice of body keys

| Case | E | Body keys | Domain |
|---|---|---|---|
| Unicast DATA, DATA_RELIABLE or ACK where the key provider returns a node key for the peer (S11.1) | 1 | K_ee, K_em of the node end of the pair | 0x04 |
| KEY_UPDATE, KEY_UPDATE_ACK, KEY_RELAY | 1 | K_ke, K_km of the node end of the pair | 0x05 |
| DATA_BROADCAST, DATA_GROUP | 0 | K_ge, K_gm | 0x03 |
| Unicast DATA, DATA_RELIABLE or ACK without a node key for the peer (node to node) | 0 | K_ge, K_gm | 0x03 |

"Node end of the pair": on a node the key is its own K_node. On the gateway the key is K_node of the peer.

Downgrade rule: a destination that holds a node key for the origin MUST drop a unicast class D frame from that origin with E = 0 (`rx_e2e_required`). A frame with E = 1 on a flood type, or with E = 1 where the destination has no node key for the origin, is dropped (`rx_key_unavailable`). The three key types are valid only with E = 1.

Every transmission attempt of a reliable message is a new frame with a new C_data value, because its `send_timestamp` and `seq_num` differ.

### S6.3 Body layer (origin and destination)

Origin, with next C_data value `c`:

1. `A_p = D ‖ origin ‖ c ‖ 0^7 ‖ 0^2` with D from S6.2.
2. `C = CTR(K_enc, A_p, body)`.
3. `tag = MIC_4(K_mac, A_p ‖ H ‖ X_im ‖ C)`.
4. `ctr = c | (E << 31)`.

Associated data of the body layer: `H` (destination, source, wire type with the epoch bit, payload_size) and `X_im` (seq_num, and the group flags). The E flag is bound through D and through the key. The fields next_hop and ttl are not covered by this layer.

Destination: recompute `tag` over the received `H`, `X_im` and `C`, compare, run the replay check of S7.4, then decrypt.

### S6.4 Hop layer (every transmitter, every receiver)

`hmic = MIC_4(K_hm[e], 0x06 ‖ H ‖ X ‖ C ‖ ctr ‖ tag)`, where `X` is the unmasked extension as this transmitter sends it.

Associated data of the hop layer: every byte of the frame before `hmic`. The hop layer has no counter of its own in this version.

`e` is the epoch named by the K bit of the frame. A relay never changes the type byte. It verifies and recomputes `hmic` under the same epoch, even if its own transmit epoch is already newer.

### S6.5 Header mask

After `hmic` is known:

1. `sample = ctr(4) ‖ tag(4) ‖ hmic(4) ‖ source(2) ‖ destination(2)` (16 bytes, addresses as on the wire).
2. `mask = AES(K_hp[e], sample)`.
3. `X_wire[k] = X[k] XOR mask[k]` for k = 0 .. len(X) − 1.

A receiver computes the mask from the received trailer and BaseHeader, unmasks `X`, then verifies `hmic`. The sample differs between two hops of the same frame whenever `X` differs, because `hmic` then differs (collision probability 2^-32). Two transmissions with identical `X` produce identical frames, which reveals nothing new.

### S6.6 Receiver and relay procedure

For every received class D frame, in this order:

1. Length rule (S3.2), `payload_size ≥ len(X)`, H flag is 0, epoch selection (S2.3).
2. Unmask `X` (S6.5). Verify `hmic` (S6.4). On mismatch drop, `rx_auth_failed`.
3. If the origin is this node, drop.
4. If `next_hop` is neither this node nor 0xFFFF, the frame is overheard: drop it here. No replay state is touched. This rule is required: the same origin counter will arrive again from the proper relay.
5. **Hop replay check.** Three cases, by frame:

   | Frame | Node is the destination | Node is a relay |
   |---|---|---|
   | E = 0, any type | hop window G of the origin | hop window G of the origin |
   | E = 1, DATA, DATA_RELIABLE, ACK | no hop window (step 6 uses the end-to-end window) | hop window R of the origin |
   | KEY_UPDATE, KEY_UPDATE_ACK, KEY_RELAY | no hop window | no hop window, forwarding cap below |

   On failure drop, `rx_replay`. A flood copy that arrives a second time ends here.
6. If this node is the destination, or the type is DATA_BROADCAST or DATA_GROUP: verify `tag` on the received ciphertext and decrypt **a copy** of the body (S6.3). For E = 1 the destination then runs the end-to-end replay check. A failure drops the frame (`rx_tag_failed` or `rx_replay`).
7. Deliver the decrypted copy. Forward the received message, whose body is still the received ciphertext.

Why three cases. Both hop windows are driven by K_net alone, so a K_net holder can push them. At a destination, E = 1 frames are therefore judged only by the end-to-end window, which needs the node key to move. At a relay, window R stops an outsider from having old E = 1 recordings forwarded again and again (N1). The key types use no hop window anywhere, so a forged counter can never block a KEY_UPDATE or a recovery (finding 1 of the first review stays fixed).

**Forwarding cap for key types.** A relay forwards at most 2 frames of types 0x51, 0x52 and 0x55 per superframe, all origins together. Further ones wait in a queue of 8. When the queue is full the oldest is dropped (`rx_key_forward_dropped`). Frames a node originates itself are outside this cap.

Relays keep the existing duplicate cache on `(source, seq_num)` for all frames.

What a relay does when it forwards:

| Frame | Verifies | Copies byte for byte | Rewrites | Recomputes |
|---|---|---|---|---|
| Unicast, E = 1, DATA, DATA_RELIABLE, ACK | hmic, hop window R | H, seq_num, C, ctr, tag | next_hop, ttl − 1 | hmic, mask |
| Unicast, key types | hmic, forwarding cap | H, seq_num, C, ctr, tag | next_hop, ttl − 1 | hmic, mask |
| Unicast, E = 0 | hmic, hop window G | H, seq_num, C, ctr, tag | next_hop, ttl − 1 | hmic, mask |
| Flood (DATA_BROADCAST, DATA_GROUP) | hmic, hop window G, tag | H, seq_num, flags, C, ctr, tag | ttl − 1 | hmic, mask |

A relay MUST forward the ciphertext `C`, `ctr` and `tag` exactly as received. It MUST NOT encrypt a body under an origin's counter, for any frame type. Only the origin ever runs the body layer in the encrypt direction. A relay cannot read or alter the body of an E = 1 frame, and it does not spend a counter of its own on forwarding.

### S6.7 Per-type table

| Type | Clear, authenticated | Masked, hop authenticated | Encrypted, tag authenticated | Body keys | Trailer |
|---|---|---|---|---|---|
| DATA | H, ctr | next_hop, ttl, seq_num | application data | S6.2 | 12 |
| DATA_RELIABLE | H, ctr | next_hop, ttl, seq_num | msg_seq, send_timestamp, application data | S6.2 | 12 |
| ACK | H, ctr | next_hop, ttl, seq_num | acked_seq, flags, echo_timestamp | S6.2 | 12 |
| DATA_BROADCAST | H, ctr | next_hop, ttl, seq_num | application data | K_ge, K_gm | 12 |
| DATA_GROUP | H, ctr | next_hop, ttl, flags, seq_num | reliable prefix if requested, application data | K_ge, K_gm | 12 |
| KEY_UPDATE, KEY_UPDATE_ACK, KEY_RELAY | H, ctr | next_hop, ttl, seq_num | S9 bodies | K_ke, K_km | 12 |

seq_num and the group flags are also covered by the tag through `X_im`.

### S6.8 Reserved: per-transmitter hop counter (H flag)

Not implemented in this version. Reserved so that a later version can add it without a format break.

- Bit 30 of `ctr` is the H flag. This version sends 0 and drops a frame with H = 1 (`rx_malformed`).
- With H = 1 the trailer is `ctr(4) ‖ tag(4) ‖ tx(2) ‖ hctr(4) ‖ hmic(4)`, 18 bytes. `tx` is the transmitter of this hop and `hctr` its own strictly rising hop counter (a fourth traffic space). The hop MIC input gains `tx ‖ hctr`. The sample of S6.5 becomes `hctr(4) ‖ tag(4) ‖ hmic(4) ‖ tx(2) ‖ source(2)`.
- A receiver tells the two variants apart by the on-air length (`6 + payload_size + 12` or `+ 18`) and then checks that the H flag agrees.
- The body layer, the nonce block and every other field stay as they are. The maximum payload drops by 6 bytes.
- With it, the hop window per origin disappears and a K_net holder can only replay its own frames.

---

## S7. Counters and replay

### S7.1 Transmit counter spaces

| Space | Width on air | Used by | Maximum usable value | Restarts at epoch switch |
|---|---|---|---|---|
| C_bcn | 24 bits | every beacon the node sends or forwards | 0xFFFFFE | yes |
| C_ctl | 32 bits | every class C1 and C2 frame the node transmits, including join frames it forwards | 0xFFFFFFFE | yes |
| C_data | 30 bits | every class D frame the node originates | 0x3FFFFFFE | yes |
| C_rec | 32 bits | every KEY_REQUEST the node sends (S9.5) | 0xFFFFFFFE | never |

The all-ones value of each space is never sent. Receivers use it as "nothing seen yet".

### S7.2 Persistence across reboot (reserve-ahead)

The application supplies a counter store (S11.3). It holds one record:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `"LMC1"` |
| 4 | 1 | format version (1) |
| 5 | 1 | transmit epoch |
| 6 | 2 | zero |
| 8 | 4 | C_bcn limit |
| 12 | 4 | C_bcn ceiling |
| 16 | 4 | C_ctl limit |
| 20 | 4 | C_ctl ceiling |
| 24 | 4 | C_data limit |
| 28 | 4 | C_data ceiling |
| 32 | 4 | C_rec limit |
| 36 | 4 | CRC-32 of bytes 0..35 (same CRC as the warm restart snapshot) |

40 bytes. `limit` is the first value that is not yet allowed. `ceiling` is the first value the node may never use (default: the all-ones value of the space). The ceiling of C_rec is fixed at 0xFFFFFFFF.

Rules:

1. **Boot.** Load the record. Each counter starts at its `limit`. Boot itself writes nothing.
2. **First need.** A space gets its first reservation when the node first has a reason to transmit in it:

   | Space | Trigger |
   |---|---|
   | C_ctl | entering JOINING, or creating a network |
   | C_bcn | entering NORMAL_OPERATION or NETWORK_MANAGER |
   | C_data | the first send call or the first ACK or key message to originate |
   | C_rec | each KEY_REQUEST (block size always 1) |

3. **Block ramp.** The k-th reservation of a space since boot has size `b_k = min(2^(k−1), B)`, so 1, 2, 4 and so on up to `B_bcn = 64`, `B_ctl = 256`, `B_data = 256`. A reservation saves `limit = min(next + b_k, ceiling)`.
4. **Refill.** After each seal, if `limit − next ≤ b_k / 4` (integer division, `b_k` = size of the last reservation), the library schedules the next reservation.
5. **Where saves run.** The first reservation of a space is saved synchronously at the state entry or call named in rule 2, before that state processes its first slot. Every later save runs in the protocol task in a SLEEP slot or, if none comes within one superframe, in the first slot in which the node neither transmits nor expects a frame. Sealing never waits for storage, and no save runs inside the radio interrupt path.
6. **Hard stop.** A frame is never sealed with a counter value at or above the saved `limit`. If the refill has not completed, sealing fails with `kCounterStoreError` and the frame stays queued.
7. **Save semantics.** `Save` returns only after the record is durable. A failed save leaves the old limit in force. If a refill has not completed when a frame is due, that frame waits under rule 6. A forwarded beacon is then skipped for that superframe.
8. **Epoch switch.** The node saves `{epoch = e + 1, limit = 0}` for C_bcn, C_ctl and C_data before it sends the first frame under `e + 1`. The ceilings and the C_rec limit keep their values. The ramp restarts.

**Boot cost.** A boot in which the node seals `n` frames in a space consumes at most `3n + 1` values of that space, and 0 values if the trigger of rule 2 is never reached. Examples: n = 1 consumes 3 values (reservations of 1 and 2), n = 3 consumes 7, n = 48 consumes 112.

| Space | Values per boot, node sends nothing in it | Values per boot, one frame then reset | Storage writes per boot, one frame then reset |
|---|---|---|---|
| C_bcn | 0 | 3 | 2 |
| C_ctl | 0 | 3 | 2 |
| C_data | 0 | 3 | 2 |
| C_rec | 0 | 1 | 1 |

Steady state write rate: about one save per 48 beacons, plus one per 192 control or data frames. The review estimates 26 to 31 writes per day for a node at a 77 s superframe and about 180 for a gateway, which is no wear concern.

### S7.3 Missing or erased storage

The library never creates the record on its own in production. In secured mode, if the store is empty or the record fails its CRC, `Start()` fails with `kCounterStoreError` and the node stays silent. The flashing tool writes the first record together with the keys. A build for tests or development may call `setAllowVolatileCounters(true)`, which starts all counters at 0 without a store.

Requirements on the application and the tool:

1. The counter record and the keys share one fate. If one is erased, the node must be provisioned again.
2. An address is provisioned at most once per epoch. A node that is provisioned again gets a new address, or the deployment moves to a new epoch first (S9). In both cases all counters may start at 0. The version 1 scheme of generation ranges is removed, because it cut the beacon space to an eighth (finding 4).
3. A node that is provisioned again with a new K_node (`kver` raised) starts C_rec at 0. With an unchanged K_node the tool MUST write a C_rec limit above any value the old device can have used. The gateway knows the last C_rec it accepted from that address.

### S7.4 Receive state and windows

State per peer address, in RAM:

| Field | Size | Meaning |
|---|---|---|
| epoch | 1 | epoch of the last accepted frame of this peer |
| bcn_last | 3 | highest accepted C_bcn, 0xFFFFFF = none |
| ctl_last | 4 | highest accepted C_ctl, 0xFFFFFFFF = none |
| g_high | 4 | hop window G: highest accepted C_data of E = 0 frames, 0x3FFFFFFF = none |
| g_bitmap | 4 | bit b set: value `g_high − b` accepted |
| r_high, r_bitmap | 4 + 4 | hop window R: same for E = 1 DATA, DATA_RELIABLE and ACK that this node relayed |
| e2e_high, e2e_bitmap | 4 + 4 | end-to-end window: same for E = 1 frames whose tag was valid, only at the destination and only for peers with a node key |

RAM per peer: 24 bytes (1 + 3 + 4 + 8 + 8), plus 8 bytes for each peer with a node key. With the 2-byte address, a table of 43 peers (35 nodes + 8) takes 1,118 bytes.

Rules, applied only after the MIC of the frame is valid:

1. **Epoch.** A frame under an epoch older than the peer's recorded epoch is dropped. A frame under a newer epoch resets all fields of that peer to "none" and records the new epoch. A new epoch therefore also clears a hop window that a K_net holder pushed.
2. **Beacon and control (strict).** Accept iff the field is "none" or `c > last`. Then `last = c`. These frames travel one hop, so they arrive in order.
3. **Data (window of 32).** Let `d = c − high`.
   - Field is "none": accept, `high = c`, bitmap = 0xFFFFFFFF.
   - `c > high` and `d ≥ 32`: accept, `high = c`, bitmap = 0x00000001.
   - `c > high` and `d < 32`: accept, `bitmap = (bitmap << d) | 1`, `high = c`.
   - `c ≤ high` and `high − c < 32` and bit `high − c` is clear: accept and set it.
   - Otherwise drop.
4. Which window a frame uses is fixed by the table in S6.6 step 5. Window G: E = 0 frames, at relays and destinations. Window R: E = 1 DATA, DATA_RELIABLE and ACK, at relays only. End-to-end window: E = 1 frames, at the destination only, after the tag verified. No frame is checked in two windows at one node, and the key types are checked only in the end-to-end window.
5. The table holds `max_network_nodes + 8` peers. When it is full, the least recently heard peer that is absent from the routing table is replaced.
6. Receive state is kept across DISCOVERY, JOINING and FAULT_RECOVERY. Only a reboot or rule 1 clears it.

The existing duplicate caches and reliable delivery streams stay as they are.

### S7.5 Cold boot of a receiver, and the joining node

A receiver without state for a peer accepts the first valid frame in each space and then enforces the rules above. An attacker who recorded frames can therefore feed old frames to a freshly booted node until that node hears the genuine peer. The first genuine frame has a higher counter and ends the attack for that space. With the all-ones initial bitmap, nothing older than the first accepted data frame is accepted.

Mitigation for end-to-end data: the library calls `SaveRxFloor` (S11.3) for a peer when `e2e_high` has advanced by 256 or more since the last save and at least 10 minutes have passed since the last save for that peer. The save runs under S7.2 rule 5. After a reboot the saved value is the floor. A store may ignore the call. In that case, and for the frames since the last save in any case, an old command can be replayed once to a rebooted node. An application that acts on commands MUST keep its own freshness check, as LoRaChat does with its command counter.

**Joining node and replayed beacon.** A node in DISCOVERY joins on the first beacon that passes validation (`network_service.cpp:3322-3345`). After a cold boot it has no counter history, so a recorded beacon of the current epoch passes. Consequences and bounds:

1. The node takes a wrong superframe phase and sends its JOIN_REQUEST at the wrong time. Either the join times out and the node returns to DISCOVERY, or a neighbour hears the request and the join completes, after which the next genuine beacon (higher counter) corrects the phase.
2. Each recorded beacon works once per boot of the victim, because receive state survives the return to DISCOVERY (S7.4 rule 6). The attacker needs recordings with rising counters.
3. A recorded JOIN_RESPONSE is rejected, because its MIC is bound to an older join nonce (S4.5). The slot values a joiner accepts are fresh.
4. Beacons of an older epoch are useless once the grace period is over.

The remaining effect of a replayed beacon is a delay of the join, which a jammer achieves more simply. This is an accepted limit.

### S7.6 Exhaustion and reboot loops

When `ceiling − next` falls below 2^18 for C_bcn, or below 2^24 for the other spaces, the library raises the event `kCounterLow` once per boot. When `next == ceiling`, sealing in that space fails with `kCounterExhausted` and the node stops sending that class of frame. A new epoch (S9) restarts C_bcn, C_ctl and C_data. A wrap never happens.

Worst-case lifetime of one epoch for a node that resets again and again, for example a solar node that browns out. The assumption is the worst one the ramp allows: every boot reaches the trigger and seals exactly one frame in the space before the reset, so it costs 3 values.

| Space | Usable values | Boots until exhausted | Reset every superframe (77 s) | Reset every 5 s | No resets, one frame per 77 s |
|---|---|---|---|---|---|
| C_bcn | 16,777,215 | 5,592,405 | 13.6 years | 324 days | 41 years |
| C_ctl | 4,294,967,295 | 1,431,655,765 | 3,490 years | 227 years | over 10,000 years |
| C_data | 1,073,741,823 | 357,913,941 | 873 years | 57 years | over 2,600 years |

Notes:
- A node that resets every 5 s cannot join and forward a beacon in that time, so its real C_bcn cost is 0. The 324 days are an upper bound.
- Version 1 lost 64 beacon values per boot and gave 29 days at one reset per superframe.
- Storage: the loop writes 2 records per boot and touched space. At one reset per 5 s with one space touched this is about 34,500 writes per day. With the review's assumption of four NVS entries per record and five pages, a page is erased about 220 times per day and reaches 100,000 cycles after about 15 months of uninterrupted looping. The application SHOULD delay `Start()` after a brownout reset. Step 6 measures the real NVS geometry.

### S7.7 Deep sleep resume

A security section is appended to the resume snapshot (new snapshot format version):

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | section version (1) |
| 1 | 1 | transmit epoch |
| 2 | 1 | flags: bit 0 = a next key is installed (PENDING), bit 1 = grace period running |
| 3 | 4 | deadline of the pending or grace timer, protocol clock ms |
| 7 | 12 | next value of C_bcn, C_ctl, C_data (4 bytes each) |
| 19 | 12 | saved limit of C_bcn, C_ctl, C_data |
| 31 | 3 | size of the last reservation as log2, one byte per space, 0xFF = none since boot |
| 34 | 1 | peer count N (at most 64) |
| 35 | 18 × N | peer: address(2), epoch(1), bcn_last(3), ctl_last(4), g_high(4), r_high(4) |
| 35 + 18N | 1 | end-to-end peer count M (at most 64) |
| 36 + 18N | 7 × M | address(2), epoch(1), e2e_high(4) |

Size `36 + 18N + 7M` bytes: 673 bytes for 35 peers and one end-to-end peer. With the typical 2,200-byte snapshot the total is about 2,873 bytes, under `RtcStateStore::kCapacity = 4096`. This layout is final for step 2c. If N would exceed 64, the 64 most recently heard peers are kept.

Rules:

1. Bitmaps are not stored. On resume every bitmap is all ones, so only values above `high` are accepted.
2. On resume the counters and the ramp continue where they stopped. Nothing is wasted.
3. After the security state has been captured into a snapshot, the codec refuses to seal (`kInvalidState`) until the node resumes or the snapshot is taken again. A frame sent between capture and sleep would otherwise reuse a counter after wake-up.
4. The snapshot is consumed once (existing behaviour: `Start()` erases it). If it is absent or invalid, the node falls back to S7.2 rule 1, which is always safe because the stored limits are never below a used value.
5. Keys are never written to the snapshot. The provider supplies them again.
6. C_rec is not in the snapshot. Its block size is 1, so the store always holds the exact value.

---

## S8. Join

### S8.1 Version 1 (this specification)

Provisioning, done by the flashing tool:

| Item | Destination |
|---|---|
| node address (unique, never provisioned before in this epoch) | application configuration |
| K_node[i] | key provider storage |
| K_net[e] and e | key provider storage |
| address of the gateway | key provider configuration (decides for which peer `GetNodeKey` answers) |
| counter record with epoch e and all limits 0 (S7.3) | counter store |

The gateway holds K_mesh, or a table of K_node values, in its key provider.

Join procedure: unchanged from Section 6 of the protocol specification, with the join nonce of S4.5. The joiner reads beacons with K_he and K_hm, seals its JOIN_REQUEST as a class C2 frame with `tx` = its own address, and accepts the JOIN_RESPONSE that verifies with its nonce. A node whose network key is stale cannot read a beacon. It detects this and recovers through S9.5.

`JoinResponseHeader::AUTHENTICATION_FAILED` stays unused.

### S8.2 Later version: node flashed with K_node only

This needs no new format. A node without any network key is an orphan with `held_epoch = 255`. It runs the recovery of S9.5, receives K_net under its node key, and then joins as in S8.1. The provisioning list shrinks to the address, K_node, the gateway address and a counter record.

What remains to be done for that version: the unsynchronised request timing of S9.5 step 3 must be shown to work for a node that never had a schedule (S14.2).

---

## S9. Rekey, revocation and recovery (step 5, formats fixed now)

### S9.1 KEY_UPDATE and KEY_UPDATE_ACK

Both are class D unicast frames with E = 1 and domain 0x05. They are routed exactly like DATA. A relay needs no code for them beyond accepting the type.

KEY_UPDATE (0x51), gateway to node i, body 26 bytes, 48 bytes on air:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | new_epoch (epoch of the carrying frame + 1) |
| 1 | 1 | flags, zero |
| 2 | 16 | K_net[new_epoch] |
| 18 | 8 | kcv = `MIC_8(K_km[i,e], 0x08 ‖ addr_i(2) ‖ new_epoch ‖ flags ‖ K_net[new_epoch])` |

KEY_UPDATE_ACK (0x52), node to gateway, body 2 bytes, 24 bytes on air:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | new_epoch |
| 1 | 1 | status: 0 = installed, 1 = already installed or already active, 2 = rejected |

`e` is the epoch of the frame that carries the message. The body is encrypted by the body layer under K_ke[i,e] with the sender's ordinary C_data value. The end-to-end replay window covers it, and since finding 1 no hop window can block it.

### S9.2 Node state machine

| State | Transmit epoch | Accepted epochs | Leaves on |
|---|---|---|---|
| STABLE(e) | e | e | valid KEY_UPDATE for e + 1, or orphan detection (S9.5) |
| PENDING(e) | e | e, e + 1 | first valid frame of any class under e + 1 from any peer, or `epoch_grace_ms / 2` after the install |
| GRACE(e + 1) | e + 1 | e + 1, e | `epoch_grace_ms` after the switch |
| STABLE(e + 1) | e + 1 | e + 1 | |

**Sealing rule.** In every state a node seals every frame under its transmit epoch and with the counters of that epoch. No node, the gateway included, ever seals under another epoch. A counter of an epoch the node has left is never used again.

**Timer rule.** `rekey_distribution_timeout` MUST be at most `epoch_grace_ms / 4`. The defaults are 1 h and 4 h. The PENDING timer (half the grace period) is then at least twice the distribution time, so no node switches by timer while the gateway is still distributing. Sizing: with the forwarding cap of S6.6 the gateway's neighbours pass 2 key frames per superframe, so N nodes need about `N / 2` superframes. 35 nodes at 77 s need 23 minutes.

- STABLE to PENDING: verify tag and kcv, call `StoreNetworkKey` (S11.1), derive the keys of e + 1, answer with status 0. A KEY_UPDATE whose new_epoch is not e + 1 is answered with status 1 (if equal to an installed or active epoch) or 2.
- PENDING to GRACE: save the counter record for e + 1 (S7.2 rule 8), then transmit under e + 1. The timer exit exists so that PENDING nodes behind a relay that missed the update still switch and can then serve that relay as recovery neighbours.
- GRACE to STABLE: erase the keys of e, call `RetireNetworkKey(e)`.
- A node in PENDING or GRACE answers a repeated KEY_UPDATE for e + 1 with status 1. The answer is sealed under the node's transmit epoch (e in PENDING, e + 1 in GRACE), as the sealing rule demands. The gateway accepts both during its own grace period.

**State after a reboot.** The transmit epoch of the counter record is authoritative. Let `r` be that epoch and let the provider return the epochs `P`.

| Condition | State after boot |
|---|---|
| `P = {r}` | STABLE(r) |
| `P = {r, r + 1}` | PENDING(r), timer restarted at full length |
| `P = {r − 1, r}` in any order | GRACE(r), grace timer restarted at full length |
| every epoch in `P` is above `r` | switch to the highest one: save the record for it, then STABLE. Safe, because the record names the highest epoch the node ever transmitted under |
| every epoch in `P` is below `r` | `Start()` fails with `kKeyUnavailable` |

After a deep sleep resume the state and its deadline come from the snapshot (S7.7).

### S9.3 Gateway procedure and revocation

1. The server gives the gateway K_net[e + 1] and the list of nodes that stay.
2. The gateway sends KEY_UPDATE to each listed node and repeats it until the ACK arrives. Reliability is its own retry loop. DATA_RELIABLE is not used.
3. When all ACKs are in, or at `rekey_distribution_timeout`, the gateway switches its own transmit epoch. Its next frame under e + 1 makes its neighbours switch, and the switch spreads hop by hop.
4. **No retries after the switch.** Once the gateway has switched it seals only under e + 1 (sealing rule of S9.2). A node still in STABLE(e) cannot read those frames. It becomes an orphan and recovers through S9.5 without a visit. Version 2 let the gateway retry under epoch e. That is removed, because sealing under an epoch that was left is the nonce risk of N7.
5. The gateway SHOULD wait for the full `rekey_distribution_timeout` before it switches when ACKs are missing, since every node reached before the switch avoids a recovery.
6. A revoked node never receives the key. The gateway's provider returns no node key for it, so it gets no grant either. Its remaining powers until the grace periods end are those of S1.2 item 2.
7. KEY_UPDATE has a visible type and a fixed length of 48 bytes. A jammer can pick it out. Such a node falls back to S9.5, whose frames can be jammed in the same way. Accepted limit (S1.2 item 6).

### S9.4 What step 2 must already do so that S9 needs no format change

Reserve the K bit, the types 0x51 to 0x55, the domains 0x05, 0x09, 0x0A and the labels `LMKE`, `LMKM`, `LMRE`, `LMRM`. Relay the class D types 0x51, 0x52 and 0x55 like DATA. Drop 0x53 and 0x54 until step 5. Keep the per-peer epoch in the receive state. Take keys from the provider as a list of up to two epochs. Keep the C_rec field in the counter record.

### S9.5 Recovery of an orphan over the air

Purpose: a node that missed one or more rekeys, or that holds no network key at all, obtains the current K_net from the gateway under its own node key. No visit. A stolen node gains nothing from it.

**Roles.** Orphan `O`. Neighbour `M`: any member in NORMAL_OPERATION or NETWORK_MANAGER that hears `O`. Gateway `G`.

**Orphan detection.** A node in STABLE enters ORPHAN when, during 8 superframes of its last known duration (or 10 minutes without one), it received at least 8 secured frames that failed with `rx_unknown_epoch` or `rx_auth_failed`, while no frame verified. It raises `kOrphaned`. A node whose provider returns no network key starts in ORPHAN.

**Leaving ORPHAN without a grant.** The first frame of any class that verifies under a key the node holds ends the ORPHAN state at once. The node returns to the state it came from and stops sending requests. Junk frames in a quiet or jammed period therefore cost at most a few requests.

**Messages.**

KEY_REQUEST (0x53), `O` to `M`, class R, wire type 0xD3, 19 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 6 | BaseHeader: destination `M`, source `O`, type 0xD3, payload_size 13 |
| 6 | 1 | held_epoch (255 = none) |
| 7 | 4 | c_rec: C_rec of `O` |
| 11 | 8 | rtag = `MIC_8(K_rm[O], A_r ‖ held_epoch)` with `A_r = 0x09 ‖ O ‖ c_rec ‖ 0^7 ‖ 0^2` |

KEY_GRANT (0x54), `M` to `O`, class R, wire type 0xD4, 31 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 6 | BaseHeader: destination `O`, source `M`, type 0xD4, payload_size 25 |
| 6 | 1 | new_epoch |
| 7 | 16 | `C_g = CTR(K_re[O], A_g, K_net[new_epoch])` |
| 23 | 8 | gtag = `MIC_8(K_rm[O], A_g ‖ C_g)` |

`A_g = 0x0A ‖ O ‖ c_rec ‖ new_epoch ‖ 0^6 ‖ 0^2`. The grant does not carry `c_rec`. `O` knows it, and it is bound through `A_g`. For one `(O, c_rec, new_epoch)` the plaintext is always the same key, so a repeated grant repeats the same bytes and no nonce is reused on different content.

KEY_RELAY (0x55), class D unicast with E = 1 between `M` and `G`, body under K_ke and K_km of `M`:

| Direction | Body | Body bytes | On air |
|---|---|---|---|
| `M` to `G` | `0x00 ‖ O(2) ‖ held_epoch(1) ‖ c_rec(4) ‖ rtag(8)` | 16 | 38 |
| `G` to `M` | `0x01 ‖ O(2) ‖ new_epoch(1) ‖ C_g(16) ‖ gtag(8)` | 28 | 50 |

**When a member listens.** Read from `SlotScheduler::FillSlotTableLocked` (`slot_scheduler.cpp:262-328`) and `ComputeBandSizes` (`slot_scheduler.cpp:121-125`). `d` is the member's hop distance to the manager, `depth` the current network depth.

| Slots, counted from superframe start | Member at distance d | Code |
|---|---|---|
| 0 | receive (the manager transmits) | lines 267-271 |
| 1 .. depth, sync layer k | transmit if k = d, receive if k = d − 1, SLEEP otherwise | lines 272-278 |
| control band, one slot per control index | transmit in its own, receive in all others | lines 283-293 |
| data band | transmit in its own, receive in slots of owners it has a route entry for, SLEEP otherwise | lines 298-313 |
| elastic buffer | SLEEP | lines 316-323 |
| last `2 × (depth + 1)` slots | DISCOVERY_RX for every member (one of them becomes DISCOVERY_TX when the member has a join frame to send) | lines 121-122, 326-328 |

The sync slot after a member's own beacon belongs to the next hop layer. The member sleeps there, which is why the version 2 rule (send right after an overheard beacon) could not work. The discovery band is the only place where every member listens for frames from unknown senders, so the request goes there.

**Timing of the request (derived from the code, still to be simulated).** Let `T_s` be the slot duration and `Δ` the set of subslot start offsets of a 26-byte frame. `O` computes both from its own radio and protocol configuration, which is deployment-wide (950 ms and {50, 356, 662} ms at SF9 BW125). A member at distance `d` starts its beacon at `S + δ`, where `S` is the start of slot `d` and `δ` is one element of `Δ` that changes per superframe.

1. `O` stays in receive and records the start time `B` (end of reception minus the airtime of the on-air length) of beacon frames of one source `M`, using only the clear header.
2. From two recorded beacons `O` takes `P = round((B2 − B1) / T_s) × T_s` after removing the offset difference: it picks the pair `(δ1, δ2)` from `Δ` for which `B2 − B1 − (δ2 − δ1)` is closest to a multiple of `T_s`. If beacons were missed, `P` is a multiple of the superframe, which is harmless.
3. `O` predicts `R = B_last + P − δ_mid`, with `δ_mid` the middle element of `Δ` (356 ms). `R` is within 306 ms of the start of M's next beacon slot.
4. `O` starts its request at `R − k × T_s + 350 ms`. With the 226 ms request this lies inside one DISCOVERY_RX slot of `M` for every `k` from `d + 1` to `d + 2 × (depth + 1)`.
5. `O` does not know `d`. It rotates `k` through 2, 5, 9, 14 on successive attempts. For every `d` from 0 to 13 at least one of these values is in range.

**Procedure.**

1. `O` picks as `M` the source address of a secured frame it overhears whose clear type is SYNC_BEACON. That address belongs to a live member.
2. `O` takes the next C_rec value (one storage save, S7.2) and builds the KEY_REQUEST. It remembers the last 8 C_rec values of this ORPHAN episode.
3. `O` sends at the time computed above. Attempts are spaced 1, 2, 4, 8, 16 and then 32 superframes apart. Every attempt uses a new C_rec value and the next `k`. After 4 attempts without a grant `O` picks another `M` if it hears one.
4. `M` accepts a KEY_REQUEST addressed to itself without any K_net check. It relays at most one request per 16 superframes, whatever the source address, since source addresses are free to claim. A request over the limit is dropped (`rx_recovery_dropped`). `M` wraps an accepted request in a KEY_RELAY to `G`. It keeps no table.
5. `G` asks its provider for the node key of `O`. No key (unknown or revoked address): drop silently. It verifies `rtag` and requires `c_rec` at or above the last value it accepted from `O` (RAM state, "none" after a gateway reboot). For an equal `c_rec` it repeats the same grant, which is safe because the grant bytes are fixed per `(O, c_rec, epoch)`. It answers at most one request per pair `(O, M)` in 4 superframes and at most 4 per `O` in 16 superframes. It builds the grant for its current transmit epoch and sends the KEY_RELAY to the `M` that relayed this request.
6. `M` receives the KEY_RELAY from `G`, which is authenticated end to end, and queues a KEY_GRANT to the address `O` named in it, like a join response (DISCOVERY_TX, with the existing fallback in DISCOVERY_RX slots). It sends the grant once.
7. `O` accepts a KEY_GRANT only while it is in ORPHAN and only if `gtag` verifies for one of its remembered C_rec values. It then compares `new_epoch` with the transmit epoch `r` of its counter record:

   | Case | Action |
   |---|---|
   | `new_epoch > r` | call `StoreNetworkKey`, save the counter record for `new_epoch` with the three traffic limits at 0 |
   | `new_epoch == r` | call `StoreNetworkKey` if the key is not held. The counter record is left as it is, so the counters continue above every value used before |
   | `new_epoch < r` | refuse (`rx_tag_failed` is not counted, the event `kOrphaned` is raised again with `detail` = `new_epoch`) |

   This rule holds for every orphan, including one that reports `held_epoch = 255`. Resetting the limits in an epoch the node already transmitted under would reuse nonces under K_net keys (N4). After an accepted grant `O` raises `kRecovered` and enters DISCOVERY to join as in S8.1.

**Why a stolen node cannot abuse it.**

| Attempt | Outcome |
|---|---|
| A revoked node sends its own KEY_REQUEST | `G` has no node key for it and stays silent |
| It forges a request for another address | it lacks that node's K_rm, `rtag` fails at `G` |
| It replays a recorded request of `O` | an older `c_rec` is refused. An equal one, or any one after a gateway reboot, produces the grant `O` would get anyway, encrypted for `O` alone and sent to the member that relayed the replay. The per-pair limit at `G` keeps this from using up the answers meant for the honest path |
| It replays an old grant to `O` | it verifies only if its `c_rec` is one of the last 8 of the current episode. Then it carries a genuine key of an epoch at or above the record epoch, or it is refused by step 7 |
| It acts as `M` and forges a grant | it lacks K_re and K_rm of `O` |
| It acts as `M` and drops requests | `O` picks its neighbour again on each attempt from the beacons it hears, so one honest neighbour suffices |
| It floods requests, as would any outsider | each `M` relays one request per 16 superframes, each relay forwards at most 2 key frames per superframe (S6.6), and `G` limits its answers |
| It reads the exchange | it learns `O`, the held epoch and the new epoch number. K_net is encrypted under K_re of `O` |

The request link has no K_net protection by design, since `O` has no valid K_net. A party without any key can therefore make each member in its range send one frame per 16 superframes toward the gateway. This is the price of recovery without a visit.

---

## S10. Size and timing budget

Radio setting for all airtimes: SF9, BW 125 kHz, CR 4/7, preamble 8, CRC on, explicit header. Values from `airtime::CalculateTimeOnAirMs`, reproduced by the script in S12 and equal to the table in the decisions file. The reviewer recomputed 26, 27, 101 and 115 bytes.

### S10.1 Per frame type

| Frame | Bytes today | Bytes secured | Added | Airtime today (ms) | Airtime secured (ms) |
|---|---|---|---|---|---|
| SYNC_BEACON | 20 | 26 | 6 (ctr 3, mic 4, delay −1) | 226 | 255 |
| ROUTE_TABLE, n entries | 13 + 10n | 21 + 10n | 8 | | |
| ROUTE_TABLE, largest fragment | 103 (9 entries) | 101 (8 entries) | | 771 | 742 |
| JOIN_REQUEST, first hop | 12 | 26 | 14 (nonce 4, trailer 10) | 169 | 255 |
| JOIN_REQUEST, forwarded | 12 | 22 | 10 | 169 | 255 |
| JOIN_RESPONSE | 15 | 25 | 10 | 198 | 255 |
| NM_CLAIM | 10 | 18 | 8 | 169 | 226 |
| SLOT_REQUEST | 7 | 15 | 8 | 140 | 198 |
| SLOT_ALLOCATION | 10 | 18 | 8 | 169 | 226 |
| DATA, DATA_BROADCAST, n bytes | 10 + n | 22 + n | 12 | 169 (n = 0) | 255 (n = 0) |
| DATA_RELIABLE, n bytes | 15 + n | 27 + n | 12 | 198 (n = 0) | 284 (n = 0) |
| DATA_GROUP, n bytes | 11 + n | 23 + n | 12 | 169 (n = 0) | 255 (n = 0) |
| ACK | 16 | 28 | 12 | 198 | 284 |
| KEY_UPDATE | | 48 | | | 398 |
| KEY_UPDATE_ACK | | 24 | | | 255 |
| KEY_REQUEST | | 19 | | | 226 |
| KEY_GRANT | | 31 | | | 312 |
| KEY_RELAY up, down | | 38, 50 | | | 341, 427 |
| Largest frame | 115 | 115 | 0 | 828 | 828 |

### S10.2 Maximum application payload

`max_payload = max_packet_size − 6 − len(X) − prefix − 12`

| Send type | Today at 115 | Secured at 115 | Secured at 242 (SF7, SF8) | Secured at 51 (SF10 to SF12) |
|---|---|---|---|---|
| DATA, DATA_BROADCAST | 105 | 93 | 220 | 29 |
| DATA_RELIABLE | 100 | 88 | 215 | 24 |
| DATA_GROUP | 104 | 92 | 219 | 28 |
| DATA_GROUP with ACKs | 99 | 87 | 214 | 23 |

### S10.3 Routing entries per fragment

`ComputeBroadcastSliceCapacity` (`network_service.cpp:1321-1345`) uses `header_overhead = 6 + 7`. In secured mode it becomes `6 + 7 + 8 = 21`.

| Limit | Today | Secured |
|---|---|---|
| By packet size, 115 bytes | (115 − 13) / 10 = 10 | (115 − 21) / 10 = 9 |
| After the slot check `100 + 50 + ToA + 20 ≤ 950`, so ToA ≤ 780 ms, so at most 107 bytes | 9 (103 bytes, 771 ms) | 8 (101 bytes, 742 ms) |
| By packet size, 51 bytes | 3 | 3 |
| By packet size, 242 bytes | 22 | 22 |

A full table of 35 nodes needs 4 fragments today and 5 secured.

### S10.4 Proof: the beacon stays at 26 bytes

`6 (BaseHeader) + 13 (sync fields with a 3-byte delay) + 3 (ctr) + 4 (mic) = 26`. The delay fits 3 bytes because it is below `255 × 65,535 = 16,711,425 < 16,777,216`. Sizes 22 to 26 share one airtime, 255 ms. `SubslotScheduler::ComputeTiming` with a 950 ms slot, 50 ms guard and 5 configured subslots keeps 3 subslots at offsets 50, 356 and 662 ms. The last one ends at `662 + 255 + 20 = 937 ≤ 950`, so `CanFitInSlot` holds in every subslot. At 27 bytes the airtime is 284 ms and the last subslot gives 966, which is the broken band named in the decisions file. A static assertion MUST pin `kSecuredSyncBeaconSize == 26`.

### S10.5 Proof: the on-air maximum stays 115

1. The codec refuses to seal when `6 + payload_size + TrailerLen > max_packet_size` (`kBufferOverflow`). Nothing longer reaches the radio.
2. The send functions reject application data above the values of S10.2 before queueing.
3. The routing slice capacity uses the secured overhead (S10.3).
4. Every frame with a fixed size is far below the limit: the largest are KEY_RELAY down (50) and KEY_UPDATE (48). Control frames other than ROUTE_TABLE are at most 26 bytes.
5. `CalculateMinSlotDuration` keeps using `ToA(max_packet_size)`. The slot stays 950 ms.

### S10.6 Subslot bands for frames sent in subslotted slots

Model of `TrySendSubslottedMessage` at slot start (slot 950 ms, guard 50 ms, 5 subslots configured). "Unsafe" means the last kept subslot fails `CanFitInSlot` and the frame goes out at once without guard.

| Size band (bytes) | Airtime (ms) | Subslots kept | Last subslot safe |
|---|---|---|---|
| 9 to 12 | 169 | 4 | yes |
| 13 to 17 | 198 | 4 | no |
| 18 to 21 | 226 | 3 | yes |
| 22 to 26 | 255 | 3 | yes |
| 27 to 30 | 284 | 3 | no |
| 31 to 48 | 312 to 398 | 2 | yes |
| 49 to 53 | 427 | 2 | no |
| 54 to 115 | 456 to 828 | 1 | yes |

Secured frames sent in such slots: SYNC_BEACON 26, JOIN_REQUEST 26 and 22, JOIN_RESPONSE 25, NM_CLAIM 18, KEY_REQUEST 19, KEY_GRANT 31. All sit in safe bands. Today's 15-byte JOIN_RESPONSE sits in an unsafe band according to this model. Any future growth of these frames MUST be checked against this table. KEY_RELAY and KEY_UPDATE travel in data slots, which use the guarded path.

### S10.7 Sites that need the on-air length

Every site computes from one function `security::OnAirSize(type, plain_total_size)`. In secured mode it returns `plain_total_size + TrailerLen(type)`, minus 1 for SYNC_BEACON. Otherwise it returns `plain_total_size`. Sites: `lora_mesh_protocol.cpp:1429`, `1473-1477`. `hardware_manager.cpp:123,132`. `network_service.cpp:1324-1341`, `1550`, `1741-1747`, `2437`. `reliable_messaging.cpp:162`, `462`, `522`. `reliable_delivery.hpp:157`.

---

## S11. Library API

The library holds no key storage policy. It asks for keys, asks for the counter record, and reports events.

### S11.1 Key provider

```cpp
namespace loramesher::security {

using Key128 = std::array<uint8_t, 16>;

struct NetworkKey {
    uint8_t epoch;
    Key128 key;
};

class IKeyProvider {
   public:
    virtual ~IKeyProvider() = default;

    /// Network keys the node holds, in any order, at most two. Returns the
    /// number written (0, 1 or 2). 0 makes the node start as an orphan.
    virtual size_t GetNetworkKeys(std::array<NetworkKey, 2>& out) = 0;

    /// Per-node key shared with `peer`. A node returns its own K_node when
    /// `peer` is its gateway and std::nullopt for any other peer. A gateway
    /// returns K_node of `peer`, or std::nullopt for an unknown or revoked
    /// address.
    virtual std::optional<Key128> GetNodeKey(AddressType peer) = 0;

    /// Address of the gateway that holds this node's key, 0 on a gateway.
    virtual AddressType GetGatewayAddress() = 0;

    /// Rekey and recovery (step 5): persist a newly received network key.
    virtual Result StoreNetworkKey(const NetworkKey& next) {
        return Result(LoraMesherErrorCode::kNotImplemented, "rekey");
    }

    /// Rekey (step 5): the epoch is no longer accepted, erase its key.
    virtual void RetireNetworkKey(uint8_t epoch) {}
};

}  // namespace loramesher::security
```

The library calls `GetNetworkKeys` in `Start()` and after `StoreNetworkKey`. Which key is current is decided by the counter record (S9.2). It calls `GetNodeKey` once per peer and caches the derived keys (cache of `max_network_nodes` entries on a gateway, 1 entry on a node). `InvalidateKeys()` on the protocol clears the caches.

### S11.2 Configuration

```cpp
enum class SecurityMode : uint8_t {
    kDisabled,  ///< 2.0.0 behaviour: plain frames, secured frames are rejected
    kSecured,   ///< every frame secured, plain frames are dropped
};

// LoRaMeshProtocolConfig
void setSecurityMode(security::SecurityMode mode);            // default kDisabled
void setKeyProvider(std::shared_ptr<security::IKeyProvider> provider);
void setCounterStore(std::shared_ptr<security::ICounterStore> store);
void setEpochGraceMs(uint32_t ms);                            // default 14'400'000
void setRekeyDistributionTimeoutMs(uint32_t ms);              // default 3'600'000, at most grace / 4
void setAllowVolatileCounters(bool allow);                    // default false, tests only

// LoraMesher::Builder, all must follow withLoRaMeshProtocol()
Builder& withSecurity(std::shared_ptr<security::IKeyProvider> provider,
                      std::shared_ptr<security::ICounterStore> store);  // sets kSecured
Builder& withEpochGrace(uint32_t ms);
```

`Configure()` fails with `kSecurityNotConfigured` when the mode is `kSecured` and the provider is missing, or the store is missing while volatile counters are not allowed. `PingPongProtocol` does not support `kSecured`.

### S11.3 Counter store

```cpp
namespace loramesher::security {

struct CounterRecord {
    uint8_t tx_epoch = 0;
    uint32_t limit[4] = {0, 0, 0, 0};  ///< 0 C_bcn, 1 C_ctl, 2 C_data, 3 C_rec
    uint32_t ceiling[3] = {0x00FFFFFF, 0xFFFFFFFF, 0x3FFFFFFF};
};

class ICounterStore {
   public:
    virtual ~ICounterStore() = default;
    /// The record, or std::nullopt when none exists or it is corrupt.
    virtual std::optional<CounterRecord> Load() = 0;
    /// Returns only when the record is durable.
    virtual Result Save(const CounterRecord& record) = 0;
    /// Optional floor of the end-to-end receive window of a peer (S7.5).
    virtual std::optional<uint32_t> LoadRxFloor(AddressType peer, uint8_t epoch) {
        return std::nullopt;
    }
    virtual void SaveRxFloor(AddressType peer, uint8_t epoch, uint32_t high) {}
};

}  // namespace loramesher::security
```

The library ships `MemoryCounterStore` (tests) and `NvsCounterStore` (ESP32, 40-byte blob of S7.2 under one NVS key). `EncodeCounterRecord` and `DecodeCounterRecord` are public so the flashing tool and other stores use the same bytes.

### S11.4 Maximum payload

```cpp
/// Largest application payload accepted for this send type with the current
/// radio settings and security mode (S10.2). `type` is DATA, DATA_BROADCAST,
/// DATA_RELIABLE or DATA_GROUP. `group_acks` applies to DATA_GROUP only.
size_t LoraMesher::GetMaxPayloadSize(MessageType type, bool group_acks = false) const;
```

`ReliableDelivery::kMaxReliablePayload` (`reliable_delivery.hpp:157`) stays the format limit. The send path additionally checks `GetMaxPayloadSize`.

### S11.5 Error codes

Appended after `kQueueFull`, so existing values keep their numbers:

| Code | Returned when |
|---|---|
| `kSecurityNotConfigured` | secured mode without provider or store |
| `kKeyUnavailable` | no usable network key, or no key for the frame's epoch or peer |
| `kAuthenticationFailed` | a MIC or tag did not verify (internal to the codec, counted) |
| `kReplayDetected` | counter outside the window (internal to the codec, counted) |
| `kCounterStoreError` | record missing or corrupt, save failed, or limit reached before the refill |
| `kCounterExhausted` | a counter reached its ceiling |
| `kPayloadTooLarge` | application data above `GetMaxPayloadSize` |

Dropped received frames never surface as errors to the application. They are counted.

### S11.6 Counters and events

```cpp
struct SecurityStats {
    uint32_t tx_sealed;
    uint32_t tx_refused;            ///< counter, key or size problem
    uint32_t rx_opened;
    uint32_t rx_plain_dropped;      ///< S = 0 in secured mode
    uint32_t rx_malformed;          ///< length rule, H flag
    uint32_t rx_unknown_epoch;
    uint32_t rx_auth_failed;        ///< MIC or hop MIC
    uint32_t rx_tag_failed;         ///< body tag, kcv, rtag, gtag
    uint32_t rx_replay;             ///< includes flood duplicates
    uint32_t rx_e2e_required;
    uint32_t rx_key_unavailable;
    uint32_t rx_recovery_dropped;   ///< KEY_REQUEST over a rate limit
    uint32_t rx_key_forward_dropped;///< key frame dropped by the forwarding cap
    uint32_t rx_join_unverified;    ///< JOIN_RESPONSE for another node, MIC failed
    uint32_t counter_store_writes;
    uint8_t  tx_epoch;
};
SecurityStats LoraMesher::GetSecurityStats() const;

enum class SecurityEvent : uint8_t {
    kCounterLow, kCounterExhausted, kCounterStoreFailure,
    kEpochPending, kEpochSwitched, kEpochRetired,
    kAuthFailureBurst, kOrphaned, kRecovered,
};
void LoraMesher::SetSecurityEventCallback(std::function<void(SecurityEvent, uint32_t detail)>);
```

`kAuthFailureBurst` fires when `rx_auth_failed + rx_tag_failed` grew by 32 or more within 10 minutes, at most once per 10 minutes. `detail` is the source address of the last failed frame.

### S11.7 Internal codec (one implementation for three paths)

```cpp
class FrameCodec {
   public:
    static size_t TrailerLen(MessageType plain_type);          // 0 when disabled
    /// Message in, on-air bytes out. Takes the counter for frames this node
    /// originates.
    Result Seal(const BaseMessage& message, std::span<uint8_t> out, size_t& out_len);
    /// On-air bytes in, message out. Steps 1 to 5 of S6.6, all of S4.2 and S5.1.
    std::optional<BaseMessage> Open(std::span<const uint8_t> frame);
    /// Class D only: verify the tag and return a copy with the body decrypted
    /// (S6.6 step 6). The input message is left untouched.
    std::optional<BaseMessage> OpenBody(const BaseMessage& message);
};
```

- TX: `HardwareManager::SendMessage` calls `Seal` where it calls `Serialize` today.
- RX: `RadioLibRadio::HandleInterrupt` calls `Open` where it calls `BaseMessage::CreateFromSerialized` today. `MockRadio` calls the same two functions, so native tests exercise the real codec.
- `BaseMessage` gains a 10-byte `SecurityContext` with the fields `ctr_field` (4), `tag` (4), `epoch` (1) and `body_state` (1). `Open` fills it for class D frames. After `Open` the extension `X` is unmasked and the body is the received ciphertext (`body_state = kCipher`). `OpenBody` returns a copy with `body_state = kPlain`.
- The network service calls `OpenBody` when the node is the destination or the type is a flood type. It delivers from the copy. This includes the reliable prefix check of group floods (`reliable_messaging.cpp:232-241`). It forwards the original message.
- `DataMessage::CreateForwarded`, `BroadcastMessage::CreateForwarded` and `GroupMessage::CreateForwarded` copy the context and the ciphertext body.
- `Seal` of a class D message whose source is another node requires `body_state == kCipher`. It copies body, `ctr_field` and `tag`, and computes only the hop MIC and the mask. With `kPlain` it fails with `kInvalidState`. This enforces S6.6: no code path can encrypt under another node's counter.
- `Seal` of a class D message whose source is this node takes the next C_data value and runs the body layer.

---

## S12. Test vectors

Generated with `docs/security/security_vectors.py` (Python `cryptography` 46.0.7). The script checks itself against RFC 4493 example 2 (`070A16B46B4D4144F79BDD9DD04A287C`).

All vectors, V1 to V7 and the keys of S12.1, were reproduced byte for byte by the reviewer with an independent AES and CMAC (`reviews/verify_vectors_1.py`, `reviews/verify_vectors_2.py`). Version 3 changes no vector.

Step 1 and step 2 MUST reproduce all of them with the C++ code.

Addresses: gateway 0x0A01, relay 0x0A02, node 0x0A03. Epoch 1, so K = 1.

### S12.1 Keys

```
K_dep         = 404142434445464748494A4B4C4D4E4F505152535455565758595A5B5C5D5E5F
K_mesh        = 9518225659933114877EA98D05C6C3F49882C16FDAC302062C6F8858B3FADC1F
K_node[0A03]  = 6F77DB437E50C7A24B5446B6F480043C      (kver 0, input "LMN1" 03 0A 00)
K_net[1]      = 000102030405060708090A0B0C0D0E0F      (given directly for the vectors)

K_he = 3F190153DF3104694764132CA0361034      CMAC(K_net, "LMHE" 01)
K_hm = 8D5A63B2B6C12FA7238DA6FE941039E0      CMAC(K_net, "LMHM" 01)
K_hp = 2977A7AE47612A1374D011B64BADF207      CMAC(K_net, "LMHP" 01)
K_ge = C2CCEC823A94FA9804AB593CB957157A      CMAC(K_net, "LMGE" 01)
K_gm = DC623F94FDCB6D1C4F18915D080E235B      CMAC(K_net, "LMGM" 01)
K_ee = B3DE076E7DC79D6E8C0218737EC1271D      CMAC(K_node, "LMEE" 01)
K_em = 1E98DEB578EDE6937E90FB6234BFF9AF      CMAC(K_node, "LMEM" 01)
K_ke = 7AE7E29E33AC0FAC8F36B34B39E0C13C      CMAC(K_node, "LMKE" 01)
K_km = 46CB3EA2F926EAF374D610071870ADFD      CMAC(K_node, "LMKM" 01)
K_re = 30E06F4E6E136E109E51029C8EDEE484      CMAC(K_node, "LMRE" 00)
K_rm = 140EA8FFE794FEE93CBE35BF73FE178D      CMAC(K_node, "LMRM" 00)
```

### S12.2 V1: sync beacon forwarded by 0x0A02

Fields: network_id 0xBEEF, total_slots 40, slot_duration 950, network_manager 0x0A01, hop_count 2, propagation_delay 1234, max_hops 5, node_count 12. C_bcn = 0x000123.

```
plain P  = EFBE28B603010A02D2040000050C          (14 bytes, byte 11 = 00)
P13      = EFBE28B603010A02D20400050C            (13 bytes)
H        = FFFF020ACE0D
A        = 02020A23010000000000000000000000
C        = D42A669BCCD9CDAFE4FB2AFE88
CMAC     = 86E3AF6E1845FE2A004798193176B609      over A ‖ H ‖ C (35 bytes)
trailer  = 230100 86E3AF6E
frame    = FFFF020ACE0DD42A669BCCD9CDAFE4FB2AFE8823010086E3AF6E      (26 bytes)
```

### S12.3 V2: routing table fragment from 0x0A02, two entries

Header fields: network_manager 0x0A01, table_version 7, entry_count 2, source_capabilities 0, source_allocated_slots 2, source_control_slot_index 1. Entry 1: destination 0x0A01, hops 1, link_quality 230, data slots 2, capabilities 0x01, control slot 0, reception_quality 220, next_hop 0x0A01. Entry 2: destination 0x0A03, hops 1, link_quality 200, data slots 1, capabilities 0, control slot 2, reception_quality 190, next_hop 0x0A03. C_ctl = 0x00000457.

```
H        = FFFF020ABA1B
P        = 010A0702000201010A01E6020100DC010A030A01C8010002BE030A
A        = 01020A57040000000000000000000000
S_1      = D3F7AE12C18D3354AA539EAAB2F31ABB
S_2      = 9E9CB6D4A99AA24C768FF37C772A807F
C        = D2FDA910C18F3255A05278A8B3F3C6BA949FBCD5619BA24EC88CF9
CMAC     = 74482AA8649807BB058F3C6CF3204441      over A ‖ H ‖ C (49 bytes)
trailer  = 57040000 74482AA8
frame    = FFFF020ABA1BD2FDA910C18F3255A05278A8B3F3C6BA949FBCD5619BA24EC88CF95704000074482AA8      (41 bytes)
```

### S12.4 V3: end-to-end DATA from 0x0A03 to gateway 0x0A01 over relay 0x0A02

Application payload "temp=21.5C" (10 bytes). seq_num 0x37, initial ttl 10. C_data = 0x00000029, E = 1, H = 0.

Body layer (computed once by the origin, identical on both hops):

```
H        = 010A030A990E
body     = 74656D703D32312E3543
ctr      = 29000080
A_p      = 04030A29000000000000000000000000
S_1      = 542B0AC1A3C5CC864F799AADC8936948      under K_ee
C        = 204E67B19EF7FDA87A3A
X_im     = 00000037
CMAC     = 5E7BC82C6564F2F1523B48B7E1CF596B      under K_em over A_p ‖ H ‖ X_im ‖ C (36 bytes)
tag      = 5E7BC82C
```

Hop 1, sent by 0x0A03 (next_hop 0x0A02, ttl 10):

```
X        = 020A0A37
hop CMAC = CF6288E43BC349279B5CB92D6676D6F2      under K_hm over 06 ‖ H ‖ X ‖ C ‖ ctr ‖ tag (29 bytes)
hmic     = CF6288E4
sample   = 290000805E7BC82CCF6288E4030A010A
mask     = 216CE481375A3298D78913B1D61825C0      AES(K_hp, sample)
X_wire   = 2366EEB6
frame    = 010A030A990E2366EEB6204E67B19EF7FDA87A3A290000805E7BC82CCF6288E4      (32 bytes)
```

Hop 2, sent by relay 0x0A02 (next_hop 0x0A01, ttl 9). The relay verified hmic of hop 1, copied H, C, ctr and tag, touched no replay window (E = 1), and used no key of the node:

```
X        = 010A0937
hop CMAC = 7F2F90E26B4F8364977282ABF07CCD01
hmic     = 7F2F90E2
sample   = 290000805E7BC82C7F2F90E2030A010A
mask     = 59B05E3BA57C37FDA5149B79F62428F5
X_wire   = 58BA570C
frame    = 010A030A990E58BA570C204E67B19EF7FDA87A3A290000805E7BC82C7F2F90E2      (32 bytes)
```

### S12.5 V4: DATA_BROADCAST from 0x0A03, E = 0

Payload "hello mesh", seq_num 0x38, ttl 10, C_data = 0x0000002A.

```
H        = FFFF030A9A0E
A_p      = 03030A2A000000000000000000000000
C        = E1BFFE005EF6AF1B37A1                  under K_ge
X_im     = 00000038
CMAC     = 4F6D5274611A3904CC8DC4BA806A4FBB      under K_gm, tag = 4F6D5274
X        = FFFF0A38
hop CMAC = BFD0F5924CC96460B5CE747DE718CBA4      hmic = BFD0F592
sample   = 2A0000004F6D5274BFD0F592030AFFFF
mask     = 25C3618D804C365119B868C49538F7A2
frame    = FFFF030A9A0EDA3C6BB5E1BFFE005EF6AF1B37A12A0000004F6D5274BFD0F592
```

### S12.6 V5: join with nonce

Join nonce `C0FFEE42`.

V5a, JOIN_REQUEST sent by joiner 0x0A03 to sponsor 0x0A02. Fields: requested_slots 1, next_hop 0x0A02, sponsor 0x0A02, hop_count 0. `tx` = 0x0A03, C_ctl of 0x0A03 = 0.

```
H        = 010A030ACA0A
P        = 01020A020A00 C0FFEE42
A        = 01030A00000000000000000000000000
C        = 566DDDC4E6B7A23E0525
CMAC     = 241A769CC1245EC1651572634D96DE15      over A ‖ H ‖ C
trailer  = 030A 00000000 241A769C
frame    = 010A030ACA0A566DDDC4E6B7A23E0525030A00000000241A769C      (26 bytes)
```

V5b, the same request forwarded by 0x0A02 toward the manager, nonce removed. Fields: next_hop 0x0A01, hop_count 1. `tx` = 0x0A02, C_ctl of 0x0A02 = 2.

```
H        = 010A030ACA06
P        = 01010A020A01
A        = 01020A02000000000000000000000000
C        = 0173303374AC
CMAC     = 789342C95787843B6FC8C078563ADA1E
trailer  = 020A 02000000 789342C9
frame    = 010A030ACA060173303374AC020A02000000789342C9      (22 bytes)
```

V5c, final JOIN_RESPONSE from sponsor 0x0A02 to joiner 0x0A03. Fields: network_id 0xBEEF, allocated_slots 1, status 0, next_hop 0x0A03, target_address 0, control_slot_index 2. `tx` = 0x0A02, C_ctl of 0x0A02 = 3. The nonce enters the MIC input only. The frame does not carry it.

```
H        = 030A010ACB09
P        = EFBE0100030A000002
A        = 01020A03000000000000000000000000
C        = E8BDEC55C0D6D23B05
CMAC     = C3D5D4EB8B14586379617A8CBDD2A8B6      over A ‖ H ‖ C ‖ C0FFEE42
trailer  = 020A 03000000 C3D5D4EB
frame    = 030A010ACB09E8BDEC55C0D6D23B05020A03000000C3D5D4EB      (25 bytes)
```

### S12.7 V6: KEY_UPDATE from gateway to 0x0A03, new epoch 2

K_net[2] = `A0A1A2A3A4A5A6A7A8A9AAABACADAEAF`, flags 0, C_data of the gateway = 0x00000100, seq_num 0x05, ttl 10, next_hop 0x0A02.

```
kcv      = DC1E63C2EA1226D4      MIC_8(K_km, 08 030A 02 00 K_net[2])
body     = 0200A0A1A2A3A4A5A6A7A8A9AAABACADAEAFDC1E63C2EA1226D4
H        = 030A010AD91E
A_p      = 05010A00010000000000000000000000
C        = 892C29831AFBF2DD1512B9FA14EEB8B1852CD4578B2822E8BA5C      under K_ke
CMAC     = 8CC2C9BC07A3E979FD5B8D62250B4FFA      under K_km, tag = 8CC2C9BC
X        = 020A0A05
hmic     = FC9B89C8
sample   = 000100808CC2C9BCFC9B89C8010A030A
frame    = 030A010AD91E4246256A892C29831AFBF2DD1512B9FA14EEB8B1852CD4578B2822E8BA5C000100808CC2C9BCFC9B89C8      (48 bytes)
```

### S12.8 V7: recovery of 0x0A03 through neighbour 0x0A02

The orphan holds epoch 1. The network is at epoch 3 with K_net[3] = `B0B1B2B3B4B5B6B7B8B9BABBBCBDBEBF`. C_rec = 0x00000007.

```
A_r         = 09030A07000000000000000000000000
rtag        = 261F520C68E88AFD                    MIC_8(K_rm, A_r ‖ 01)
KEY_REQUEST = 020A030AD30D 01 07000000 261F520C68E88AFD      (19 bytes)

A_g         = 0A030A07000000030000000000000000
C_g         = DAC4A8CE528B55C9F0BB2FEB5697BD6B    CTR(K_re, A_g, K_net[3])
gtag        = BAC34B148A020B19                    MIC_8(K_rm, A_g ‖ C_g)
KEY_GRANT   = 030A020AD419 03 DAC4A8CE528B55C9F0BB2FEB5697BD6B BAC34B148A020B19      (31 bytes)

KEY_RELAY body, 0x0A02 to gateway = 00 030A 01 07000000 261F520C68E88AFD      (16 bytes, 38 on air)
KEY_RELAY body, gateway to 0x0A02 = 01 030A 03 DAC4A8CE528B55C9F0BB2FEB5697BD6B BAC34B148A020B19      (28 bytes, 50 on air)
```

The two KEY_RELAY frames are ordinary class D frames with E = 1 under the keys of 0x0A02. They follow V6 in construction and are not worked out here.

---

## S13. Implementation steps

Each step is one pull request against `release/2.0.0`. New code lives in `src/security/`. Tests are native googletest unless stated.

| Step | Content | Files | Tests | Compatibility |
|---|---|---|---|---|
| 1a | AES block backends, CMAC, CTR, KDF, constant-time compare | new `src/security/aes128.hpp`, `aes128_mbedtls.cpp`, `aes128_portable.cpp`, `cmac.hpp/.cpp`, `ctr.hpp`, `kdf.hpp`, `src/CMakeLists.txt`, `library.json` | FIPS 197 block vector, RFC 4493 examples 1 to 4, SP 800-38A CTR vector, S12.1 keys. One ESP32 build per pinned core (6.5.0 and 8.0.0) | no behaviour change |
| 1b | Frame counters and counter store | new `frame_counter.hpp/.cpp`, `i_counter_store.hpp`, `memory_counter_store.hpp`, `nvs_counter_store.hpp/.cpp`, `counter_record_codec.cpp` | block ramp 1, 2, 4 up to B, boot cost `3n + 1`, no write on a boot without frames, failed save, ceiling, exhaustion, empty store refused, record CRC, epoch switch keeps ceiling and C_rec | no behaviour change |
| 2a | Type bits, codec for classes B, C1, C2, D with E = 0, receive state, join nonce | `message_type.hpp`, `base_header.cpp`, `base_message.hpp/.cpp` (SecurityContext), new `frame_codec.hpp/.cpp`, `replay_state.hpp/.cpp`, `i_key_provider.hpp`, `security_config.hpp`, the three `CreateForwarded` functions | vectors V1, V2, V4, V5a, V5b, V5c byte for byte, every bit of a frame flipped once must fail, beacon with a non-zero byte 11 refused, window rules with shifts of 31, 32 and 33, overheard rule, epoch selection, `Seal` refuses a forwarded `kPlain` body, H flag dropped | codec is not wired in, no behaviour change |
| 2b | Wiring, sizes, timing, API | `hardware_manager.cpp`, `radiolib_radio.cpp`, `mock_radio.cpp`, `radio_event.hpp`, `lora_mesh_protocol.cpp`, `network_service.cpp` (sites of S10.7, beacon ToA fix at 1550, join nonce table, `OpenBody` at delivery), `reliable_messaging.cpp`, `protocol_configuration.hpp`, `loramesher.hpp/.cpp`, `loramesher_error_codes.hpp`, `PROTOCOL_SPEC.md` | full mesh tests in secured mode, multi-hop sync test with 3 hops checking resync error under 25 ms, beacon size assertion, MTU rejection, routing fragments of 8 entries, replayed JOIN_RESPONSE rejected, response to the previous nonce accepted, first reservation saved at state entry, flood relay sends the received ciphertext bytes, saves only outside active slots, `kAuthFailureBurst`, plain node and secured node ignore each other, 2.0.0 golden frames unchanged in `kDisabled` | `kDisabled` is byte compatible with 2.0.0. Turning on `kSecured` is the flag day for a deployment |
| 2c | Resume snapshot section | `resume_snapshot.hpp`, `resume_snapshot_codec.cpp` (version bump), `lora_mesh_protocol.cpp` (seal lock after capture) | resume keeps counters, ramp and all three windows, seal refused after capture, power loss falls back to the store, snapshot size at 35 peers | an old snapshot is rejected by its version and the node starts cold once |
| 3 | End-to-end layer, E = 1 | `frame_codec.cpp`, `replay_state.cpp`, `network_service.cpp` (send path asks `GetNodeKey`), `reliable_messaging.cpp` | vector V3 on both hops, relay cannot open, downgrade rule, ACK of a reliable message end to end, relay drops a replayed E = 1 DATA frame in window R, destination ignores window R, **finding 1 regression: a forged broadcast with source = gateway and the maximal counter does not block a later E = 1 frame of the gateway**, `SaveRxFloor` stride and interval | relays of step 2 forward E = 1 frames unchanged. Both ends of a pair must run step 3 and must be given the node key together |
| 4 | LoRaChat and tools | LoRaChat key provider, NVS layout, flashing tool (K_node derivation, counter record, address uniqueness, one provisioning per address and epoch), gateway key table with revocation | tool output equals S12.1, erased node refuses to start, second provisioning of an address in one epoch refused by the tool | flag day for LoRaChat deployments, existing 32-byte deployment key becomes K_dep or is replaced |
| 5a | Rekey and revocation | `frame_codec.cpp` (domain 0x05), new `rekey_manager.hpp/.cpp`, `network_service.cpp`, key provider `StoreNetworkKey` | vector V6, state machine of S9.2 with both exits of PENDING, boot table of S9.2, no frame ever sealed under a left epoch, timeout above grace / 4 refused, forwarding cap for key types, grace expiry, revoked node cut off, counters restart | compatible with step 3 nodes as relays. A node without step 5a cannot follow a rekey |
| 5b | Recovery | `frame_codec.cpp` (class R, domains 0x09, 0x0A), new `recovery_manager.hpp/.cpp`, `network_service.cpp` (ORPHAN state, rate limits), `protocol_state.hpp` | vector V7, orphan after one and after two missed rekeys, exit from ORPHAN on a verified frame, grant for an epoch above, equal to and below the record epoch (counters never restart in a used epoch), request timing for d = 0 to 5 in the slot simulator, every row of the abuse table of S9.5, rate limits, node without network key joins | nodes without step 5b drop 0x53 and 0x54 and cannot serve as recovery neighbours. They still relay 0x55 |
| 6 | Bench on boards | no library code, measurement report | beacon resync error over 3 hops, seal and open time per frame size with hardware AES, NVS write time and geometry, deep sleep resume with snapshot size, success rate of synchronised and unsynchronised KEY_REQUEST, brownout loop | none |

---

## S14. Choices made, open points, disputed points

### S14.1 Choices between two reasonable options

| # | Choice | Alternative | Reason in one line |
|---|---|---|---|
| 1 | AES-CTR plus truncated AES-CMAC, encrypt-then-MAC | AES-CCM for encryption and CMAC for the hop MIC | One block primitive on every core. Confirmed by the review. |
| 2 | Control frames are resealed by every transmitter with its own counter | Keep mutable fields outside the encrypted part | Relays already rebuild these frames, so resealing hides every field and can never reuse a nonce. |
| 3 | JOIN_REQUEST and JOIN_RESPONSE carry the transmitter address in the trailer (10 bytes) | Rewrite the BaseHeader source at each hop | The join logic depends on the original source, and 2 bytes on small frames cost nothing. |
| 4 | Data frames: 12 bytes, hop layer is a MIC without its own counter, H flag reserved | Per-hop counter and transmitter address now (18 bytes) | Owner decision 2. The 18-byte variant has a reserved place (S6.8). |
| 5 | Header mask on next_hop, ttl, seq_num | Leave the DataHeader extension readable | Costs one AES block and no bytes, and serves the owner's aim of unreadable routing information. |
| 6 | Beacon: 3-byte delay, 3-byte counter, 4-byte MIC | 3-byte MIC with the 2.0.0 body | Owner decision 3. The codec shortens the body, so the beacon classes and the pre-send patch stay untouched. |
| 7 | Epoch as one parity bit in type bit 3 | Epoch bits in the counter field or trial decryption | Zero bytes, authenticated with the header, same place in every frame. Cost: subtypes limited to 1..7. |
| 8 | E flag in bit 31 and H flag in bit 30 of the data counter field | A second type bit or new message types | No free type bit remains, and 30 counter bits are ample (S7.6). |
| 9 | Three traffic counter spaces plus C_rec | One counter for everything | The data window must count data frames, the beacon needs a short counter, recovery needs a counter that survives epochs. |
| 10 | Traffic counters restart at every epoch, traffic keys bound to the epoch | Counters that run forever | A rekey becomes the single cure for exhaustion. |
| 11 | Strict "greater than" for beacon and control, window of 32 for data | A window everywhere | Control frames travel one hop in order, data frames can overtake each other. |
| 12 | Receive bitmap starts all ones after cold boot and after resume | Start empty | Closes replay of the previous 31 frames, at the price of dropping a rare late frame. |
| 13 | Empty counter store stops the node | Start at zero or at a random value | Silent restart at zero would reuse nonces. |
| 14 | Reservation blocks ramp from 1 | Fixed blocks of 8 for the boot reservation (reviewer) | A boot costs at most three times what it used and nothing if it sent nothing, at the price of a few more writes after boot. |
| 15 | A provisioned address is never reused within an epoch | Generation ranges inside each counter space | The ranges cut the beacon space to an eighth (finding 4). |
| 16 | K_net derived from K_dep by label and epoch | Random K_net per epoch | The server can rebuild every key from one secret. A revoked node still cannot compute the next key. |
| 17 | KEY_UPDATE travels as a class D frame under epoch-bound keys with the ordinary data counter | Own counter and own frame class | No new relay code and no new replay state. |
| 18 | Nodes switch epoch on the first valid frame under the new epoch, or on a timer | An explicit activation message | No extra message, and nodes behind a relay that missed the update are not stranded. |
| 19 | Recovery request has no shared-key protection on the first link | A deployment-wide recovery key that never rotates | Such a key would be held by every stolen node for ever. Rate limits bound the cost. |
| 20 | Recovery reuses the neighbour's own end-to-end channel to the gateway (KEY_RELAY) | Route the orphan's frame itself through the mesh | The mesh then carries only frames with a valid hop MIC. |
| 21 | Join nonce is bound into the MIC of the response without being sent back | Echo the nonce in the response | Zero bytes on the response, which stays at 25 bytes in a safe subslot band (reviewer's proposal). |
| 22 | `payload_size` is the wire payload length, trailer length follows from the type | payload_size counts the trailer | Parsers after the codec stay unchanged. The beacon is the one type whose wire payload is shorter than the plain one. |
| 23 | After its switch the gateway sends nothing under the old epoch. Late nodes use recovery | Gateway retries under the old epoch with a counter kept in RAM (version 2) | One rule for all nodes: seal only under the transmit epoch. No path can reuse an old-epoch counter (N7). |
| 24 | Key types skip all hop windows and get a forwarding cap of 2 per superframe | A capped-jump window for key types | Any window driven by K_net can be pushed by a K_net holder, also in small steps. |
| 25 | Recovery request is timed into the discovery band from clear beacon headers | A timing hint frame under a shared key | Needs no K_net, no new frame and no long-lived shared key (reviewer's proposal, offset 14 added for deep networks). |
| 26 | Security modes are only `kDisabled` and `kSecured` | A hop-only mode without node keys | Whether a pair has a node key is already decided per peer by the provider. |

### S14.2 Not verified

1. No vector is unverified any more. The C++ code has still to reproduce them (steps 1 and 2).
2. The recovery timing of S9.5 is derived from `slot_scheduler.cpp` and from the subslot model. It was not simulated or run on a board. Open in particular: whether `depth` is at least 1 in a network of one node, whether a member with a deep-sleep policy keeps its DISCOVERY_RX slots, how often a member turns a discovery slot into DISCOVERY_TX, and the tolerance of the period estimate against clock drift. This also decides S8.2.
3. `RadioLibAES128::encryptECB` was read only as a declaration in RadioLib 7.1.0 (the copy in `.pio/libdeps`). Its padding behaviour for one 16-byte block, its thread safety and its presence under that name in 7.7.1 are unchecked. If it does not fit, step 1a writes a small portable AES.
4. `CONFIG_MBEDTLS_CMAC_C` is absent in Arduino 2.0.14 and present in the 3.3.x libs package found on this machine. The exact core that `espressif32@8.0.0` installs for the library's own `esp32` environment was not opened. The design does not depend on it.
5. CPU time of sealing and opening is an estimate from block counts (4 AES blocks for a beacon, about 16 for a relayed 115-byte data frame). Step 6 measures it.
6. The subslot band table of S10.6 is a model written from the code. It reproduces the band named in the decisions file. The 13 to 17 byte and 49 to 53 byte bands were not confirmed on a board or in a test. The reviewer did not verify the model either.
7. NVS page geometry, write duration and the wear figure of S7.6 rest on the review's assumptions.
8. The beacon and control frame rates per superframe (one beacon, one to two control frames) and the 77 s reference superframe are assumptions taken from the review.
9. The snapshot size (2,200 bytes typical) is taken from the decisions file. The 533 bytes added here were computed, the total was not measured.
10. The routing entry cap at SF10 to SF12 after the airtime clamp was not recomputed.
11. The header mask follows the QUIC header protection design (RFC 9001 section 5.4). The reviewer found no oracle beyond a 2^-32 forgery. No further review was done.
12. The "too soon" beacon filter near `network_service.cpp:3150` (S1.2 item 5) is cited from the review. I did not open that line.
13. `PingPongProtocol` was not analysed. The spec excludes it from secured mode.
14. Several gateways that share node keys work at the wire level (the origin address is in the nonce) but were not designed in detail. Recovery assumes one gateway address per node.

### S14.3 Disputed

Nothing is disputed in version 3.

The jump cap on the beacon counter (finding 2 of the first review) is closed: the second reviewer agreed that with a 32-bit MIC the beacon carries the same risk as ROUTE_TABLE. The plain strict rule of S7.4 rule 2 ships. The candidate form sketched in version 2 is withdrawn.

One deliberate deviation from a first-round fix is recorded in S14.1 choice 23: finding 5 asked for gateway retries under the old epoch during the grace period. Version 3 removes them in favour of the sealing rule of N7 and relies on recovery (S9.5) for late nodes.
