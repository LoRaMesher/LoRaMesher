# LoRaMesher Utilities
## Update and Monitor

### Introduction
updateAndMonitor.py is a script that updates all connected devices to the actual code. After that, it will monitor all the devices that are connected.

### Usage
1. Installation of the PlatformIO for Python:
```
pip install platformio
```

2. Wait until the PlatformIO is fully initialized.
3. Execute it in the terminal.
4. Wait for the results.

WARNING!: It is not fully tested. Every time you execute it, it is necessary to close the terminal where it has been executed, because of the multiple process that are being executed. This should be fixed in some way, but at the moment is what it is.


## Analyze Monitors

### Introduction
Initial script to analyze all the packets that have been arrived by the protocol.
The results of the analysis is a summary of each node, telling all the types of packets that have been arrived and sent.
A final line a summary of all the nodes and an approximation of the results, such as the packet loss or collisions occurred.

### Usage
Execute this script in the same folder where it have the monitors. The name of the monitors should contain "monitor", "COM" and ".txt"
