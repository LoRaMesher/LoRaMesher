# Network Merge Feature Design Document

## Overview

This document describes the design for a network merge feature in LoRaMesher. When two independent mesh networks come into range of each other, they need a process to detect each other, decide which network is primary, and coordinate the merge.

---

## Problem Statement

**Scenario**: Two independent LoRa mesh networks (Network A and Network B) operate separately. At some point, nodes from these networks come within radio range of each other.

**Challenges**:
1. **Detection**: Each network has its own TDMA superframe timing - nodes may not be listening when foreign beacons are transmitted
2. **Decision**: Which network should survive (primary) and which should merge (secondary)?
3. **Coordination**: How do nodes from the secondary network join the primary without disrupting either network?

---

## Current Implementation Analysis

### Network Identity

Currently, the network ID is the Network Manager's address:

```cpp
// From network_service.cpp:2025
uint16_t network_id = node_address_;  // TODO: At this moment NetworkId is network Manager
```

This is stored in `SyncBeaconHeader::network_id_` and transmitted in every SYNC_BEACON.

**Key Files**:
- `src/types/messages/loramesher/sync_beacon_header.hpp` (lines 193-197)
- `src/protocols/lora_mesh/services/network_service.cpp` (line 2025)

### Protocol States

The protocol operates in 6 states defined in `i_network_service.hpp`:

```cpp
enum class ProtocolState {
    INITIALIZING,      // Protocol is initializing
    DISCOVERY,         // Looking for existing network
    JOINING,           // Attempting to join network
    NORMAL_OPERATION,  // Normal network operation
    NETWORK_MANAGER,   // Acting as network manager
    FAULT_RECOVERY     // Attempting to recover from fault
};
```

**Key File**: `src/protocols/lora_mesh/interfaces/i_network_service.hpp` (lines 28-35)

### Discovery Process

1. Node enters DISCOVERY state and listens for SYNC_BEACONs
2. If beacon received: extract NM address, transition to JOINING
3. If timeout: create new network, become Network Manager

```cpp
// From network_service.cpp:1914-1956
if (state_ == ProtocolState::DISCOVERY) {
    selected_sponsor_ = source;  // First sync beacon sender becomes sponsor
    network_id = sync_beacon.GetNetworkId();
    network_manager = sync_beacon.GetNetworkManager();
    Result join_result = StartJoining(network_manager, GetJoinTimeout());
}
```

**Key Files**:
- `src/protocols/lora_mesh/services/network_service.cpp` (lines 582-639, 1914-1956)
- `src/protocols/lora_mesh/services/network_service.hpp` (lines 243-251)

### Network Manager Role

The Network Manager is responsible for:
- Processing join requests (lines 902-1023)
- Slot allocation (lines 2240-2273)
- Sending original sync beacons (lines 1999-2097)
- Routing table management
- Network coordination

**Election**: First node to timeout during discovery becomes the Network Manager (no re-election mechanism).

### SYNC_BEACON Contents

```cpp
// From sync_beacon_header.hpp:193-202
uint16_t network_id_;           // Network identifier (currently = NM address)
uint8_t total_slots_;           // Number of slots in superframe
uint16_t slot_duration_ms_;     // Individual slot duration
AddressType network_manager_;   // Address of the network manager
uint8_t hop_count_;             // Hops from Network Manager
uint32_t propagation_delay_ms_; // Accumulated forwarding delay
uint8_t max_hops_;              // Network diameter limit (default: 5)
```

### Message Type System

Messages use a nibble encoding (high 4 bits = category, low 4 bits = subtype):

```cpp
// From message_type.hpp
SYSTEM_MSG = 0x40,       // 0100 xxxx: System message category
SYNC = 0x41,
JOIN_REQUEST = 0x42,
JOIN_RESPONSE = 0x43,
SLOT_REQUEST = 0x44,
SLOT_ALLOCATION = 0x45,
SYNC_BEACON = 0x46,      // Highest current system message
```

