# LoRaMesher Protocol Specification

**Version**: 1.0  
**Last Updated**: 2025-06-30  
**Protocol Type**: Distance-Vector Mesh Routing with TDMA

This document provides the complete technical specification for the LoRaMesher protocol, a distance-vector routing protocol designed for LoRa mesh networks with TDMA coordination.

## Table of Contents

1. [Protocol Overview](#1-protocol-overview)
2. [State Machine Specification](#2-state-machine-specification)
3. [Message Format Specification](#3-message-format-specification)
4. [Routing Algorithm](#4-routing-algorithm)
5. [Network Synchronization (TDMA)](#5-network-synchronization-tdma)
6. [Network Discovery & Joining](#6-network-discovery--joining)
7. [Packet Structure](#7-packet-structure)
8. [Error Handling](#8-error-handling)
9. [Performance Characteristics](#9-performance-characteristics)

---

## 1. Protocol Overview

### 1.1 Design Principles

LoRaMesher is designed around these core principles:

- **Distance-Vector Routing**: Each node maintains routing tables with hop counts and link quality metrics
- **TDMA Coordination**: Time-division multiple access prevents collisions and enables network synchronization
- **Service-Oriented Architecture**: Protocol functionality is separated into distinct services
- **Hierarchical Network Management**: Special network manager nodes coordinate timing and topology
- **Adaptive Link Quality**: Multiple metrics determine optimal routing paths

### 1.2 Network Architecture

```mermaid
graph TB
    subgraph "Mesh Network"
        NM[Network Manager]
        N1[Node 1]
        N2[Node 2]
        N3[Node 3]
        N4[Node 4]
        N5[Node 5]
    end
    
    NM -.->|Sync/Coord| N1
    NM -.->|Sync/Coord| N2
    N1 <-->|Data/Route| N2
    N1 <-->|Data/Route| N3
    N2 <-->|Data/Route| N4
    N3 <-->|Data/Route| N4
    N4 <-->|Data/Route| N5
    N5 <-->|Data/Route| N3
    
    style NM fill:#ff9999
    style N1 fill:#99ccff
    style N2 fill:#99ccff
    style N3 fill:#99ccff
    style N4 fill:#99ccff
    style N5 fill:#99ccff
```

### 1.3 Protocol Stack

```mermaid
graph TB
    App[Application Layer]
    Msg[Message Layer]
    Route[Routing Service]
    Net[Network Service]
    Sync[Synchronization Service]
    Radio[Radio Hardware Layer]
    
    App --> Msg
    Msg --> Route
    Route --> Net
    Net --> Sync
    Sync --> Radio
    
    style App fill:#e1f5fe
    style Msg fill:#f3e5f5
    style Route fill:#e8f5e8
    style Net fill:#fff8e1
    style Sync fill:#fce4ec
    style Radio fill:#f1f8e9
```

---

## 2. State Machine Specification

### 2.1 Protocol States

The LoRaMesher protocol operates in six distinct states:

```mermaid
stateDiagram-v2
    [*] --> INITIALIZING
    INITIALIZING --> DISCOVERY : Hardware Ready
    DISCOVERY --> JOINING : Network Found
    DISCOVERY --> NETWORK_MANAGER : Create Network
    JOINING --> NORMAL_OPERATION : Join Success
    JOINING --> FAULT_RECOVERY : Join Timeout
    NORMAL_OPERATION --> NETWORK_MANAGER : Manager Election
    NORMAL_OPERATION --> FAULT_RECOVERY : Connection Lost
    NETWORK_MANAGER --> NORMAL_OPERATION : Role Change
    NETWORK_MANAGER --> FAULT_RECOVERY : Network Failure
    FAULT_RECOVERY --> DISCOVERY : Recovery Success
    FAULT_RECOVERY --> [*] : Recovery Failed
```

### 2.2 State Descriptions

#### INITIALIZING
- **Purpose**: System startup and hardware configuration
- **Duration**: 1-5 seconds
- **Activities**: 
  - Initialize radio hardware
  - Load configuration parameters
  - Start protocol services
- **Transitions**: 
  - Success → `DISCOVERY`
  - Failure → Protocol termination

#### DISCOVERY
- **Purpose**: Find existing networks or create new one
- **Duration**: 10-30 seconds (configurable)
- **Activities**:
  - Listen for network beacons
  - Broadcast discovery messages
  - Evaluate network options
- **Transitions**:
  - Network found → `JOINING`
  - Timeout with no networks → `NETWORK_MANAGER`
  - Critical error → `FAULT_RECOVERY`

#### JOINING
- **Purpose**: Connect to discovered network
- **Duration**: 5-15 seconds
- **Activities**:
  - Send join request to network manager
  - Negotiate slot assignment
  - Synchronize with network timing
- **Transitions**:
  - Join successful → `NORMAL_OPERATION`
  - Join failed/timeout → `FAULT_RECOVERY`

#### NORMAL_OPERATION
- **Purpose**: Standard data communication and routing
- **Duration**: Indefinite
- **Activities**:
  - Process and forward data messages
  - Maintain routing tables
  - Participate in network synchronization
- **Transitions**:
  - Elected as manager → `NETWORK_MANAGER`
  - Connection loss → `FAULT_RECOVERY`

#### NETWORK_MANAGER
- **Purpose**: Coordinate network timing and topology
- **Duration**: Indefinite
- **Activities**:
  - Broadcast synchronization beacons
  - Manage slot allocations
  - Handle join requests
  - Coordinate routing updates
- **Transitions**:
  - Role transferred → `NORMAL_OPERATION`
  - Network failure → `FAULT_RECOVERY`

#### FAULT_RECOVERY
- **Purpose**: Handle connection loss and network failures
- **Duration**: 30-60 seconds
- **Activities**:
  - Attempt to reconnect to known networks
  - Clear stale routing information
  - Reset synchronization state
- **Transitions**:
  - Recovery successful → `DISCOVERY`
  - Recovery failed → Protocol termination

### 2.3 State Transition Triggers

| From State | To State | Trigger | Condition |
|------------|----------|---------|-----------|
| INITIALIZING | DISCOVERY | Hardware Ready | Radio initialized successfully |
| DISCOVERY | JOINING | Network Found | Valid beacon received |
| DISCOVERY | NETWORK_MANAGER | Timeout | No networks found after timeout |
| JOINING | NORMAL_OPERATION | Join Success | JOIN_RESPONSE received |
| JOINING | FAULT_RECOVERY | Join Timeout | No response to JOIN_REQUEST |
| NORMAL_OPERATION | NETWORK_MANAGER | Manager Election | Current manager lost |
| NORMAL_OPERATION | FAULT_RECOVERY | Connection Lost | No valid messages for timeout period |
| NETWORK_MANAGER | NORMAL_OPERATION | Role Change | Another node elected manager |
| FAULT_RECOVERY | DISCOVERY | Recovery Success | Ready to rejoin network |

---

## 3. Message Format Specification

### 3.1 Message Type Organization

Messages are organized in a hierarchical 16×16 structure:

```cpp
// Message categories (upper 4 bits)
DATA_MESSAGE    = 0x10,  // Application data transmission
CONTROL_MESSAGE = 0x20,  // Protocol control and management
ROUTING_MESSAGE = 0x30,  // Routing table updates
SYSTEM_MESSAGE  = 0x40,  // System management and diagnostics
```

### 3.2 Core Message Types

#### 3.2.1 Join Messages
```cpp
// Join process messages
JOIN_REQUEST  = 0x21,  // Request to join network
JOIN_RESPONSE = 0x22,  // Response to join request
```

**JOIN_REQUEST Format**:
```cpp
struct JoinRequest {
    uint8_t messageType;     // 0x21
    uint16_t nodeId;         // Requesting node ID
    uint8_t capabilities;    // Node capability flags
    uint16_t networkId;      // Target network ID (0 for any)
    uint8_t requestedSlots;  // Number of slots requested
    uint8_t checksum;        // Message integrity check
};
```

**JOIN_RESPONSE Format**:
```cpp
struct JoinResponse {
    uint8_t messageType;     // 0x22
    uint16_t nodeId;         // Target node ID
    uint8_t status;          // JOIN_ACCEPTED(0) or JOIN_DENIED(1)
    uint16_t assignedSlot;   // Allocated slot number
    uint32_t superframeTime; // Current superframe timing
    uint16_t networkId;      // Network identification
    uint8_t checksum;        // Message integrity check
};
```

#### 3.2.2 Routing Messages
```cpp
// Routing protocol messages
ROUTING_UPDATE = 0x31,  // Distance-vector routing update
ROUTE_REQUEST  = 0x32,  // Route discovery request
ROUTE_RESPONSE = 0x33,  // Route discovery response
```

**ROUTING_UPDATE Format**:
```cpp
struct RoutingUpdate {
    uint8_t messageType;     // 0x31
    uint16_t originNode;     // Node sending update
    uint8_t numRoutes;       // Number of routes in update
    uint32_t superframeTime; // Network synchronization time
    
    struct RouteEntry {
        uint16_t destination; // Destination node ID
        uint8_t hops;        // Hop count to destination
        uint8_t linkQuality; // Link quality metric (0-255)
        uint16_t nextHop;    // Next hop node ID
        uint32_t timestamp;  // Route freshness timestamp
    } routes[MAX_ROUTES_PER_UPDATE];
    
    uint8_t checksum;        // Message integrity check
};
```

#### 3.2.3 Data Messages
```cpp
// Application data messages
DATA_UNICAST   = 0x11,  // Point-to-point data
DATA_BROADCAST = 0x12,  // Network-wide broadcast
DATA_MULTICAST = 0x13,  // Group communication
```

**DATA_UNICAST Format**:
```cpp
struct DataUnicast {
    uint8_t messageType;     // 0x11
    uint16_t sourceNode;     // Originating node
    uint16_t destinationNode;// Target node
    uint8_t ttl;            // Time-to-live (hop limit)
    uint16_t sequenceNumber; // Message sequence number
    uint8_t payloadLength;   // Length of application data
    uint8_t payload[MAX_PAYLOAD_SIZE]; // Application data
    uint8_t checksum;        // Message integrity check
};
```

### 3.3 Message Serialization

All messages use little-endian byte order and are serialized using the following process:

1. **Header Creation**: Message type and routing information
2. **Payload Serialization**: Application data with length prefix
3. **Checksum Calculation**: Simple XOR checksum of all bytes
4. **Size Validation**: Ensure total size ≤ 255 bytes (LoRa constraint)

---

## 4. Routing Algorithm

### 4.0 Routing Update Propagation Sequence

The following diagram shows how routing updates propagate through the network:

```mermaid
sequenceDiagram
    participant A as Node A
    participant B as Node B  
    participant C as Node C
    participant D as Node D
    
    Note over A,D: Initial network topology: A-B-C-D (linear)
    Note over A,D: Node E joins network at position B-E-C
    
    B->>B: Detect new neighbor E
    B->>*: Broadcast ROUTING_UPDATE (new route to E)
    
    Note over A,D: Routing update propagation
    A->>A: Process update from B
    A->>A: Update routing table: E via B (2 hops)
    A->>*: Broadcast updated routing table
    
    C->>C: Process update from B
    C->>C: Update routing table: E via B (2 hops)
    C->>*: Broadcast updated routing table
    
    D->>D: Process update from C
    D->>D: Update routing table: E via C (3 hops)
    D->>*: Broadcast updated routing table
    
    Note over A,D: Network converged with optimal routes to E
```

### 4.1 Data Message Multi-Hop Forwarding

This diagram illustrates how data messages are forwarded through multiple hops:

```mermaid
sequenceDiagram
    participant App as Application
    participant A as Node A
    participant B as Node B
    participant C as Node C
    participant D as Node D (Target)
    
    App->>A: Send data to Node D
    A->>A: Lookup route to D (via B, 3 hops)
    A->>A: Create DATA_UNICAST message
    A->>A: Set TTL=4, sequence number
    
    Note over A,B: Hop 1: A → B
    A->>B: DATA_UNICAST (src=A, dst=D, ttl=4)
    B->>B: Check destination (not for me)
    B->>B: Lookup next hop to D (via C)
    B->>B: Decrement TTL to 3
    
    Note over B,C: Hop 2: B → C  
    B->>C: DATA_UNICAST (src=A, dst=D, ttl=3)
    C->>C: Check destination (not for me)
    C->>C: Lookup next hop to D (direct)
    C->>C: Decrement TTL to 2
    
    Note over C,D: Hop 3: C → D
    C->>D: DATA_UNICAST (src=A, dst=D, ttl=2)
    D->>D: Check destination (for me!)
    D->>D: Forward to application layer
    
    Note over A,D: Optional: Acknowledgment path
    D->>C: ACK message (if requested)
    C->>B: Forward ACK
    B->>A: Forward ACK
    A->>App: Data delivered confirmation
```

---

## 4. Routing Algorithm

### 4.1 Distance-Vector Algorithm

LoRaMesher implements a modified Bellman-Ford distance-vector routing algorithm optimized for wireless mesh networks.

#### 4.1.1 Routing Table Structure

```cpp
struct RouteEntry {
    uint16_t destination;    // Destination node ID
    uint16_t nextHop;       // Next hop to reach destination
    uint8_t hops;           // Hop count (primary metric)
    uint8_t linkQuality;    // Link quality (secondary metric)
    uint32_t lastUpdate;    // Timestamp of last update
    uint8_t flags;          // Route flags (valid, temporary, etc.)
};
```

#### 4.1.2 Route Calculation

```mermaid
flowchart TB
    A[Receive Routing Update] --> B{Valid Update?}
    B -->|No| C[Discard]
    B -->|Yes| D[Extract Routes]
    D --> E{For Each Route}
    E --> F[Calculate New Metric]
    F --> G{Better than Current?}
    G -->|No| E
    G -->|Yes| H[Update Route Entry]
    H --> I[Set Update Flag]
    I --> E
    E --> J{More Routes?}
    J -->|Yes| E
    J -->|No| K{Updates Made?}
    K -->|Yes| L[Broadcast Update]
    K -->|No| M[Complete]
    L --> M
```

#### 4.1.3 Metric Calculation

**Primary Metric - Hop Count**:
```cpp
uint8_t newHops = receivedHops + 1;
if (newHops > MAX_HOPS) {
    // Route invalid due to hop limit
    return INVALID_ROUTE;
}
```

**Secondary Metric - Link Quality**:
```cpp
uint8_t combinedQuality = (receivedLinkQuality + localLinkQuality) / 2;
if (combinedQuality < MIN_QUALITY_THRESHOLD) {
    // Route below quality threshold
    return POOR_QUALITY_ROUTE;
}
```

**Route Comparison**:
```cpp
bool isBetterRoute(const RouteEntry& current, const RouteEntry& candidate) {
    // Primary comparison: hop count
    if (candidate.hops < current.hops) return true;
    if (candidate.hops > current.hops) return false;
    
    // Secondary comparison: link quality
    return candidate.linkQuality > current.linkQuality;
}
```

### 4.2 Loop Prevention

**Split Horizon**: Don't advertise routes back to the node that provided them
```cpp
void SendRoutingUpdate(uint16_t targetNode) {
    for (auto& route : routingTable) {
        if (route.nextHop != targetNode) {
            // Only advertise if not learned from target
            includeInUpdate(route);
        }
    }
}
```

**Route Poisoning**: Advertise unreachable routes with infinite metric
```cpp
void PoisonRoute(uint16_t destination) {
    RouteEntry poisonRoute;
    poisonRoute.destination = destination;
    poisonRoute.hops = INFINITE_HOPS;
    poisonRoute.linkQuality = 0;
    broadcastRoutingUpdate(poisonRoute);
}
```

### 4.3 Route Aging

Routes are aged out if not refreshed within the configured timeout:

```cpp
void AgeRoutes() {
    uint32_t currentTime = getCurrentTime();
    for (auto& route : routingTable) {
        if (currentTime - route.lastUpdate > ROUTE_TIMEOUT) {
            if (route.flags & ROUTE_PERMANENT) {
                continue; // Don't age permanent routes
            }
            removeRoute(route.destination);
            broadcastRoutePoison(route.destination);
        }
    }
}
```

### 4.5 Network Topology Examples and Routing Behavior

This section demonstrates how the routing algorithm performs in different network topologies.

#### 4.5.1 Linear Network Topology

```mermaid
graph LR
    A[Node A<br/>ID: 0x1001] --- B[Node B<br/>ID: 0x1002]
    B --- C[Node C<br/>ID: 0x1003]
    C --- D[Node D<br/>ID: 0x1004]
    D --- E[Node E<br/>ID: 0x1005]
    
    style A fill:#ff9999
    style E fill:#99ff99
```

**Routing Table at Node A:**
| Destination | Next Hop | Hops | Link Quality |
|-------------|----------|------|--------------|
| 0x1002      | 0x1002   | 1    | 220          |
| 0x1003      | 0x1002   | 2    | 180          |
| 0x1004      | 0x1002   | 3    | 150          |
| 0x1005      | 0x1002   | 4    | 120          |

**Data Flow A → E:** A → B → C → D → E (4 hops)

#### 4.5.2 Mesh Network Topology

```mermaid
graph TB
    A[Node A<br/>Manager] --- B[Node B]
    A --- C[Node C]
    B --- D[Node D]
    C --- D
    B --- E[Node E]
    C --- F[Node F]
    D --- F
    E --- F
    
    style A fill:#ff9999
    style B fill:#99ccff
    style C fill:#99ccff
    style D fill:#99ccff
    style E fill:#99ccff
    style F fill:#99ccff
```

**Routing Table at Node A (Multiple Path Options):**
| Destination | Primary Route | Backup Route | Hops | Quality |
|-------------|---------------|--------------|------|---------|
| 0x1002      | Direct        | via C        | 1    | 240     |
| 0x1003      | Direct        | via B        | 1    | 230     |
| 0x1004      | via B         | via C        | 2    | 200     |
| 0x1005      | via B         | via C→D      | 2    | 190     |
| 0x1006      | via C         | via B→D      | 2    | 200     |

**Optimal Data Paths from A:**
- A → D: A → B → D (2 hops) or A → C → D (2 hops)
- A → E: A → B → E (2 hops, preferred due to higher link quality)
- A → F: A → C → F (2 hops, preferred due to higher link quality)

#### 4.5.3 Star Network Topology

```mermaid
graph TB
    M[Manager<br/>ID: 0x1000] --- A[Node A<br/>ID: 0x1001]
    M --- B[Node B<br/>ID: 0x1002]
    M --- C[Node C<br/>ID: 0x1003]
    M --- D[Node D<br/>ID: 0x1004]
    M --- E[Node E<br/>ID: 0x1005]
    
    style M fill:#ff9999
    style A fill:#99ccff
    style B fill:#99ccff
    style C fill:#99ccff
    style D fill:#99ccff
    style E fill:#99ccff
```

**Routing Table at Node A:**
| Destination | Next Hop | Hops | Link Quality |
|-------------|----------|------|--------------|
| 0x1000      | 0x1000   | 1    | 250          |
| 0x1002      | 0x1000   | 2    | 200          |
| 0x1003      | 0x1000   | 2    | 195          |
| 0x1004      | 0x1000   | 2    | 205          |
| 0x1005      | 0x1000   | 2    | 190          |

**Characteristics:**
- Single point of failure (Manager)
- All inter-node communication requires 2 hops
- Optimal for centralized control scenarios
- Manager handles all routing decisions

#### 4.5.4 Partitioned Network Recovery

This example shows how the network handles and recovers from partitions:

**Before Partition:**
```mermaid
graph LR
    subgraph "Full Network"
        A[Node A] --- B[Node B]
        B --- C[Node C]
        C --- D[Node D]
        D --- E[Node E]
        E --- F[Node F]
    end
```

**During Partition (Link C-D Failed):**
```mermaid
graph LR
    subgraph "Partition 1"
        A[Node A] --- B[Node B]
        B --- C[Node C]
    end
    
    subgraph "Partition 2"  
        D[Node D] --- E[Node E]
        E --- F[Node F]
    end
    
    style A fill:#ff9999
    style D fill:#99ff99
```

**After Healing (Alternative Path A-G-F Discovered):**
```mermaid
graph TB
    subgraph "Healed Network"
        A[Node A] --- B[Node B]
        B --- C[Node C]
        A --- G[Node G<br/>New]
        G --- F[Node F]
        F --- E[Node E]
        E --- D[Node D]
    end
    
    style G fill:#ffff99
```

**Route Changes During Healing:**
| Phase | A→D Route | Hops | Status |
|-------|-----------|------|--------|
| Normal | A→B→C→D | 3 | Active |
| Partition | No route | ∞ | Failed |
| Healing | A→G→F→E→D | 4 | Restored |

#### 4.5.5 Load Balancing Example

In networks with multiple equal-cost paths, the protocol can distribute traffic:

```mermaid
graph TB
    A[Source A] --- B[Node B]
    A --- C[Node C]
    B --- D[Destination D]
    C --- D
    
    B -.->|Path 1<br/>Load: 60%| D
    C -.->|Path 2<br/>Load: 40%| D
```

**Load Distribution Logic:**
```cpp
struct LoadBalancing {
    struct PathMetrics {
        uint8_t hops;
        uint8_t linkQuality;
        uint8_t currentLoad;    // 0-100%
        uint32_t lastUsed;
    };
    
    uint16_t selectNextHop(uint16_t destination) {
        auto equalCostPaths = findEqualCostPaths(destination);
        
        // Weighted selection based on current load
        uint16_t selectedPath = 0;
        uint8_t minLoad = 255;
        
        for (auto& path : equalCostPaths) {
            if (path.currentLoad < minLoad) {
                minLoad = path.currentLoad;
                selectedPath = path.nextHop;
            }
        }
        
        return selectedPath;
    }
};
```

---

## 5. Network Synchronization (TDMA)

### 5.1 Superframe Structure

The TDMA system organizes time into superframes divided into slots:

```mermaid
gantt
    title Superframe Structure (Example: 8 slots)
    dateFormat X
    axisFormat %s
    
    section Superframe
    Slot 0 (Manager)    :0, 1000
    Slot 1 (Node A)     :1000, 2000
    Slot 2 (Node B)     :2000, 3000
    Slot 3 (Node C)     :3000, 4000
    Slot 4 (Node D)     :4000, 5000
    Slot 5 (Node E)     :5000, 6000
    Slot 6 (Open)       :6000, 7000
    Slot 7 (Open)       :7000, 8000
```

### 5.2 Timing Parameters

```cpp
struct SuperframeConfig {
    uint16_t slotDuration;      // Duration of each slot (ms)
    uint8_t slotsPerSuperframe; // Number of slots in superframe
    uint32_t superframeDuration; // Total superframe duration (ms)
    uint8_t guardTime;          // Guard time between slots (ms)
    uint32_t syncTolerance;     // Acceptable sync drift (ms)
};

// Default configuration
SuperframeConfig defaultConfig = {
    .slotDuration = 1000,       // 1 second per slot
    .slotsPerSuperframe = 8,    // 8 slots total
    .superframeDuration = 8000, // 8 second superframe
    .guardTime = 50,            // 50ms guard time
    .syncTolerance = 100        // 100ms sync tolerance
};
```

### 5.3 Synchronization Protocol

#### 5.3.1 Network Manager Synchronization

The network manager broadcasts synchronization information in slot 0:

```cpp
struct SyncMessage {
    uint8_t messageType;        // SYNC_BEACON (0x41)
    uint16_t managerId;         // Network manager node ID
    uint32_t superframeTime;    // Current superframe timestamp
    uint16_t superframeNumber;  // Superframe sequence number
    uint8_t activeSlots;        // Bitmap of active slots
    uint8_t checksum;           // Message integrity
};
```

#### 5.3.2 Time Synchronization Algorithm

```mermaid
sequenceDiagram
    participant M as Network Manager
    participant N as Node
    
    M->>N: SYNC_BEACON (superframe_time)
    Note over N: Calculate time offset
    N->>N: Adjust local clock
    N->>M: Optional: SYNC_ACK
    
    loop Every Superframe
        M->>N: SYNC_BEACON
        N->>N: Drift correction
    end
```

**Clock Adjustment Logic**:
```cpp
void ProcessSyncBeacon(const SyncMessage& sync) {
    uint32_t receivedTime = getCurrentLocalTime();
    uint32_t expectedTime = sync.superframeTime;
    int32_t drift = receivedTime - expectedTime;
    
    if (abs(drift) > syncTolerance) {
        // Large drift - hard sync
        setLocalTime(expectedTime);
        logEvent("Hard sync: drift = " + std::to_string(drift));
    } else if (abs(drift) > syncTolerance / 2) {
        // Small drift - gradual adjustment
        adjustClockRate(drift);
        logEvent("Soft sync: drift = " + std::to_string(drift));
    }
    
    lastSyncTime = receivedTime;
}
```

### 5.4 Slot Allocation

#### 5.4.1 Initial Allocation

New nodes request slots during the join process:

1. **Slot Request**: Include desired number of slots in JOIN_REQUEST
2. **Slot Assignment**: Network manager assigns available slots
3. **Slot Confirmation**: Node confirms slot usage in first transmission

#### 5.4.2 Dynamic Reallocation

```cpp
struct SlotRequest {
    uint8_t messageType;     // SLOT_REQUEST (0x23)
    uint16_t nodeId;         // Requesting node
    uint8_t requestedSlots;  // Number of slots needed
    uint8_t priority;        // Request priority (0-255)
    uint8_t reason;          // Reason for request
    uint8_t checksum;        // Message integrity
};
```

### 5.5 Network Manager Election Sequence

When the current network manager fails, a new manager must be elected:

```mermaid
sequenceDiagram
    participant A as Node A
    participant B as Node B (Failed Manager)
    participant C as Node C
    participant D as Node D
    
    Note over A,D: Normal operation with B as network manager
    B->>A: SYNC_BEACON (regular operation)
    B->>C: SYNC_BEACON (regular operation)  
    B->>D: SYNC_BEACON (regular operation)
    
    Note over B: Network Manager B fails/disconnects
    
    Note over A,D: Sync timeout detection
    A->>A: No sync beacon received (timeout)
    C->>C: No sync beacon received (timeout)
    D->>D: No sync beacon received (timeout)
    
    Note over A,D: Manager election process
    A->>A: Enter manager election mode
    A->>*: Broadcast MANAGER_ELECTION (nodeId=A, priority=high)
    
    C->>C: Enter manager election mode
    C->>*: Broadcast MANAGER_ELECTION (nodeId=C, priority=medium)
    
    D->>D: Enter manager election mode
    D->>*: Broadcast MANAGER_ELECTION (nodeId=D, priority=low)
    
    Note over A,D: Election resolution (highest priority wins)
    A->>A: Received all election messages
    A->>A: I have highest priority - become manager
    
    C->>C: Node A has higher priority
    C->>A: MANAGER_ACCEPT (accept A as manager)
    
    D->>D: Node A has higher priority  
    D->>A: MANAGER_ACCEPT (accept A as manager)
    
    Note over A,D: New manager takes control
    A->>A: Transition to NETWORK_MANAGER state
    A->>*: Broadcast SYNC_BEACON (new manager)
    
    C->>C: Synchronize with new manager A
    D->>D: Synchronize with new manager A
```

### 5.6 Fault Recovery and Network Healing

This sequence shows how the network recovers from partitions:

```mermaid
sequenceDiagram
    participant A as Node A
    participant B as Node B  
    participant C as Node C (Partition 1)
    participant D as Node D (Partition 2)
    participant E as Node E
    
    Note over A,E: Normal mesh operation
    Note over C,D: Link failure creates partition
    
    C->>C: Detect lost connection to D
    C->>C: Remove routes via D from table
    C->>*: Broadcast ROUTE_POISON (destinations via D)
    
    D->>D: Detect lost connection to C
    D->>D: Remove routes via C from table  
    D->>*: Broadcast ROUTE_POISON (destinations via C)
    
    Note over A,C: Partition 1 continues operating
    A->>B: Normal data flow
    B->>C: Normal data flow
    
    Note over D,E: Partition 2 continues operating
    D->>E: Normal data flow
    
    Note over A,E: Healing process begins
    Note over C,D: Alternative path discovered (via A-B-E-D)
    
    A->>A: Periodic network discovery
    A->>*: Broadcast NETWORK_HEALING_BEACON
    
    E->>E: Received healing beacon from A
    E->>*: Broadcast NETWORK_MERGE_REQUEST
    
    C->>C: Process merge request
    C->>C: Calculate new routes via A-B-E path
    C->>*: Broadcast ROUTING_UPDATE (restored routes)
    
    D->>D: Process routing update
    D->>D: Network connectivity restored
    
    Note over A,E: Network healed - full connectivity restored
```

---

## 6. Network Discovery & Joining

### 6.1 Network Discovery Process

```mermaid
sequenceDiagram
    participant N as New Node
    participant M as Network Manager
    participant E as Existing Node
    
    N->>N: Enter DISCOVERY state
    N->>*: Broadcast DISCOVERY_REQUEST
    
    alt Network Manager responds
        M->>N: DISCOVERY_RESPONSE (network_info)
        N->>N: Evaluate network
        N->>M: JOIN_REQUEST
        M->>N: JOIN_RESPONSE (slot_assignment)
        N->>N: Enter NORMAL_OPERATION
    else No response
        N->>N: Timeout - become Network Manager
        N->>N: Enter NETWORK_MANAGER state
    end
```

### 6.2 Discovery Messages

#### 6.2.1 Discovery Request
```cpp
struct DiscoveryRequest {
    uint8_t messageType;     // DISCOVERY_REQUEST (0x24)
    uint16_t nodeId;         // Discovering node ID
    uint8_t capabilities;    // Node capabilities
    uint8_t preferredRole;   // Preferred network role
    uint8_t maxHops;         // Maximum network diameter
    uint8_t checksum;        // Message integrity
};
```

#### 6.2.2 Discovery Response
```cpp
struct DiscoveryResponse {
    uint8_t messageType;     // DISCOVERY_RESPONSE (0x25)
    uint16_t managerId;      // Network manager ID
    uint16_t networkId;      // Network identifier
    uint8_t networkSize;     // Number of active nodes
    uint8_t availableSlots;  // Number of free slots
    uint8_t networkQuality;  // Overall network quality
    uint32_t uptime;         // Network uptime
    uint8_t checksum;        // Message integrity
};
```

### 6.3 Join Process

#### 6.3.1 Join Request Handling

```cpp
void ProcessJoinRequest(const JoinRequest& request) {
    // Validate request
    if (!validateJoinRequest(request)) {
        sendJoinResponse(request.nodeId, JOIN_DENIED, "Invalid request");
        return;
    }
    
    // Check available slots
    uint16_t availableSlot = findAvailableSlot();
    if (availableSlot == NO_SLOT_AVAILABLE) {
        sendJoinResponse(request.nodeId, JOIN_DENIED, "No slots available");
        return;
    }
    
    // Assign slot and create response
    assignSlot(request.nodeId, availableSlot);
    sendJoinResponse(request.nodeId, JOIN_ACCEPTED, availableSlot);
    
    // Update network state
    addNodeToNetwork(request.nodeId, availableSlot);
    broadcastNetworkUpdate();
}
```

#### 6.3.2 Join Response Processing

```cpp
void ProcessJoinResponse(const JoinResponse& response) {
    if (response.status == JOIN_ACCEPTED) {
        // Join successful
        assignedSlot = response.assignedSlot;
        networkId = response.networkId;
        synchronizeTime(response.superframeTime);
        
        // Transition to normal operation
        changeState(NORMAL_OPERATION);
        
        // Start slot-based operation
        startSlotScheduler();
    } else {
        // Join denied - return to discovery
        logEvent("Join denied: " + std::string(response.reason));
        changeState(DISCOVERY);
    }
}
```

---

## 7. Packet Structure

### 7.1 Physical Layer Frame

```
┌─────────────────────────────────────────────────────────────────┐
│                        LoRa PHY Header                         │
├─────────────────────────────────────────────────────────────────┤
│                     LoRaMesher Frame                           │
├─────────────────────────────────────────────────────────────────┤
│                        LoRa PHY CRC                            │
└─────────────────────────────────────────────────────────────────┘
```

### 7.2 LoRaMesher Frame Structure

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
├─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┤
│     Message Type    │     Flags     │         Source Node       │
├─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┤
│      Destination Node (optional)     │   TTL   │ Seq Number (H) │
├─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┤
│ Seq Number (L) │ Payload Length │          Payload...         │
├─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┤
│                         ...Payload...                          │
├─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┤
│   Checksum    │
└─┼─┼─┼─┼─┼─┼─┼─┤
```

### 7.3 Header Field Descriptions

| Field | Size | Description |
|-------|------|-------------|
| Message Type | 8 bits | Message category and subtype |
| Flags | 8 bits | Protocol flags (ACK required, priority, etc.) |
| Source Node | 16 bits | Originating node identifier |
| Destination Node | 16 bits | Target node (optional for broadcasts) |
| TTL | 8 bits | Time-to-live (hop limit) |
| Sequence Number | 16 bits | Message sequence number |
| Payload Length | 8 bits | Length of payload data |
| Payload | Variable | Application or protocol data |
| Checksum | 8 bits | XOR checksum of entire frame |

### 7.4 Maximum Frame Sizes

| LoRa Configuration | Max Frame Size | Max Payload Size |
|-------------------|----------------|------------------|
| SF7, BW125, CR4/5 | 255 bytes | 246 bytes |
| SF8, BW125, CR4/5 | 255 bytes | 246 bytes |
| SF9, BW125, CR4/5 | 255 bytes | 246 bytes |
| SF10, BW125, CR4/5 | 255 bytes | 246 bytes |
| SF11, BW125, CR4/5 | 255 bytes | 246 bytes |
| SF12, BW125, CR4/5 | 255 bytes | 246 bytes |

*Note: All LoRa configurations support 255-byte frames. Header overhead is 9 bytes, leaving 246 bytes for payload.*

---

## 8. Error Handling

### 8.1 Error Classification

#### 8.1.1 Protocol Errors
- **Invalid Message Type**: Unknown or corrupted message type
- **Checksum Failure**: Message integrity check failed
- **Timeout Errors**: Expected responses not received
- **State Violations**: Invalid state transitions

#### 8.1.2 Network Errors
- **Route Unavailable**: No path to destination
- **Network Partition**: Network split detected
- **Synchronization Loss**: TDMA timing lost
- **Slot Conflicts**: Multiple nodes using same slot

#### 8.1.3 Hardware Errors
- **Radio Failure**: LoRa module not responding
- **Transmission Failure**: Message send failed
- **Reception Corruption**: Received message corrupted
- **Resource Exhaustion**: Memory or buffer overflow

### 8.2 Error Recovery Mechanisms

#### 8.2.1 Message-Level Recovery
```cpp
enum class MessageResult {
    SUCCESS,
    CHECKSUM_ERROR,
    TIMEOUT,
    ROUTE_ERROR,
    BUFFER_FULL
};

MessageResult sendMessageWithRetry(const Message& msg, uint8_t maxRetries = 3) {
    for (uint8_t attempt = 0; attempt < maxRetries; attempt++) {
        MessageResult result = sendMessage(msg);
        if (result == SUCCESS) {
            return SUCCESS;
        }
        
        // Exponential backoff
        delay(100 * (1 << attempt));
    }
    return TIMEOUT;
}
```

#### 8.2.2 Route Recovery
```cpp
void handleRouteError(uint16_t destination) {
    // Remove failed route
    removeRoute(destination);
    
    // Broadcast route poison
    broadcastRoutePoison(destination);
    
    // Trigger route discovery if needed
    if (hasPendingTraffic(destination)) {
        initiateRouteDiscovery(destination);
    }
}
```

#### 8.2.3 Network Recovery
```cpp
void handleNetworkPartition() {
    // Clear stale routing information
    purgeStaleRoutes();
    
    // Reset synchronization
    resetSynchronization();
    
    // Return to discovery mode
    changeState(DISCOVERY);
    
    // Attempt network healing
    broadcastNetworkHealingBeacon();
}
```

---

## 9. Performance Characteristics

### 9.1 Timing Requirements

| Parameter | Minimum | Typical | Maximum | Units |
|-----------|---------|---------|---------|-------|
| Slot Duration | 500 | 1000 | 5000 | ms |
| Superframe Duration | 4000 | 8000 | 40000 | ms |
| Route Update Interval | 5000 | 10000 | 30000 | ms |
| Join Timeout | 5000 | 10000 | 30000 | ms |
| Sync Tolerance | 50 | 100 | 500 | ms |

### 9.2 Scalability Limits

| Parameter | Current Limit | Design Limit | Notes |
|-----------|---------------|--------------|-------|
| Network Size | 16 nodes | 65535 nodes | Limited by 16-bit node IDs |
| Slots per Superframe | 8 | 255 | Configurable via SuperframeConfig |
| Routes per Node | 50 | 255 | Memory-dependent |
| Message Queue Depth | 10 | 255 | Per-slot queue |
| Hop Count Limit | 15 | 255 | Prevents routing loops |

### 9.3 LoRa Air Time Calculations

| Frame Size | SF7 | SF8 | SF9 | SF10 | SF11 | SF12 |
|------------|-----|-----|-----|------|------|------|
| 50 bytes | 51ms | 103ms | 185ms | 371ms | 741ms | 1.4s |
| 100 bytes | 82ms | 144ms | 267ms | 535ms | 1.0s | 2.1s |
| 200 bytes | 144ms | 267ms | 493ms | 989ms | 1.9s | 3.9s |
| 255 bytes | 185ms | 329ms | 616ms | 1.2s | 2.4s | 4.9s |

*Calculated for BW=125kHz, CR=4/5, with 8-byte preamble*

### 9.4 Memory Usage

#### 9.4.1 Static Memory (per node)
- **Protocol State**: ~100 bytes
- **Configuration**: ~200 bytes  
- **Service Objects**: ~500 bytes
- **Total Static**: ~800 bytes

#### 9.4.2 Dynamic Memory (per node)
- **Routing Table**: ~50 routes × 16 bytes = 800 bytes
- **Message Queues**: ~10 messages × 255 bytes = 2.5KB
- **Network State**: ~300 bytes
- **Total Dynamic**: ~3.6KB

#### 9.4.3 Total Memory Footprint
- **ESP32 Usage**: ~4.4KB RAM + ~50KB Flash
- **Acceptable for**: ESP32 with >100KB RAM available

---

## Conclusion

The LoRaMesher protocol provides a robust, scalable solution for LoRa mesh networking with the following key features:

- **Reliable Routing**: Distance-vector algorithm with link quality metrics
- **Collision-Free Access**: TDMA-based slot coordination
- **Network Synchronization**: Hierarchical timing coordination
- **Fault Tolerance**: Comprehensive error handling and recovery
- **Scalable Architecture**: Service-oriented design supporting protocol extensions

The protocol is designed for embedded systems with limited resources while maintaining the flexibility to scale to larger networks as needed.