# LoRaMesher Protocol Specification

**Version**: 1.0  
**Last Updated**: 2025-06-30  
**Status**: Implementation Guide

## Table of Contents

1. [Overview](#overview)
2. [Protocol States](#protocol-states)
3. [Power-Aware Slot Allocation](#power-aware-slot-allocation)
4. [Network Topology Knowledge](#network-topology-knowledge)
5. [Multi-Hop Synchronization Strategy](#multi-hop-synchronization-strategy)
6. [Message Specifications](#message-specifications)
7. [State Transition Procedures](#state-transition-procedures)
8. [Implementation Requirements](#implementation-requirements)

## Overview

LoRaMesher implements a distance-vector routing protocol for LoRa mesh networks with power-aware TDMA slot allocation. The protocol emphasizes energy efficiency through intelligent sleep scheduling while maintaining reliable mesh communication.

### Design Principles

- **Power Efficiency**: 70-80% sleep time in normal operation
- **Scalable Architecture**: Efficient slot allocation for 2-50 nodes
- **Reliable Mesh**: Distance-vector routing with link quality metrics
- **State-Based Operation**: Clear state machine with defined transitions

## Protocol States

### State Machine Overview

```
INITIALIZING → DISCOVERY → JOINING → NORMAL_OPERATION
                    ↑          ↓
               FAULT_RECOVERY ← NETWORK_MANAGER
```

### State Definitions

#### INITIALIZING (0)
- **Purpose**: System startup and configuration
- **Duration**: Minimal (hardware initialization only)
- **Transitions**: Automatically to DISCOVERY

#### DISCOVERY (1)
- **Purpose**: Network detection or creation
- **Behavior**: Listen for existing networks, create if none found
- **Timeout**: Configurable discovery timeout (default: 30 seconds)

#### JOINING (2)
- **Purpose**: Connect to discovered network
- **Behavior**: Send join requests, wait for acceptance
- **Timeout**: Configurable join timeout (default: 10 seconds)

#### NORMAL_OPERATION (3)
- **Purpose**: Regular mesh node operation
- **Behavior**: Route messages, participate in network maintenance
- **Transitions**: To FAULT_RECOVERY on connection loss

#### NETWORK_MANAGER (4)
- **Purpose**: Network coordination and management
- **Behavior**: Coordinate slots, manage joins, route optimization
- **Election**: Automatic based on capabilities and network conditions

#### FAULT_RECOVERY (5)
- **Purpose**: Recover from network failures
- **Behavior**: Attempt reconnection, network healing
- **Timeout**: Configurable recovery timeout (default: 60 seconds)

## Power-Aware Slot Allocation

### Slot Types

| Slot Type | Purpose | Power State |
|-----------|---------|-------------|
| TX | Transmit data/control messages | Active (High Power) |
| RX | Receive data/control messages | Active (Medium Power) |
| SYNC_BEACON_TX | Transmit beacon synchronization | Active (High Power) |
| SYNC_BEACON_RX | Receive beacon synchronization | Active (Medium Power) |
| CONTROL_TX | Transmit network management | Active (High Power) |
| CONTROL_RX | Receive network management | Active (Medium Power) |
| DISCOVERY_TX | Transmit network beacons | Active (High Power) |
| DISCOVERY_RX | Listen for network activity | Active (Medium Power) |
| SLEEP | Radio power down | Sleep (Minimal Power) |

### Power-Optimized Slot Allocation Formula

For N-node network (1 manager + N-1 regular nodes):

```
Required Active Slots:
- Beacon Slots = 
- Control Slots = N (1 TX manager + N-1 RX nodes)
- Data Slots = N × data_slots_per_node
- Discovery Slots = min(5, max(2, ceil(N/3)))
- Total Active = Control + Data + Discovery

Power-Optimized Superframe:
- Target Duty Cycle = 30% (configurable)
- Superframe Size = ceil(Total Active / Target Duty Cycle)
- SLEEP Slots = Superframe Size - Total Active
- Actual Duty Cycle = Total Active / Superframe Size
```

### State-Specific Slot Configurations

#### DISCOVERY State
```
Slots: [DISCOVERY_RX × 10][SLEEP × 20]
Total: 30 slots
Duty Cycle: 33% (discovery-optimized)
Purpose: Wide network scanning with power efficiency
```

#### JOINING State  
```
Slots: [CONTROL_TX][CONTROL_RX][DISCOVERY_RX × 2][SLEEP × 26]
Total: 30 slots
Duty Cycle: 13% (minimal power during join process)
Purpose: Join communication while conserving power
```

#### NORMAL_OPERATION State
```
Active Slots: [Control × N][Data × N × config][Discovery × calculated]
SLEEP Slots: [SLEEP × (70-80% of superframe)]
Duty Cycle: 20-30% (application-dependent)
Purpose: Full mesh communication with maximum power efficiency
```

#### NETWORK_MANAGER State
```
Enhanced duty cycle: 35-40% (vs 30% for regular nodes)
Additional management slots for coordination
Purpose: Network management with reasonable power consumption
```

### Network Size Examples

#### 2-Node Network
- Active: 2 control + 2 data + 2 discovery = 6 slots
- Superframe: 20 slots (30% duty cycle)
- SLEEP: 14 slots (70% power saving)
- Power Profile: Each node awake ~6 slots per cycle

#### 4-Node Network
- Active: 4 control + 4 data + 3 discovery = 11 slots  
- Superframe: 37 slots (30% duty cycle)
- SLEEP: 26 slots (70% power saving)
- Power Profile: Each node awake ~5-6 slots per cycle

#### 8-Node Network
- Active: 8 control + 8 data + 4 discovery = 20 slots
- Superframe: 67 slots (30% duty cycle)
- SLEEP: 47 slots (70% power saving)
- Power Profile: Each node awake ~6-8 slots per cycle

## Network Topology Knowledge

### Knowledge Requirements by State

| State | Network Knowledge | Routing Table | Node List |
|-------|------------------|---------------|-----------|
| DISCOVERY | None | Empty | Empty |
| JOINING | Manager Address | Manager Only | Manager Only |
| NORMAL_OPERATION | Complete Topology | Full Routes | All Nodes |
| NETWORK_MANAGER | Complete + Management | Full + Metrics | All + Status |
| FAULT_RECOVERY | Partial/Cached | Partial | Partial |

### Progressive Knowledge Acquisition

1. **DISCOVERY**: Detect network manager through beacons
2. **JOINING**: Learn basic network structure from join response
3. **NORMAL_OPERATION**: Build complete topology through routing updates
4. **Maintenance**: Continuous topology updates and optimization

## Multi-Hop Synchronization Strategy

### The Multi-Hop Synchronization Challenge

In mesh networks, nodes beyond 1-hop distance from the Network Manager cannot directly receive synchronization beacons. A robust multi-hop forwarding strategy is essential for network-wide time synchronization while avoiding message collisions.

### Problem Statement

**Direct Sync Limitation:**
- Network Manager broadcasts sync beacon (reaches 1-hop neighbors only)
- Nodes at 2+ hops cannot synchronize directly
- Without forwarding: network fragments by communication range

**Collision Challenge:**
- Naive forwarding: all 1-hop neighbors rebroadcast simultaneously
- Result: massive collisions, poor sync beacon delivery
- LoRa collision probability approaches 80-90% with multiple simultaneous transmitters

### Hop-Layered Collision-Free Forwarding Solution

#### **Core Innovation: Sequential Hop Transmission**

**Hop-Layered Sync Beacon Slot Allocation:**

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                   SYNC BEACON SLOT ALLOCATION BY HOP COUNT                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  Slot    Purpose              NM (hop=0)  Node A (hop=1)  Node B (hop=2)   │
│  ────────────────────────────────────────────────────────────────────────   │
│   0   │ Original transmission    TX          RX            RX              │
│   1   │ 1-hop forwarding        SLEEP        TX            RX              │  
│   2   │ 2-hop forwarding        SLEEP       SLEEP          TX              │
│   3   │ 3-hop forwarding        SLEEP       SLEEP         SLEEP            │
│  ...  │ Additional hops          ...         ...           ...             │
│   N   │ Control/Data slots      CTRL         CTRL          CTRL            │
│                                                                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                            TRANSMISSION FLOW                               │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  Step 1: NM broadcasts original sync beacon in slot 0                      │
│          ┌──────┐   sync beacon   ┌──────┐                                 │
│          │  NM  ├─────────────────▶│ Hop1 │                                 │
│          │(hop0)│                 │      │                                 │
│          └──────┘                 └──────┘                                 │
│                                                                             │
│  Step 2: Hop-1 nodes forward in slot 1 (no collision with NM)              │
│          ┌──────┐                 ┌──────┐   sync beacon   ┌──────┐        │
│          │  NM  │                 │ Hop1 ├─────────────────▶│ Hop2 │        │
│          │(hop0)│                 │      │                 │      │        │
│          └──────┘                 └──────┘                 └──────┘        │
│                                                                             │
│  Step 3: Hop-2 nodes forward in slot 2 (collision-free progression)        │
│          ┌──────┐                 ┌──────┐                 ┌──────┐        │
│          │  NM  │                 │ Hop1 │                 │ Hop2 │        │
│          │(hop0)│                 │      │                 │      ├──────▶ │
│          └──────┘                 └──────┘                 └──────┘        │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Slot Assignment Algorithm:**
```cpp
// For each sync beacon slot (0 to max_hops-1)
for (hop_layer = 0; hop_layer < max_hops; hop_layer++) {
    if (hop_layer == 0) {
        // Slot 0: Network Manager original transmission  
        if (is_network_manager) {
            slot[0] = SYNC_BEACON_TX;  // NM sends original
        } else {
            slot[0] = SYNC_BEACON_RX;  // Others receive
        }
    } else {
        // Slots 1+: Hop-layered forwarding
        if (my_hop_distance == hop_layer) {
            slot[hop_layer] = SYNC_BEACON_TX;  // Forward from previous hop
        } else if (my_hop_distance == hop_layer + 1) {
            slot[hop_layer] = SYNC_BEACON_RX;  // Receive for next hop
        } else {
            slot[hop_layer] = SLEEP;  // Sleep if not relevant
        }
    }
}
```

**Example: 4-Node Linear Network**
```
Topology: NM ←→ A ←→ B ←→ C
Hops:    (0)   (1)  (2)  (3)

┌─────┬─────────┬─────────┬─────────┬─────────┬─────────┐
│Slot │ Purpose │   NM    │    A    │    B    │    C    │
├─────┼─────────┼─────────┼─────────┼─────────┼─────────┤
│  0  │ NM→A    │   TX    │   RX    │   RX    │   RX    │
│  1  │ A→B     │ SLEEP   │   TX    │   RX    │   RX    │  
│  2  │ B→C     │ SLEEP   │ SLEEP   │   TX    │   RX    │
│  3  │ C→...   │ SLEEP   │ SLEEP   │ SLEEP   │   TX    │
│  4  │ Control │ CTRL_TX │ CTRL_RX │ CTRL_TX │ CTRL_RX │
└─────┴─────────┴─────────┴─────────┴─────────┴─────────┘

Timeline:
T0: NM transmits sync beacon     → A,B,C receive
T1: A forwards received beacon   → B,C receive (no collision with NM)
T2: B forwards received beacon   → C receives (no collision with A)  
T3: C could forward if needed    → (network end, sleeps instead)
T4: Normal control operations begin...
```

**Example: Complex Mesh Network**
```
Topology:     D(2) ← B(1) ← NM(0) → A(1) → C(2)
                          ↓
                         E(1)
                          ↓  
                         F(2)

Node hop distances: NM=0, A=1, B=1, E=1, C=2, D=2, F=2

┌─────┬─────────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
│Slot │ Purpose │ NM  │  A  │  B  │  E  │  C  │  D  │  F  │
├─────┼─────────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤
│  0  │ NM→All  │ TX  │ RX  │ RX  │ RX  │ RX  │ RX  │ RX  │
│  1  │ Hop1→2  │SLEEP│ TX  │ TX  │ TX  │ RX  │ RX  │ RX  │  
│  2  │ Hop2→3  │SLEEP│SLEEP│SLEEP│SLEEP│ TX  │ TX  │ TX  │
│  3  │ Control │CTRL │CTRL │CTRL │CTRL │CTRL │CTRL │CTRL │
└─────┴─────────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘

Benefits of collision-free forwarding:
• Slot 1: A, B, E transmit simultaneously (same hop, LoRa capture effect)
• Slot 2: C, D, F transmit simultaneously (same hop, LoRa capture effect)  
• Zero cross-hop interference
• Guaranteed sync beacon propagation to network edge
```

**Collision Elimination:**
- **Zero inter-hop collisions**: Different hops transmit in different slots
- **Controlled same-hop collisions**: LoRa's capture effect handles simultaneous same-hop forwards
- **Predictable timing**: Each hop knows when to forward based on its distance from Network Manager

#### **Multi-Hop Sync Beacon Message Structure**

**SYNC_BEACON Message Type:** `0x46` (System message category)

```cpp
struct SyncBeaconHeader {
    // Standard message header
    AddressType destination = 0xFFFF;    // Broadcast to all nodes
    AddressType source;                  // Current transmitter address
    MessageType type = SYNC_BEACON;      // Message type 0x46
    uint8_t payload_size;                // Size of sync data
    
    // Core synchronization fields
    uint16_t network_id;                 // Network identifier
    uint32_t superframe_number;          // Incremental superframe counter
    uint16_t superframe_duration_ms;     // Total superframe time
    uint8_t total_slots;                 // Slots in complete superframe
    uint32_t slot_duration_ms;           // Individual slot duration
    
    // Multi-hop forwarding fields  
    AddressType original_source;         // Network Manager address
    uint8_t hop_count;                   // Hops from Network Manager
    uint32_t original_timestamp_ms;      // NM's original transmission time
    uint32_t propagation_delay_ms;       // Accumulated forwarding delay
    uint8_t max_hops;                    // Network diameter limit
    uint16_t sequence_number;            // Prevent forwarding loops
};
```

#### **Forwarding Logic and Timing Compensation**

**Hop Count-Based Forwarding Decision:**
```cpp
bool ShouldForwardSyncBeacon(const SyncBeaconMessage& beacon) {
    return (my_hop_count_from_nm_ == beacon.GetHopCount() + 1) &&
           (beacon.GetHopCount() < beacon.GetMaxHops()) &&
           (!IsSequenceAlreadyForwarded(beacon.GetSequenceNumber()));
}
```

**Timing Accuracy Preservation:**
- **Original Timestamp Retention**: All forwarded beacons carry Network Manager's original timestamp
- **Propagation Delay Accumulation**: Each forwarder adds transmission + processing delay
- **Receiver Compensation**: `original_timing = received_time - propagation_delay`
- **Target Accuracy**: ±50ms synchronization across entire network

**Forwarding Schedule Calculation:**
```cpp
uint8_t CalculateForwardingSlot(uint8_t my_hop_count) {
    // Slot 0: Network Manager (hop 0)
    // Slot 1: 1-hop neighbors (hop 1)  
    // Slot N: N-hop neighbors (hop N)
    return my_hop_count;
}
```

#### **Collision Mitigation for Same-Hop Forwarders**

**LoRa Parameter Diversity:**
- **Spreading Factor Rotation**: Different hops use SF7, SF8, SF9
- **Frequency Separation**: Each hop uses different frequency channels
- **Power Optimization**: Closer hops use lower power to reduce interference

**Temporal Collision Reduction:**
- **Random Jitter**: 0-50ms random delay for same-hop forwarders
- **Slot Subdivision**: Divide forwarding slots into sub-intervals
- **Capture Effect Utilization**: Stronger signals dominate in LoRa collisions

#### **Network Diameter and Scalability**

**Dynamic Hop Limit Calculation:**
```
max_hops = ceil(log2(network_nodes)) + safety_margin
sync_slots_needed = max_hops + 1
```

**Scalability Characteristics:**
- **2-4 node networks**: 2-3 sync slots (1-2 forwarding hops)
- **8-16 node networks**: 3-4 sync slots (2-3 forwarding hops)  
- **32+ node networks**: 4-5 sync slots (3-4 forwarding hops)
- **Power efficiency**: Sync overhead decreases as percentage with network growth

#### **Topology Awareness and Hop Discovery**

**Hop Count Learning:**
- Nodes discover their hop distance during network joining
- Hop count included in join response from Network Manager
- Used for sync forwarding slot calculation

**Dynamic Topology Updates:**
- Hop counts updated when network topology changes
- Routing updates carry hop distance information
- Automatic adaptation to network reconfigurations

### Synchronization State Machine Integration

#### **DISCOVERY State Sync Reception**
```
Slot Config: [SYNC_RX][DISCOVERY_RX × 9][SLEEP × 20]
Behavior: Listen for sync beacons in Slot 0-3, wide discovery in remaining slots
Goal: Detect network timing, hop structure, and Network Manager identity
```

#### **JOINING State Sync Alignment**  
```
Slot Config: Align with detected network timing
- Slots 0-N: Listen for sync beacons (learn network timing)
- Slot M: Send join request during NM's CONTROL_RX slot  
- Remaining: SLEEP for power efficiency
```

#### **NORMAL_OPERATION State Sync Maintenance**
```
Slot Config: Full network allocation with sync awareness
- Slot 0: Receive sync beacon (if not Network Manager)
- Slot 0: Send sync beacon (if Network Manager)  
- Slot X: Forward sync beacon (if intermediate hop)
- Remaining: Normal network operations
```

### Failure Recovery and Robustness

#### **Sync Loss Detection**
- **Missed Beacon Tolerance**: Allow 2-3 missed sync beacons before triggering recovery
- **Timing Drift Monitoring**: Detect excessive clock drift (>100ms)
- **Network Partition Detection**: Identify loss of Network Manager connectivity

#### **Recovery Procedures**
- **Automatic Re-sync**: Return to DISCOVERY state for timing re-acquisition
- **Backup Timing**: Use internal crystal oscillator during brief sync losses
- **Network Healing**: Re-establish sync forwarding paths after topology changes

#### **Robustness Features**
- **Loop Prevention**: Sequence numbers prevent infinite rebroadcast loops
- **Duplicate Detection**: Nodes track forwarded sequences to prevent duplicates
- **Emergency Fallback**: Manual sync procedures for extreme failure scenarios

### Performance Characteristics

#### **Synchronization Accuracy**
- **1-hop nodes**: ±10ms from Network Manager
- **2-hop nodes**: ±25ms from Network Manager  
- **3-hop nodes**: ±40ms from Network Manager
- **4+ hop nodes**: ±50ms from Network Manager

#### **Network Convergence Time**
- **Sync propagation**: 1 slot per hop (1-5 seconds typical)
- **Full network sync**: `max_hops * slot_duration` 
- **Re-sync after failure**: 2-3 superframe cycles

#### **Power and Bandwidth Overhead**
- **Sync beacon size**: 32-48 bytes per hop
- **Overhead percentage**: 2-5% of total network bandwidth
- **Power impact**: Minimal (sync slots already allocated for network management)

## Message Specifications

### Message Types

| Type | Category | Purpose | States Used |
|------|----------|---------|-------------|
| SYNC_BEACON | System | Multi-hop network synchronization | ALL_STATES |
| JOIN_REQUEST | System | Request network membership | JOINING |
| JOIN_RESPONSE | System | Accept/reject join request | NETWORK_MANAGER |
| ROUTING_UPDATE | Routing | Share route information | NORMAL_OPERATION, NETWORK_MANAGER |
| DATA_MESSAGE | Data | Application data transport | NORMAL_OPERATION |
| NETWORK_BEACON | Discovery | Network advertisement | DISCOVERY, NETWORK_MANAGER |

### Enhanced Join Response Format

```cpp
struct JoinResponseHeader {
    AddressType destination;        // Joining node address
    AddressType source;            // Network manager address  
    MessageType type;              // JOIN_RESPONSE
    uint8_t payload_size;          // Size of additional data
    
    // Join response specific fields
    uint16_t network_id;           // Network identifier
    uint8_t allocated_slots;       // Data slots for this node
    JoinResponseStatus status;     // ACCEPTED/REJECTED/etc
    
    // NEW: Power-aware slot information
    uint16_t superframe_size;      // Total slots in superframe
    uint16_t active_slots;         // Non-sleep slots
    uint8_t assigned_control_slot; // Node's control slot position
    uint8_t sleep_slot_duration;   // Duration of each sleep slot (ms)
};
```

**Note**: Checksums are not implemented as LoRa provides built-in error detection and correction.

## State Transition Procedures

### DISCOVERY → JOINING Transition

1. **Network Detection**: Receive network beacon in DISCOVERY_RX slot
2. **Slot Preparation**: Configure minimal JOINING slots (2 control + 2 discovery)
3. **Join Request**: Send JOIN_REQUEST in CONTROL_TX slot
4. **State Change**: Transition to JOINING state
5. **Power Optimization**: Enter sleep between join attempts

### JOINING → NORMAL_OPERATION Transition

1. **Join Response**: Receive JOIN_RESPONSE with network slot allocation
2. **Slot Calculation**: Parse superframe_size and active_slots from response
3. **Slot Preparation**: Configure full network slot allocation
4. **Synchronization**: Align with network superframe timing
5. **State Change**: Transition to NORMAL_OPERATION state
6. **Network Integration**: Begin full mesh participation

### Power-Efficient State Transitions

- **Minimize Active Time**: Use minimal slots during transitions
- **Preserve Synchronization**: Maintain superframe timing across state changes
- **Graceful Handover**: Complete ongoing communications before slot changes
- **Emergency Fallback**: Return to DISCOVERY if transitions fail

## Implementation Requirements

### Critical Functions to Implement

```cpp
// Power-aware slot allocation
SlotAllocation CalculatePowerAwareSlots(uint8_t network_size, 
                                       uint8_t data_slots_per_node,
                                       float target_duty_cycle = 0.3);

// State-specific slot configuration
Result SetJoiningSlots();
Result SetNormalOperationSlots(const SlotAllocation& target);
Result SetDiscoverySlots();

// Slot transition management
Result TransitionSlots(ProtocolState from_state, ProtocolState to_state);
bool ValidatePowerEfficiency(float min_sleep_ratio = 0.5);

// Power management integration
void EnterSleepSlot(uint16_t duration_ms);
void WakeFromSleep();
bool MaintainSynchronization();
```

### Configuration Parameters

```cpp
struct PowerConfig {
    float target_duty_cycle = 0.3;        // 30% active, 70% sleep
    uint8_t min_sleep_percentage = 50;     // Minimum 50% sleep time
    uint16_t max_superframe_slots = 100;   // Upper bound for superframe size
    uint8_t data_slots_per_node = 1;       // Default data slots per node
    bool battery_aware_scheduling = true;   // Adjust based on battery levels
};

struct TimingConfig {
    uint32_t discovery_timeout_ms = 30000;  // 30 second discovery
    uint32_t join_timeout_ms = 10000;       // 10 second join timeout
    uint32_t slot_duration_ms = 1000;       // 1 second per slot
    uint32_t superframe_sync_tolerance_ms = 100; // Sync tolerance
};
```

### Implementation Priorities

#### Phase 1: Critical Fixes (Immediate)
1. Fix `StartJoining()` slot allocation bug
2. Implement `SetJoiningSlots()` with minimal power
3. Add SLEEP slot calculation to existing functions

#### Phase 2: Power Management (Short Term)
1. Implement power-aware slot allocation formulas
2. Add enhanced join response with slot information
3. Create power configuration system

#### Phase 3: Advanced Features (Medium Term)
1. Battery-aware dynamic slot scheduling
2. Network-wide power optimization
3. Emergency wake-up protocols

### Testing Requirements

- **Power Consumption**: Measure actual duty cycles in different states
- **Battery Life**: Simulate network lifetime with various configurations  
- **Synchronization**: Verify timing accuracy after sleep periods
- **Scale Testing**: Validate power efficiency across 2-50 node networks
- **State Transitions**: Test smooth transitions without communication loss

### Success Metrics

- **Target**: 70%+ SLEEP time in normal operation
- **Joining**: <15% duty cycle during JOINING state  
- **Synchronization**: <100ms timing drift after sleep
- **Scalability**: Better power efficiency with larger networks
- **Reliability**: No communication loss during state transitions

---

**Implementation Status**: See [IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md) for current progress and specific TODOs.


### TODO LIST
- [ ] Commit the different changes in different commits. In advance for now, every feature/fix that passes test should be commited. You can use rollback at any time.
- [ ] Store max_hops received by the beacon
- [ ] Use max_hops to calculate the actual slots
- [ ] Check if the sync beacon message could be reduced in size, now is 27 bytes of size. (removing sequence_number, superframe_number, superframe_duration_ms, original_source) Check if some of the types could be reduced into smaller types. Correct me if I am wrong. 
- [ ] Modify all the functions that uses beacon messages with the previous changes.
- [ ] Correct and test the sync beacon message test.
- [ ] Test and check the comprehensive_slot_allocation_test
- [ ] Evaluate how the join_request and the join_response should be forwarded throught the network. Multiple devices would receive the message and we need to ensure that only one message is forwarded. (Use the next_hop when joining a network?)
- [ ] Evaluate how the superframe is being start when a sync beacon is received.
- [ ] We need to think how [Collision Mitigation for Same-Hop Forwarders](#collision-mitigation-for-same-hop-forwarders) would be implemented.
- [ ] I would like to test the collision mitigation in a seperate test to ensure this works, it is a really critical part of the code.
- [ ] Duty cycle configuration to ensure regulations (Optional)
- [ ] Separate network_service.cpp in different files. Think hard. Slot allocations inside the superframe_service, for example. Correct me if I am wrong.