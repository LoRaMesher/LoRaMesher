# Unidirectional Link Detection

## Problem Statement

At marginal signal levels (SNR -11 to -14), radio links between LoRa nodes can become purely one-directional due to antenna orientation, physical obstructions, or hardware asymmetry. One direction works while the other is completely dead.

In the sf9_17Power_experiment with 15 nodes, node 0x3428 could receive routing table broadcasts from gateway 0x77A4 consistently (~every 39 seconds, RSSI 122-127), but 0x77A4's log contained zero received packets from 0x3428. Three nodes (0xDD58, 0x14A4, 0x3428) never appeared in MQTT monitoring because all their data was routed through this dead link.

## Root Cause

The library has the data to detect unidirectional links but three code locations prevented it from acting:

### Failure Chain

1. **Route change**: Node 0x3428 receives a routing table broadcast from 0x77A4, marking it as a direct neighbor (hop_count=1). This replaces the correct 2-hop route via=0x006C.

2. **Quality looks excellent**: Link quality to 0x77A4 calculates as 230/255 based on one-way EWMA reception. The library checks what 0x77A4 reports about 0x3428 via `GetLinkQualityFor(0x3428)`. In a mesh, 0x77A4 typically has a multi-hop route to 0x3428 (e.g., hop_count=2 via 0x006C, quality=251), so `GetLinkQualityFor()` returns 251 instead of 0, masking the unidirectional nature. Final quality: (230 + 251) / 2 = 240.

3. **Wrong path selected**: With quality 240 at 1 hop, the direct route is preferred over the working 2-hop route via 0x006C.

4. **TDMA slot mismatch**: Node 0x3428 allocates a DATA TX slot for sending to 0x77A4. But 0x77A4 doesn't know 0x3428 as a direct neighbor, so it allocates that slot as SLEEP (radio off).

5. **Data lost**: 0x3428 transmits on the DATA TX slot. Intermediate node 0x006C receives the packet but correctly ignores it (next_hop != its address). Gateway 0x77A4's radio is off. The data is lost.

6. **Repeats indefinitely**: All 27+ monitoring messages forwarded toward 0x77A4 via 0x3428 are lost.

### Bug Locations (Fixed)

**Bug 1 (fixed in commit 16a310e)**: `network_service.cpp` masked `GetLinkQualityFor()=0` by defaulting to 128.

**Bug 2 (fixed in commit 16a310e)**: `network_node_route.cpp:CalculateQuality()` had no penalty when `remote_link_quality == 0`.

**Bug 3 (fixed post-experiment)**: `routing_table_message.cpp:GetLinkQualityFor()` returned link_quality for ANY entry matching the destination, including multi-hop indirect routes. In a real mesh, the peer almost always has some multi-hop route to us via NM, so `remote_link_quality` was never 0 and the penalty from Bug 2's fix never triggered.

Fix: `GetLinkQualityFor()` (now renamed `GetReceptionQualityFor()`) filters by `hop_count == 1` and reads the dedicated `reception_quality` field. This field carries the sender's raw EWMA reception rate, not the combined bidirectional quality, eliminating a circular feedback loop where `CalculateQuality()` output was reflected back as `remote_link_quality`. The redundant `SetLinkQualityFor()` method was removed.

## Detection Algorithm

### Mechanism

When node A receives a routing table from node B, A checks whether B lists A as a **direct neighbor** (hop_count=1) via `GetReceptionQualityFor(A)`. The function only considers hop_count==1 entries and reads their `reception_quality` field (the sender's raw EWMA). Multi-hop entries are ignored because they prove indirect reachability, not direct radio contact. If A is not listed as a direct neighbor, `GetReceptionQualityFor()` returns 0, meaning "B doesn't hear A directly."

The detection adds a penalty after confirmation:

1. **Grace period**: For the first 2 routing table exchanges, `remote_link_quality=0` is tolerated. The peer hasn't had time to list us yet — bidirectional links typically get `remote_link_quality > 0` by superframe 2.

2. **Confirmation**: After `messages_expected >= 3` routing table exchanges where the peer still doesn't list us, the link is classified as confirmed unidirectional.

3. **Penalty**: Quality is reduced to `local_quality / 4`. With a typical window PDR of ~255, this produces quality ~63.

For bidirectional links with asymmetric PDR, quality uses a weighted bottleneck: `(min(local, remote) × 7 + avg(local, remote) × 3) / 10`. This penalizes asymmetric links without the cliff effect of pure `min()` that causes premature abandonment of marginal direct neighbors.

### Why Threshold 3

Bidirectional link timeline:
- Superframe N: A discovers B (messages_expected=0)
- Superframe N+1: A broadcasts routing table listing B. B receives it, adds A.
- Superframe N+2: B broadcasts routing table listing A with quality > 0. A receives it.

So `remote_link_quality > 0` by superframe 2 for bidirectional links. Threshold 3 provides margin for one dropped packet.

### Why /4

Route cost comparison (ETX-based: `cost = hop_count × 65536 / quality`):

| Route | Quality | Cost |
|-------|---------|------|
| 1-hop unidirectional, PDR=255, penalty=255/4=63 | 63 | 1×65536/63 = 1040 |
| 2-hop bidirectional relay, quality=200 | 200 | 2×65536/200 = 655 |
| 3-hop bidirectional relay, quality=200 | 200 | 3×65536/200 = 983 |

The 2-hop bidirectional path (cost=655) wins over the unidirectional 1-hop (cost=1040) by 37%. The `/8` divisor was too aggressive (cost=2114), preventing recovery from transient unidirectionality and creating an irreversible death spiral when combined with the inactivation → `reception_quality=0` → false UNIDIRECTIONAL cascade.

### Recovery

Recovery is automatic. Once the peer starts listing us (`remote_link_quality > 0`), the penalty branch is skipped entirely and quality returns to the weighted bottleneck formula. This handles transient unidirectionality (e.g., temporary interference) without permanent damage.

## Topology Example

```
Gateway(NM) <---> Relay <---> Edge
      |                         ^
      +--- unidirectional ------+
           (GW broadcasts reach Edge,
            Edge transmissions don't reach GW)
```

**Before fix**: Edge sees Gateway as 1-hop quality 179 → routes directly → data lost (TDMA mismatch).

**After fix**: After 3 superframes, Edge's quality to Gateway drops to ~57. The 2-hop route via Relay (quality ~90) becomes preferred. Data flows Edge→Relay→Gateway successfully.

## TDMA Impact

Without detection, the sending node allocates a DATA TX slot for a peer whose radio is off during that slot. The distributed TDMA algorithm requires both sides to agree on the neighbor relationship for slot alignment. A unidirectional link creates a one-sided neighbor relationship that breaks this assumption.

With detection, the route switches to the relay path where both hops have proper TDMA slot alignment (both sides recognize each other as direct neighbors).