New merge messages would use 0x47-0x4B.

---

## The Gap: No Foreign Network Handling

Currently, if a node in NORMAL_OPERATION or NETWORK_MANAGER state receives a SYNC_BEACON from a different network, there is **no handling for this case**. The beacon would either be ignored or cause confusion.

---

## Proposed Design

### Design Decisions

Based on user requirements:

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Priority criteria | Lower Network Manager ID | Deterministic - both sides calculate same result |
| Detection trigger | Single confirmed foreign beacon | TDMA makes detection rare, so act when it happens |
| Control model | Hybrid: auto detection, NM-coordinated merge | Balance between simplicity and control |

### Phase 1: Foreign Network Detection

#### The TDMA Challenge

Each network has its own superframe timing:
- Node A might be in TX slot while Node B sends a beacon
- Probability of overlap depends on slot duration alignment

#### Solution: Periodic Foreign Discovery Windows

All nodes periodically enter a "foreign network discovery window" where they listen outside their normal TDMA schedule.

**New Slot Type**:
```cpp
enum class SlotType : uint8_t {
    // ... existing types ...
    FOREIGN_DISCOVERY_RX = 0x0A,  // Foreign network discovery reception
};
```

**Configuration Parameters**:
```cpp
uint32_t foreign_discovery_interval_ms = 300000;  // 5 minutes between windows
uint32_t foreign_discovery_duration_ms = 30000;   // 30 second window
```

#### Foreign Beacon Detection Logic

A beacon is "foreign" when:
```cpp
bool IsForeignBeacon(const SyncBeaconMessage& beacon) const {
    uint16_t our_network_id = network_manager_;
    uint16_t beacon_network_id = beacon.GetNetworkId();
    return beacon_network_id != our_network_id && beacon_network_id != 0;
}
```

#### Edge Node Reporting

When an edge node detects a foreign beacon, it reports to its Network Manager.

**New Message: FOREIGN_NETWORK_DETECTED (0x47)**
```cpp
struct ForeignNetworkDetectedHeader : public BaseHeader {
    uint16_t foreign_network_id_;      // Network ID of detected network
    AddressType foreign_nm_address_;   // Foreign NM address
    uint8_t foreign_hop_count_;        // Hop count from foreign beacon
    int8_t rssi_;                      // Signal strength
    uint32_t detection_timestamp_;     // When detected
};
```

### Phase 2: Merge Negotiation

#### NM-to-NM Communication

The two Network Managers are in separate networks with different TDMA timing. Direct communication requires using a "bridge node" - the node that detected the foreign network.

#### Negotiation Messages

**NETWORK_MERGE_PROBE (0x48)**: Initial probe to foreign network
```cpp
struct NetworkMergeProbeHeader : public BaseHeader {
    uint16_t source_network_id_;
    AddressType source_nm_address_;
    uint16_t target_network_id_;
    uint8_t network_size_;           // Number of nodes
    uint8_t network_max_hops_;
    uint32_t network_uptime_;
    uint8_t probe_sequence_;
};
```

**NETWORK_MERGE_REQUEST (0x49)**: Formal request from secondary to primary
```cpp
struct NetworkMergeRequestHeader : public BaseHeader {
    uint16_t source_network_id_;
    AddressType source_nm_address_;
    uint16_t target_network_id_;
    AddressType target_nm_address_;
    uint8_t merge_request_id_;
    uint8_t total_nodes_to_merge_;
    uint8_t total_slots_needed_;
    uint8_t capabilities_;
};
```

**NETWORK_MERGE_RESPONSE (0x4A)**: Response from primary NM
```cpp
struct NetworkMergeResponseHeader : public BaseHeader {
    uint16_t source_network_id_;
    uint16_t target_network_id_;
    uint8_t merge_request_id_;
    MergeResponseStatus status_;
    uint8_t migration_delay_superframes_;
    uint16_t new_slot_allocation_base_;
};

enum class MergeResponseStatus : uint8_t {
    ACCEPTED = 0x00,
    REJECTED_CAPACITY = 0x01,
    REJECTED_TIMING = 0x02,
    RETRY_LATER = 0x03,
};
```

