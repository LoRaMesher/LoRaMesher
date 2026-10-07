# LoRaMesher Simple Example

A minimal example demonstrating how to set up a LoRa mesh network node using the LoRaMesher library.

## What This Example Does

1. **Initializes** the LoRa radio with configured parameters
2. **Discovers** existing mesh networks or creates a new one
3. **Joins** the network and synchronizes timing
4. **Sends** periodic "Hello from node!" messages to discovered peers
5. **Displays** routing table and network status on serial output

## Hardware Requirements

### Supported Boards

| Board | LoRa Module | CS | RST | IRQ (DIO0) | IO1 (DIO1) |
|-------|-------------|-----|-----|------------|------------|
| TTGO T-Beam v1.x | SX1276 | 18 | 23 | 26 | 33 |
| TTGO LoRa32 v1 | SX1276 | 18 | 14 | 26 | 33 |
| Heltec WiFi LoRa 32 | SX1276 | 18 | 14 | 26 | 35 |

If using a different board, update the pin definitions in `main.cpp`:

```cpp
#define LORA_CS 18    // SPI Chip Select (NSS)
#define LORA_RST 23   // Radio Reset pin
#define LORA_IRQ 26   // DIO0 - Primary interrupt
#define LORA_IO1 33   // DIO1 - Secondary interrupt
```

## Radio Configuration

| Parameter | Default Value | Description |
|-----------|---------------|-------------|
| Frequency | 869.9 MHz | EU868 band - adjust for your region |
| Spreading Factor | 7 | SF7-SF12: higher = more range, slower data rate |
| Bandwidth | 125 kHz | Standard bandwidth (125/250/500 kHz) |
| Coding Rate | 4/7 | Error correction rate (4/5 to 4/8) |
| TX Power | 6 dBm | Start low for testing, max depends on module |
| Sync Word | 0x14 (20) | Network identifier - all nodes must match |

### Regional Frequency Bands

| Region | Frequency Range | Suggested |
|--------|-----------------|-----------|
| EU868 | 863-870 MHz | 869.9 MHz |
| US915 | 902-928 MHz | 915.0 MHz |
| AU915 | 915-928 MHz | 915.0 MHz |
| AS923 | 920-923 MHz | 920.0 MHz |

## Building and Uploading

### Using PlatformIO CLI

```bash
# Build the project
pio run -e ttgo-t-beam

# Upload to the board
pio run -e ttgo-t-beam --target upload

# Monitor serial output
pio device monitor -b 115200
```

### Using PlatformIO IDE

1. Open the project folder in VS Code with PlatformIO extension
2. Select your board environment from the status bar
3. Click the Upload button (arrow icon)
4. Open Serial Monitor (plug icon)

## Expected Serial Output

### Initial Startup

```
[INFO] Initializing LoraMesher
[INFO] Generated node address 0x82DF from hardware
[INFO] LoRaMesh protocol initialized for node 0x82DF
[INFO] LoraMesher started successfully
Routing table has 0 entries:
Network status: State=1, Manager=0x0, Slot=0, Nodes=0
Node is not synchronized.
```

### After Discovering a Network

```
Route updated - Destination: 0x3ADF, Next hop: 0x3ADF, Hops: 1
[INFO] Discovery: Found existing network with id 0x3ADF
[INFO] Transitioning from DISCOVERY to JOINING for network 0x3ADF
```

### Normal Operation

```
Routing table has 1 entries:
  Destination: 0x3adf, Next hop: 0x3adf, Hops: 1, Valid: yes
Network status: State=2, Manager=0x3adf, Slot=2, Nodes=1
Node is synchronized, last sync 0 ms ago.
Sent message to 0x3adf
```

### Receiving Data

```
Received data from: 0x3adf (16 bytes)
48 65 6c 6c 6f 20 66 72 6f 6d 20 6e 6f 64 65 21
```

## Customizing the Example

### Change the Message Content

```cpp
// In sendTestMessage()
std::string message = "Your custom message here";
```

### Process Received Data

```cpp
void OnDataReceived(AddressType source, const std::vector<uint8_t>& data) {
    // Convert to string
    std::string text(data.begin(), data.end());
    std::cout << "Message from 0x" << std::hex << source
              << ": " << text << std::endl;

    // Add your processing logic here
    if (text == "ping") {
        // Send response
        std::string response = "pong";
        mesher->Send(source,
            std::vector<uint8_t>(response.begin(), response.end()));
    }
}
```

### Adjust Timing

The loop sends only when the TX queue is empty and waits for the node's next data slot; `kLoopIntervalMs` sets the time between iterations:

```cpp
constexpr uint32_t kLoopIntervalMs = 10000;  // Time between loop() iterations
```

Shortening it only helps while the network has spare capacity. A node's data slots (`GetDataSlotsPerSuperframe()` per superframe) also carry the messages it forwards for other nodes, so a rate that works for two neighbours can overload a larger or multi-hop network. See the Traffic Budget section in `examples/reliable_example`.

## Troubleshooting

### "Node is not synchronized"

The node is in discovery mode, searching for an existing network. This is normal on startup. If it persists:
- Ensure another node is powered on and running
- Check that all nodes use the same frequency and sync word
- Verify antennas are connected
- Check if both nodes have initiated a network. You can avoid this by defining the role of the node.
    `mesh_config.setNodeRole(NodeRole::NETWORK_MANAGER);` or `mesh_config.setNodeRole(NodeRole::NODE_ONLY);`

### No messages being sent/received

1. **Check antennas** - Never power on LoRa modules without antennas attached
2. **Verify pin configuration** - Ensure pins match your board
3. **Check frequency** - All nodes must use the same frequency
4. **Check sync word** - All nodes must use the same sync word
5. **Reduce distance** - Start with nodes close together for testing

### Radio initialization failure

```
Failed to start LoraMesher: [error message]
```

- Verify SPI pins are correct for your board
- Check that no other code is using the SPI bus
- Ensure the LoRa module is properly connected

## Code Structure

```
main.cpp
├── Hardware Configuration     (lines 20-30)
├── Radio Configuration        (lines 32-47)
├── Global Variables           (line 53-54)
├── OnDataReceived callback    (lines 60-77)
├── Helper Functions
│   ├── printRoutingTable()    (lines 78-95)
│   ├── printNetworkStatus()   (lines 97-111)
│   └── sendTestMessage()      (lines 113-145)
├── ConfigureAndUseLoraMesher  (lines 147-200)
└── Arduino setup()/loop()     (lines 201-224)
```

## Further Reading

- [LoRaMesher Protocol Specification](../../docs/PROTOCOL_SPEC.md)
- [RadioLib Documentation](https://jgromes.github.io/RadioLib/)
