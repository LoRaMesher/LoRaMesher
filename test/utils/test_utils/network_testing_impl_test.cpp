// /**
//  * @file test_network_testing_impl.cpp
//  * @brief Comprehensive Google Test suite for network testing implementation
//  * @author Generated Test Suite
//  */

// #include <gmock/gmock.h>
// #include <gtest/gtest.h>
// #include <chrono>
// #include <memory>
// #include <thread>
// #include <vector>

// #include "../test/utils/mock_radio.hpp"
// #include "../test/utils/network_testing_impl.hpp"

// using ::testing::_;
// using ::testing::AnyNumber;
// using ::testing::DoAll;
// using ::testing::Invoke;
// using ::testing::NiceMock;
// using ::testing::Return;
// using ::testing::SaveArg;
// using ::testing::InvokeWithoutArgs;

// namespace loramesher {
// namespace test {

// /**
//  * @brief Test fixture for network testing implementation
//  */
// class NetworkTestingImplTest : public ::testing::Test {
//    protected:
//     /**
//      * @brief Set up test environment
//      */
//     void SetUp() override {
//         network_ = std::make_unique<VirtualNetwork>();
//         time_controller_ = std::make_unique<VirtualTimeController>(*network_);

//         // Create mock radios with basic expectations that are always needed
//         radio1_ = std::make_unique<NiceMock<radio::test::MockRadio>>();
//         radio2_ = std::make_unique<NiceMock<radio::test::MockRadio>>();
//         radio3_ = std::make_unique<NiceMock<radio::test::MockRadio>>();

//         // Create adapters
//         adapter1_ = std::make_unique<RadioToNetworkAdapter>(
//             radio1_.get(), *network_, kNode1Address);
//         adapter2_ = std::make_unique<RadioToNetworkAdapter>(
//             radio2_.get(), *network_, kNode2Address);
//         adapter3_ = std::make_unique<RadioToNetworkAdapter>(
//             radio3_.get(), *network_, kNode3Address);

//         // Register nodes with network
//         network_->RegisterNode(kNode1Address, adapter1_.get());
//         network_->RegisterNode(kNode2Address, adapter2_.get());
//         network_->RegisterNode(kNode3Address, adapter3_.get());
//     }

//     /**
//      * @brief Tear down test environment
//      */
//     void TearDown() override {
//         // Clean up in reverse order
//         adapter3_.reset();
//         adapter2_.reset();
//         adapter1_.reset();
//         radio3_.reset();
//         radio2_.reset();
//         radio1_.reset();
//         time_controller_.reset();
//         network_.reset();
//     }

//     // Test constants
//     static constexpr uint32_t kNode1Address = 0x1001;
//     static constexpr uint32_t kNode2Address = 0x1002;
//     static constexpr uint32_t kNode3Address = 0x1003;
//     static constexpr int8_t kDefaultRSSI = -65;
//     static constexpr int8_t kDefaultSNR = 8;

//     // Test objects
//     std::unique_ptr<VirtualNetwork> network_;
//     std::unique_ptr<VirtualTimeController> time_controller_;
//     std::unique_ptr<NiceMock<radio::test::MockRadio>> radio1_;
//     std::unique_ptr<NiceMock<radio::test::MockRadio>> radio2_;
//     std::unique_ptr<NiceMock<radio::test::MockRadio>> radio3_;
//     std::unique_ptr<RadioToNetworkAdapter> adapter1_;
//     std::unique_ptr<RadioToNetworkAdapter> adapter2_;
//     std::unique_ptr<RadioToNetworkAdapter> adapter3_;

//     bool callback1_called_ = false;
//     bool callback2_called_ = false;
//     bool callback3_called_ = false;
// };

// /**
//  * @brief Test basic virtual network functionality
//  */
// TEST_F(NetworkTestingImplTest, BasicNetworkRegistration) {
//     // Test that nodes are properly registered
//     EXPECT_TRUE(network_->IsLinkActive(kNode1Address, kNode2Address));
//     EXPECT_TRUE(network_->IsLinkActive(kNode2Address, kNode1Address));

//     // Test current time
//     EXPECT_EQ(network_->GetCurrentTime(), 0);
//     EXPECT_EQ(time_controller_->GetCurrentTime(), 0);
// }

// /**
//  * @brief Test message transmission between nodes
//  */
// TEST_F(NetworkTestingImplTest, MessageTransmission) {
//     const std::vector<uint8_t> test_message = {'H', 'e', 'l', 'l', 'o'};