**NETWORK_MERGE_ANNOUNCEMENT (0x4B)**: Broadcast from secondary NM to its nodes
```cpp
struct NetworkMergeAnnouncementHeader : public BaseHeader {
    uint16_t old_network_id_;
    uint16_t new_network_id_;
    AddressType new_nm_address_;
    uint8_t migration_countdown_;
    uint8_t slot_offset_;
};
```

#### Priority Decision

```cpp
bool IsPrimaryNetwork(uint16_t foreign_network_id) const {
    // Lower ID has priority - becomes/stays primary
    return network_manager_ < foreign_network_id;
}
```

Both Network Managers calculate the same result - no negotiation needed for this decision.

### Phase 3: Merge Execution

#### New Protocol State

```cpp
enum class ProtocolState {
    // ... existing states ...
    NETWORK_MERGING,   // Migrating to another network
};
```

#### State Transitions

```
NORMAL_OPERATION ──[MERGE_ANNOUNCEMENT]──> NETWORK_MERGING
                                                 │
                                                 │ [migration slot]
                                                 v
                                              JOINING
                                                 │
                                                 │ [JOIN_RESPONSE ACCEPTED]
                                                 v
                                          NORMAL_OPERATION
```

#### Migration Process

1. **Announcement Phase** (2-3 superframes)
   - Secondary NM broadcasts NETWORK_MERGE_ANNOUNCEMENT
   - Repeated for reliability

2. **Timing Alignment Phase** (1-2 superframes)
   - Secondary nodes listen in foreign discovery windows
   - Receive primary network sync beacons to learn timing

3. **Sequential Migration** (N superframes)
   - Nodes migrate one at a time to avoid collision
   - Order: Bridge node first, then by hop count from bridge, then by address

4. **Individual Node Migration**
   - Enter NETWORK_MERGING state
   - Listen for primary network sync beacons
   - At migration slot, send JOIN_REQUEST to primary NM
   - On JOIN_RESPONSE(ACCEPTED), transition to NORMAL_OPERATION

#### Slot Allocation

The primary NM pre-allocates a slot range for merging nodes:
- `new_slot_allocation_base_` in MERGE_RESPONSE specifies starting slot
- Secondary NM assigns specific offsets to each node in ANNOUNCEMENT
- Prevents slot conflicts during migration

### Phase 4: Edge Cases

#### Networks Meeting and Separating Repeatedly

**Problem**: Mobile networks may repeatedly come in and out of range.

**Solution**: Merge cooldown and attempt limits
```cpp
struct MergeState {
    uint16_t last_merge_target_id_;
    uint32_t last_merge_attempt_time_;
    uint8_t merge_attempt_count_;

    static constexpr uint32_t MERGE_COOLDOWN_MS = 300000;  // 5 minutes
    static constexpr uint8_t MAX_MERGE_ATTEMPTS = 3;
};
```

#### Merge Failure Handling

| Failure Type | Recovery Action |
|--------------|-----------------|
| REJECTED (capacity) | Continue in current network, enter cooldown |
| TIMEOUT during migration | Enter FAULT_RECOVERY, attempt rejoin |
| Communication loss | Node enters FAULT_RECOVERY |
| Partial merge | Failed nodes enter FAULT_RECOVERY |

#### Ongoing Data Transmissions

**Strategy**: Buffer and forward
- During NETWORK_MERGING: buffer outgoing data
- After successful migration: forward through new network
- On failure: drop after timeout

#### Interior Nodes (Cannot Hear Other Network)

**Problem**: Nodes far from the edge cannot directly hear the primary network.

**Solution**: Bridge node as sponsor
- Bridge node relays sync beacon timing information
- Interior nodes use relayed timing to synchronize
- JOIN_REQUEST routed through bridge node

