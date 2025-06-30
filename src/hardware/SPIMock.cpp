// SPIMock.cpp
#include "SPIMock.hpp"

#ifndef ARDUINO
// Define the global SPI instances for native builds
SPIClass SPI(0);
SPIClass SPI1(1);
SPIClass SPI2(2);
#endif