//     // Track callback invocations
//     bool callback2_called = false;
//     bool callback3_called = false;

//     // Set up radio2 and radio3 to be in receive mode
//     EXPECT_CALL(*radio2_, getState())
//         .WillRepeatedly(Return(loramesher::radio::RadioState::kReceive));
//     EXPECT_CALL(*radio3_, getState())
//         .WillRepeatedly(Return(loramesher::radio::RadioState::kReceive));

//     // Override the setActionReceive expectation to capture callbacks
//     EXPECT_CALL(*radio2_, setActionReceive(A<void(*)()>()))
//         .WillRepeatedly(InvokeWithoutArgs([this]() {
//             callback2_called_ = true;
//             return Result::Success();
//         }));

//     EXPECT_CALL(*radio3_, setActionReceive(A<void(*)()>()))
//         .WillRepeatedly(InvokeWithoutArgs([this]() {
//             callback3_called_ = true;
//             return Result::Success();
//         }));

//     radio2_->StartReceive();
//     radio3_->StartReceive();

//     // Transmit message from node1
//     network_->TransmitMessage(kNode1Address, test_message, kDefaultRSSI,
//                               kDefaultSNR);

//     // Advance time to process message delivery
//     time_controller_->AdvanceTime(10);

//     // Verify callbacks were called (messages delivered)
//     EXPECT_TRUE(callback2_called);
//     EXPECT_TRUE(callback3_called);
// }

// /**
//  * @brief Test link control functionality
//  */
// TEST_F(NetworkTestingImplTest, LinkControl) {
//     const std::vector<uint8_t> test_message = {'T', 'e', 's', 't'};

//     // Initially, link should be active
//     EXPECT_TRUE(network_->IsLinkActive(kNode1Address, kNode2Address));

//     // Disable link between node1 and node2
//     network_->SetLinkStatus(kNode1Address, kNode2Address, false);
//     EXPECT_FALSE(network_->IsLinkActive(kNode1Address, kNode2Address));
//     EXPECT_FALSE(network_->IsLinkActive(kNode2Address, kNode1Address));

//     // Set up radio3 to be in receive mode (should still receive)
//     EXPECT_CALL(*radio3_, getState())
//         .WillRepeatedly(Return(loramesher::radio::RadioState::kReceive));

//     bool callback3_called = false;
//     EXPECT_CALL(*radio3_, setActionReceive(A<void (*)()>()))
//         .WillRepeatedly(DoAll(Invoke([&callback3_called](void (*)()) {
//                                   callback3_called = true;
//                               }),
//                               Return(Result::Success())));

//     // Transmit message - node2 should not receive, node3 should
//     network_->TransmitMessage(kNode1Address, test_message);
//     time_controller_->AdvanceTime(10);

//     // Node3 should have received the message
//     EXPECT_TRUE(callback3_called);

//     // Re-enable link
//     network_->SetLinkStatus(kNode1Address, kNode2Address, true);
//     EXPECT_TRUE(network_->IsLinkActive(kNode1Address, kNode2Address));
// }

// /**
//  * @brief Test message delay functionality
//  */
// TEST_F(NetworkTestingImplTest, MessageDelay) {
//     const std::vector<uint8_t> test_message = {'D', 'e', 'l', 'a', 'y'};
//     const uint32_t delay_ms = 100;

//     // Set delay between nodes
//     network_->SetMessageDelay(kNode1Address, kNode2Address, delay_ms);

//     // Set up radio2 to be in receive mode
//     EXPECT_CALL(*radio2_, getState())
//         .WillRepeatedly(Return(loramesher::radio::RadioState::kReceive));

//     bool callback_called = false;
//     EXPECT_CALL(*radio2_, setActionReceive(A<void (*)()>()))
//         .WillRepeatedly(DoAll(
//             Invoke([&callback_called](void (*)()) { callback_called = true; }),
//             Return(Result::Success())));

//     // Transmit message
//     network_->TransmitMessage(kNode1Address, test_message);

//     // Message should not be delivered immediately
//     time_controller_->AdvanceTime(delay_ms - 1);
//     EXPECT_FALSE(callback_called);

//     // Advance time to exactly the delay
//     time_controller_->AdvanceTime(1);

//     // Now callback should have been called
//     EXPECT_TRUE(callback_called);
// }

