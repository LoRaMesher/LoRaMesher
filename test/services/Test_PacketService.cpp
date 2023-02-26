#include <Arduino.h>
#include <unity.h>

#include <LoraMesher.h>

//Define LoRaMesher god mode, to test private functions too.
#define LM_GOD_MODE

//TODO: If is ControlPacket -> Is DataPacket.

void testHasDataPacket() {
    TEST_ASSERT_TRUE(PacketService::isDataPacket(DATA_P | NEED_ACK_P | ACK_P | XL_DATA_P | LOST_P | SYNC_P));
}

void testHasNotDataPacket() {
    TEST_ASSERT_FALSE(PacketService::isDataPacket(HELLO_P));
}

void testHasControlPacket() {
    TEST_ASSERT_TRUE(PacketService::isControlPacket(NEED_ACK_P | ACK_P | XL_DATA_P | LOST_P | SYNC_P));
}

void testHasNotControlPacket() {
    TEST_ASSERT_FALSE(PacketService::isDataPacket(HELLO_P | DATA_P));
}

void testGetExtraLengthToPayloadDataPacket() {
    uint8_t headerToPayloadLength = PacketService::getExtraLengthToPayload(DATA_P);
    TEST_ASSERT_EQUAL(2, headerToPayloadLength);
    TEST_ASSERT_EQUAL(sizeof(DataPacket) - sizeof(PacketHeader), headerToPayloadLength);
}

void testGetExtraLengthToPayloadControlPacketAndDataPacket() {
    TEST_ASSERT_EQUAL(5, PacketService::getExtraLengthToPayload(NEED_ACK_P | ACK_P | XL_DATA_P | LOST_P | SYNC_P));
}

void testDataPacketLength() {
    uint8_t dataPLen = sizeof(DataPacket) + sizeof(PacketHeader);
    TEST_ASSERT_EQUAL(9, dataPLen);
}

void testControlPacketLength() {
    uint8_t controlPLen = sizeof(ControlPacket) + sizeof(PacketHeader);
    TEST_ASSERT_EQUAL(10, controlPLen);
}

void testPacketService() {
    Serial.println("- Test Packet Service");
    RUN_TEST(testHasDataPacket);
    RUN_TEST(testHasNotDataPacket);
    RUN_TEST(testHasControlPacket);
    RUN_TEST(testHasNotControlPacket);
    RUN_TEST(testGetExtraLengthToPayloadDataPacket);
    RUN_TEST(testGetExtraLengthToPayloadControlPacketAndDataPacket);
    RUN_TEST(testDataPacketLength);
    RUN_TEST(testControlPacketLength);
}