---

## Sequence Diagram

```
Network A (NM=0x0010)          Bridge Node           Network B (NM=0x0020)
        │                          │                         │
        │                          │◄──SYNC_BEACON───────────│
        │                          │   [foreign detected]    │
        │◄──FOREIGN_NETWORK_DETECTED│                        │
        │                          │                         │
        │  [A is primary: 0x0010 < 0x0020]                   │
        │                          │                         │
        │──NETWORK_MERGE_PROBE────►│                         │
        │                          │──NETWORK_MERGE_PROBE───►│
        │                          │                         │
        │                          │  [B recognizes A as primary]
        │                          │                         │
        │                          │◄──NETWORK_MERGE_REQUEST─│
        │◄──NETWORK_MERGE_REQUEST──│                         │
        │                          │                         │
        │  [evaluate capacity]     │                         │
        │  [accept merge]          │                         │
        │                          │                         │
        │──NETWORK_MERGE_RESPONSE─►│                         │
        │  (ACCEPTED, slots 30-35) │──NETWORK_MERGE_RESPONSE►│
        │                          │                         │
        │                          │◄──MERGE_ANNOUNCEMENT────│
        │                          │   (broadcast to B nodes)│
        │                          │                         │
        │                          │   [nodes enter MERGING] │
        │                          │   [sequential migration]│
        │                          │                         │
        │◄──JOIN_REQUEST───────────│                         │
        │  (bridge node first)     │                         │
        │──JOIN_RESPONSE──────────►│                         │
        │  (ACCEPTED, slot 30)     │                         │
        │                          │                         │
        │◄──JOIN_REQUEST───────────│◄────────────────────────│
        │  (next node)             │   (other B nodes)       │
        │──JOIN_RESPONSE──────────►│────────────────────────►│
        │                          │                         │
        │  [merge complete]        │                         │
        │  [all nodes in Network A]│                         │
```

---

## Configuration Parameters

```cpp
struct MergeConfig {
    // Detection
    uint32_t foreign_discovery_interval_ms = 300000;  // 5 minutes
    uint32_t foreign_discovery_duration_ms = 30000;   // 30 seconds

    // Negotiation
    uint32_t merge_probe_timeout_ms = 10000;
    uint32_t merge_request_timeout_ms = 30000;
    uint8_t merge_request_retries = 3;

    // Execution
    uint8_t migration_delay_superframes = 5;
    uint32_t migration_timeout_ms = 120000;  // 2 minutes per node

    // Failure handling
    uint32_t merge_cooldown_ms = 300000;  // 5 minutes
    uint8_t max_merge_attempts = 3;

    // Capacity
    uint8_t max_merge_nodes = 20;  // Max nodes to accept in one merge
};
```

---

## Files Summary

### Files to Modify

| File | Changes |
|------|---------|
| `src/types/messages/message_type.hpp` | Add message types 0x47-0x4B |
| `src/protocols/lora_mesh/interfaces/i_network_service.hpp` | Add NETWORK_MERGING state |
| `src/protocols/lora_mesh/services/network_service.hpp` | Add merge state variables and methods |
| `src/protocols/lora_mesh/services/network_service.cpp` | Implement foreign detection, merge logic |
| `src/types/protocols/lora_mesh/slot_allocation.hpp` | Add FOREIGN_DISCOVERY_RX slot type |

### New Files to Create

| File | Purpose |
|------|---------|
| `src/types/messages/loramesher/network_merge_headers.hpp` | Merge message header classes |
| `src/types/messages/loramesher/network_merge_messages.hpp` | Merge message wrapper classes |
| `test/protocols/lora_mesh/services/test_network_merge/` | Test directory |

---

## Test Scenarios

### Basic Merge Test
```
Setup: Network A (nodes 0x0010, 0x0011, 0x0012)
       Network B (nodes 0x0020, 0x0021, 0x0022)

1. Networks operate independently
2. Node 0x0012 moves into range of 0x0022
3. Foreign beacon detected
4. Merge negotiation completes
5. Network B nodes migrate to Network A
6. Verify: All 6 nodes in single network with NM 0x0010
```