// /**
//  * @brief Test packet loss functionality
//  */
// TEST_F(NetworkTestingImplTest, PacketLoss) {
//     const std::vector<uint8_t> test_message = {'L', 'o', 's', 's'};

//     // Set 100% packet loss
//     network_->SetPacketLossRate(1.0f);

//     // Set up radio2 to be in receive mode
//     EXPECT_CALL(*radio2_, getState())
//         .WillRepeatedly(Return(loramesher::radio::RadioState::kReceive));

//     bool callback_called_with_loss = false;
//     EXPECT_CALL(*radio2_, setActionReceive(A<void (*)()>()))
//         .WillRepeatedly(DoAll(Invoke([&callback_called_with_loss](void (*)()) {
//                                   callback_called_with_loss = true;
//                               }),
//                               Return(Result::Success())));

//     // Transmit message - should be lost
//     network_->TransmitMessage(kNode1Address, test_message);
//     time_controller_->AdvanceTime(10);

//     // Message should be lost
//     EXPECT_FALSE(callback_called_with_loss);

//     // Reset packet loss to 0%
//     network_->SetPacketLossRate(0.0f);

//     bool callback_called_without_loss = false;
//     EXPECT_CALL(*radio2_, setActionReceive(A<void (*)()>()))
//         .WillRepeatedly(
//             DoAll(Invoke([&callback_called_without_loss](void (*)()) {
//                       callback_called_without_loss = true;
//                   }),
//                   Return(Result::Success())));

//     // Now message should go through
//     network_->TransmitMessage(kNode1Address, test_message);
//     time_controller_->AdvanceTime(10);

//     EXPECT_TRUE(callback_called_without_loss);
// }

// /**
//  * @brief Test radio state checking
//  */
// TEST_F(NetworkTestingImplTest, RadioStateChecking) {
//     const std::vector<uint8_t> test_message = {'S', 't', 'a', 't', 'e'};

//     // Set radio2 to sleep mode - should not receive
//     EXPECT_CALL(*radio2_, getState())
//         .WillRepeatedly(Return(loramesher::radio::RadioState::kSleep));

//     // Set radio3 to receive mode - should receive
//     EXPECT_CALL(*radio3_, getState())
//         .WillRepeatedly(Return(loramesher::radio::RadioState::kReceive));

//     bool callback2_called = false;
//     bool callback3_called = false;

//     EXPECT_CALL(*radio2_, setActionReceive(A<void (*)()>()))
//         .WillRepeatedly(DoAll(Invoke([&callback2_called](void (*)()) {
//                                   callback2_called = true;
//                               }),
//                               Return(Result::Success())));

//     EXPECT_CALL(*radio3_, setActionReceive(A<void (*)()>()))
//         .WillRepeatedly(DoAll(Invoke([&callback3_called](void (*)()) {
//                                   callback3_called = true;
//                               }),
//                               Return(Result::Success())));

//     // Transmit message
//     network_->TransmitMessage(kNode1Address, test_message);
//     time_controller_->AdvanceTime(10);

//     // Only radio3 should have received (radio2 is sleeping)
//     EXPECT_FALSE(callback2_called);
//     EXPECT_TRUE(callback3_called);
// }

// /**
//  * @brief Test virtual time controller scheduling
//  */
// TEST_F(NetworkTestingImplTest, VirtualTimeScheduling) {
//     bool event1_triggered = false;
//     bool event2_triggered = false;
//     bool event3_triggered = false;

//     // Schedule events at different times
//     time_controller_->ScheduleAt(
//         50, [&event1_triggered]() { event1_triggered = true; });

//     time_controller_->ScheduleAfter(
//         75, [&event2_triggered]() { event2_triggered = true; });

//     time_controller_->ScheduleAt(
//         100, [&event3_triggered]() { event3_triggered = true; });

//     // Advance time progressively
//     time_controller_->AdvanceTime(25);
//     EXPECT_FALSE(event1_triggered);
//     EXPECT_FALSE(event2_triggered);
//     EXPECT_FALSE(event3_triggered);

//     time_controller_->AdvanceTime(25);  // Total: 50ms
//     EXPECT_TRUE(event1_triggered);
//     EXPECT_FALSE(event2_triggered);
//     EXPECT_FALSE(event3_triggered);

//     time_controller_->AdvanceTime(25);  // Total: 75ms
//     EXPECT_TRUE(event1_triggered);
//     EXPECT_TRUE(event2_triggered);
//     EXPECT_FALSE(event3_triggered);

