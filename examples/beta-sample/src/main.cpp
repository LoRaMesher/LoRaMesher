#include <Arduino.h>
#include "loramesher.h"


LoraMesher* radio;


void setup() {
  Serial.begin(9600);
  radio = new LoraMesher();
}

void loop() {
  sleep(7);
  radio->sendDataPacket();
}