### Merge Rejection Test
```
Setup: Network A (3 nodes), Network B (50 nodes)

1. A detects B
2. A sends merge probe
3. B sends merge request
4. A rejects (capacity: 50 > max_merge_nodes)
5. Both networks continue independently
6. Verify: Both networks intact, merge cooldown active
```

### Partial Merge Failure
```
Setup: Two networks, one node in B loses connectivity mid-merge

1. Merge initiated
2. 2 of 3 nodes from B successfully migrate
3. Third node times out
4. Third node enters FAULT_RECOVERY
5. Verify: Third node eventually discovers and joins merged network
```

### Repeated Proximity Test
```
Setup: Two mobile networks

1. Networks detect each other
2. Merge initiated but networks separate (out of range)
3. Merge times out
4. Cooldown prevents immediate re-merge attempt
5. After cooldown, networks meet again
6. Successful merge on second attempt
```

---

## Open Questions / Future Considerations

1. **Network ID**: Currently network ID = NM address. Should this be a separate identifier that survives NM changes?

2. **NM Handover**: What happens if the primary NM fails after merge? Is there NM election?

3. **Partial Visibility**: What if only some nodes from each network can see each other?

4. **Priority Alternatives**: Lower NM ID is simple but arbitrary. Consider:
   - Network size (larger wins)
   - Network age (older wins)
   - Configurable priority value

5. **Security**: Should merge requests be authenticated? Could a malicious network force others to merge?

---

## Future Work: NM Listening During SLEEP Slots

### Problem

Both NMs in a merge scenario share `kMinSlots = 16`, giving identical superframe periods.
The phase offset between them is constant (zero drift). If NM_B's `SYNC_BEACON_TX` (slot 0) falls
during NM_A's SLEEP slots (6 out of 16 — slots 6-11 where the radio is off), the merge can never
happen regardless of timeout.

The current test workaround seeds the PRNG and uses calculated phase alignment to guarantee that
NM_B's beacon lands in NM_A's DISCOVERY_RX range. This makes tests deterministic but does not
fix the underlying protocol limitation: in a real deployment, two independently started NMs with
unlucky phase alignment will fail to merge.

### Proposed Solution

During SLEEP slots, if the node is a NETWORK_MANAGER, keep the radio in `kReceive` instead of
`kSleep`. This makes the NM always listening, ensuring foreign beacons are received regardless
of TDMA phase alignment.

Location: `src/protocols/lora_mesh_protocol.cpp`, `ProcessSlotMessages()`, `SLEEP` case handler.

### Considerations

- **Power consumption**: NMs would not be able to light-sleep their radio during SLEEP slots.
  This is acceptable for wall-powered NMs but may not be desirable for battery-powered ones.
  A possible compromise: listen during only a subset of SLEEP slots (e.g., 1 out of every N),
  or make this behavior configurable.
- **`prepare_sleep_callback_`**: If the NM stays in RX during SLEEP, the sleep callback should
  not fire (no MCU light sleep). The wake-up callback should also not fire spuriously.
- **`in_subslotted_slot_`**: Should be set to `true` so the radio stays in RX after processing
  a received message (otherwise `ProcessRadioEvents` sets it back to `kSleep`).
- **Alternative**: Distribute DISCOVERY_RX slots evenly across the superframe instead of
  clustering them at the end. This increases RX coverage without eliminating all sleep.

---

## References

- `src/protocols/lora_mesh/services/network_service.hpp` - Main network service
- `src/protocols/lora_mesh/services/network_service.cpp` - Implementation
- `src/types/messages/loramesher/sync_beacon_header.hpp` - Beacon format
- `src/protocols/lora_mesh/interfaces/i_network_service.hpp` - State definitions
- `src/types/messages/message_type.hpp` - Message type enumeration
