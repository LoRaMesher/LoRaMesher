# LoRaMesher Coding Style Guide

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
  2. C++ standard library
  3. Other libraries
  4. Project headers

## Example
```cpp
#pragma once

#include <memory>
#include <string>

#include "loramesher/core/types.hpp"

namespace loramesher {

class RadioManager {
public:
    RadioManager();
    bool sendPacket(const Packet& packet);

private:
    static constexpr uint32_t MAX_RETRY_COUNT = 3;
    
    std::unique_ptr<Radio> radio_;
    Config                 config_;
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