//     time_controller_->AdvanceTime(25);  // Total: 100ms
//     EXPECT_TRUE(event1_triggered);
//     EXPECT_TRUE(event2_triggered);
//     EXPECT_TRUE(event3_triggered);
// }

// /**
//  * @brief Test radio adapter functionality
//  */
// TEST_F(NetworkTestingImplTest, RadioAdapterFunctionality) {
//     // Test CanReceive functionality
//     EXPECT_CALL(*radio1_, getState())
//         .WillOnce(Return(loramesher::radio::RadioState::kReceive))
//         .WillOnce(Return(loramesher::radio::RadioState::kSleep));

//     EXPECT_TRUE(adapter1_->CanReceive());
//     EXPECT_FALSE(adapter1_->CanReceive());

//     // Test GetRadioState functionality
//     EXPECT_CALL(*radio1_, getState())
//         .WillOnce(Return(loramesher::radio::RadioState::kTransmit));

//     EXPECT_EQ(adapter1_->GetRadioState(),
//               loramesher::radio::RadioState::kTransmit);
// }

// /**
//  * @brief Test end-to-end message flow
//  */
// TEST_F(NetworkTestingImplTest, EndToEndMessageFlow) {
//     const std::vector<uint8_t> test_message = {'E', '2', 'E'};

//     // Override default expectations for detailed message flow testing
//     EXPECT_CALL(*radio2_, getState())
//         .WillRepeatedly(Return(loramesher::radio::RadioState::kReceive));

//     EXPECT_CALL(*radio2_, getPacketLength())
//         .WillRepeatedly(Return(test_message.size()));

//     EXPECT_CALL(*radio2_, getRSSI()).WillRepeatedly(Return(kDefaultRSSI));

//     EXPECT_CALL(*radio2_, getSNR()).WillRepeatedly(Return(kDefaultSNR));

//     EXPECT_CALL(*radio2_, readData(_, _))
//         .WillRepeatedly(
//             Invoke([&test_message](uint8_t* data, size_t len) -> Result {
//                 if (len >= test_message.size()) {
//                     std::copy(test_message.begin(), test_message.end(), data);
//                     return Result::Success();
//                 }
//                 return Result(LoraMesherErrorCode::kBufferOverflow,
//                               "Buffer too small");
//             }));

//     bool receive_callback_called = false;
//     EXPECT_CALL(*radio2_, setActionReceive(A<void (*)()>()))
//         .WillRepeatedly(DoAll(Invoke([&receive_callback_called](void (*)()) {
//                                   receive_callback_called = true;
//                               }),
//                               Return(Result::Success())));

//     // Simulate message transmission and reception
//     network_->TransmitMessage(kNode1Address, test_message, kDefaultRSSI,
//                               kDefaultSNR);
//     time_controller_->AdvanceTime(10);

//     // Verify the callback was called
//     EXPECT_TRUE(receive_callback_called);
// }

// /**
//  * @brief Test network cleanup and unregistration
//  */
// TEST_F(NetworkTestingImplTest, NetworkCleanup) {
//     // Unregister a node
//     network_->UnregisterNode(kNode2Address);

//     const std::vector<uint8_t> test_message = {'C', 'l', 'e', 'a', 'n'};

//     // Set up radio3 to receive (radio2 is unregistered)
//     EXPECT_CALL(*radio3_, getState())
//         .WillRepeatedly(Return(loramesher::radio::RadioState::kReceive));

//     bool callback2_called = false;
//     bool callback3_called = false;

//     EXPECT_CALL(*radio2_, setActionReceive(A<void (*)()>()))
//         .WillRepeatedly(DoAll(Invoke([&callback2_called](void (*)()) {
//                                   callback2_called = true;
//                               }),
//                               Return(Result::Success())));

//     EXPECT_CALL(*radio3_, setActionReceive(A<void (*)()>()))
//         .WillRepeatedly(DoAll(Invoke([&callback3_called](void (*)()) {
//                                   callback3_called = true;
//                               }),
//                               Return(Result::Success())));

//     // Transmit message - only radio3 should receive (radio2 is unregistered)
//     network_->TransmitMessage(kNode1Address, test_message);
//     time_controller_->AdvanceTime(10);

//     EXPECT_FALSE(callback2_called);  // Should not be called (unregistered)
//     EXPECT_TRUE(callback3_called);   // Should be called (still registered)
// }

// }  // namespace test
// }  // namespace loramesher