# LoRaMesher Coding Style Guide

We are using [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)

## General Rules
- Use 4 spaces for indentation
- Maximum line length is 100 characters
- Use UTF-8 encoding
- Files should end with a newline
- Remove trailing whitespace

## Naming Conventions
### Files
- Header files: `.hpp` for C++, `.h` for C
- Source files: `.cpp` for C++, `.c` for C
- Use lowercase with underscores: `mesh_protocol.hpp`

### Classes
- Use PascalCase: `class RadioManager`
- One class per file

### Functions & Variables
- Use camelCase: `sendMessage()`
- Private member variables: ends with underscore `_`: `radioConfig_`

### Constants & Enums
- Use UPPER_CASE: `MAX_PACKET_SIZE`

## Header Files
- Use pragma once
- Use forward declarations when possible
- Order includes:
  1. Related header
  2. A blank line
  3. C++ standard library
  4. A blank line
  3. Other libraries
  4. A blank line
  5. Project headers

## Example
```cpp
/// @file radio_manager.hpp
/// @brief Defines the RadioManager class for handling LoRa communication.

#pragma once

#include <memory>
#include <string>

#include "loramesher/core/types.hpp"

namespace loramesher {

/**
 * @class RadioManager
 * @brief Manages the LoRa radio communication.
 *
 * This class provides functionalities to send packets using a LoRa radio module.
 */
class RadioManager {
public:
    /**
     * @brief Constructs a new RadioManager object.
     *
     * Initializes the radio module and sets up necessary configurations.
     */
    RadioManager();

    /**
     * @brief Sends a packet over the LoRa network.
     *
     * @param packet The packet to be transmitted.
     * @return true if the packet is successfully sent, false otherwise.
     */
    bool sendPacket(const Packet& packet);

private:
    /// @brief Maximum number of retry attempts for sending a packet.
    static constexpr uint32_t MAX_RETRY_COUNT = 3;
    
    /// @brief Unique pointer to the radio interface.
    std::unique_ptr<Radio> radio_;
    
    /// @brief Configuration settings for the radio manager.
    Config config_;
};

} // namespace loramesher

```

## Comments

- Use Doxygen-style comments for public interfaces
- Keep comments up to date
- Explain why, not what

## Best Practices

1. RAII - Use smart pointers
2. Make member functions const when possible
3. Use references instead of pointers when possible
4. Initialize all variables
5. Use enum class instead of plain enums