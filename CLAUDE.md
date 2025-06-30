# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

LoRaMesher is a C++20 library implementing a distance-vector routing protocol for LoRa mesh networks. It uses RadioLib for low-level radio communication and FreeRTOS for task scheduling. The library supports both ESP32 and desktop environments for testing.

## Build System & Commands

### CMake (Desktop Testing)
```bash
mkdir build && cd build
cmake .. -DBUILD_DESKTOP=ON
cmake --build . --target loramesher_lib     # Build library only
cmake --build . --target build_all_tests   # Build all tests
cmake --build . --target run_all_tests     # Build and run tests
ctest                                       # Alternative test runner
```

### PlatformIO (ESP32 Development)
```bash
pio run                                     # Build for ESP32
pio run -e native                          # Build for native/desktop
pio run -e test_native                     # Build native tests with debug flags
pio test                                   # Run tests
pio device monitor                         # Monitor serial output
```

## Architecture Overview

### Core Components
- **LoraMesher**: Main API entry point using Builder pattern
- **ProtocolManager**: Manages different communication protocols
- **HardwareManager**: Hardware abstraction layer for different platforms
- **RTOS Layer**: Task management and synchronization

### Protocol Stack
- **LoRaMeshProtocol**: Main mesh networking protocol with discovery, joining, and routing
- **PingPongProtocol**: Simple point-to-point communication for testing
- **Message System**: Type-safe message handling with headers and payloads

### State Machine
The mesh protocol operates in 6 states:
1. **Initialization**: System startup
2. **Discovery**: Network detection or creation
3. **Joining**: Connection to existing network
4. **Normal Operation**: Data transmission/reception
5. **Network Manager**: Special coordination role  
6. **Fault Recovery**: Connection loss handling

### Hardware Abstraction
- **RadioLib Integration**: SX126x, SX127x series support
- **Platform Support**: ESP32 (Arduino/ESP-IDF) and native desktop
- **Mock Interfaces**: Full testing framework with hardware mocks

## Key Directories

- `src/`: Main library source code
  - `protocols/`: Network protocols and services
  - `hardware/`: Platform abstraction layer
  - `types/`: Message types, configurations, error codes
  - `os/`: RTOS abstraction
- `test/`: Comprehensive test suite with unit and integration tests
- `examples/`: Usage examples for mesh and ping-pong modes
- `docs/`: Technical documentation and protocol diagrams

## Coding Standards

Follows Google C++ Style Guide with specific conventions:
- **C++20 standard required**
- **4 spaces indentation, 100 char line limit**
- **PascalCase**: Classes and functions
- **UPPER_CASE**: Constants and enums
- **trailing underscore**: Private members (`config_`)
- **Doxygen comments** for public APIs
- **RAII principles** with smart pointers

## Platform Differences

The codebase conditionally compiles for different platforms:
- **ESP32**: Uses Arduino framework, FreeRTOS, RadioLib hardware drivers
- **Desktop**: Uses mocked hardware, native threading, for testing only
- Build flags automatically set via `scripts/extra_script.py`

## Testing Framework

Uses GoogleTest with comprehensive coverage:
- **Unit tests**: Individual component testing
- **Integration tests**: Protocol interaction testing  
- **Hardware mocks**: Full radio and RTOS simulation
- **Network simulation**: Multi-node mesh testing framework
- **PlatformIO Native Tests**: Run native tests using `pio test -e test_native`