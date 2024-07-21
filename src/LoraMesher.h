#ifndef _LORAMESHER_H
#define _LORAMESHER_H

// LoRa libraries
#include "RadioLib.h"

//Actual LoRaMesher Libraries
#include "BuildOptions.h"

#include "modules/LM_Modules.h"

#include "utilities/LinkedQueue.hpp"

#include "services/PacketService.h"

#include "services/RoutingTableService.h"

#include "services/PacketQueueService.h"

#include "services/WiFiService.h"

#include "services/RoleService.h"

#include "services/SimulatorService.h"

/**
 * @brief LoRaMesher Library
 *
 */
class LoraMesher {

public:
    /**
     * @brief Get the Instance of the LoRaMesher
     *
     * @return LoraMesher&
     */
    static LoraMesher& getInstance() {
        static LoraMesher instance;
        return instance;
    };

    enum LoraModules {
        SX1276_MOD,
        SX1262_MOD,
        SX1278_MOD,
        SX1268_MOD,
        SX1280_MOD,
    };

    /**
     * @brief LoRaMesher configuration
     *
     */
    struct LoraMesherConfig {
        // LoRa pins
        uint8_t loraCs = 0; // LoRa chip select pin
        uint8_t loraIrq = 0; // LoRa IRQ pin
        uint8_t loraRst = 0; // LoRa reset pin
        uint8_t loraIo1 = 0; // LoRa DIO1 pin

        // LoRa configuration
        LoraModules module = LoraModules::SX1276_MOD; // Define the module to be used. Allowed values are in the BuildOptions.h file. By default is SX1276_MOD
        float freq = LM_BAND; // Carrier frequency in MHz. Allowed values range from 137.0 MHz to 1020.0 MHz.
        float bw = LM_BANDWIDTH; // LoRa bandwidth in kHz. Allowed values are 10.4, 15.6, 20.8, 31.25, 41.7, 62.5, 125, 250 and 500 kHz.
        uint8_t sf = LM_LORASF; // LoRa spreading factor. Allowed values range from 6 to 12.
        uint8_t cr = LM_CODING_RATE; // LoRa coding rate denominator. Allowed values range from 5 to 8.
        uint8_t syncWord = LM_SYNC_WORD; // LoRa sync word. Can be used to distinguish different networks. Note that value 0x34 is reserved for LoRaWAN networks.
        int8_t power = LM_POWER; // Transmission output power in dBm. Allowed values range from 2 to 17 dBm.
        uint16_t preambleLength = LM_PREAMBLE_LENGTH; // Length of LoRa transmission preamble in symbols. The actual preamble length is 4.25 symbols longer than the set number. Allowed values range from 6 to 65535.
        // MAX packet size per packet in bytes. It could be changed between 13 and 255 bytes. Recommended 100 or less bytes.
        // If exceed it will be automatically separated through multiple packets 
        // In bytes (226 bytes [UE max allowed with SF7 and 125khz])
        // MAX payload size for hello packets = LM_MAX_PACKET_SIZE - 7 bytes of header
        // MAX payload size for data packets = LM_MAX_PACKET_SIZE - 7 bytes of header - 2 bytes of via
        // MAX payload size for reliable and large packets = LM_MAX_PACKET_SIZE - 7 bytes of header - 2 bytes of via - 3 of control packet.
        // Having different max_packet_size in the same network will cause problems.
        size_t max_packet_size = LM_MAX_PACKET_SIZE;
#ifdef ARDUINO
        // Custom SPI pins
        SPIClass* spi = nullptr;
#else
        // Custom RadioLibHal
        RadioLibHal* hal = nullptr;
#endif
        LoraMesherConfig() {}
    };

    /**
     * @brief LoRaMesh initialization method.
     *
     * @param config Configuration of the LoRaMesher
     *
     * Example of usage:
     * @code
     * void processReceivedPackets(void* parameters) {
     *      for (;;) {
     *          //Wait for the notification of processReceivedPackets and enter blocking
     *          ulTaskNotifyTake(pdPASS, portMAX_DELAY);
     *
     *          // Get the receivedAppPackets and get all the elements
     *          while (radio.getReceivedQueueSize() > 0) {
     *              //Get the first element inside the Received User Packets FiFo
     *              AppPacket<DataPacket>* packet = radio.getNextAppPacket<DataPacket>();
     *
     *              //Do something with the packet, ex: print(packetReceived);
     *
     *              //Then delete the packet
     *              radio.deletePacket(packet);
     *          }
     *      }
     * }
     * @endcode
     *
     *
     *   Then initialize:
     * @code
     *   LoraMesher radio = LoraMesher::getInstance();
     *   radio.begin();
     *   radio.start();
     * @endcode
     *
     *   Then for receiving app packets:
     * @code
     *   TaskHandle_t receiveLoRaMessage_Handle = NULL;
     *   int res = xTaskCreate(
     *       processReceivedPackets,
     *       "Receive User routine",
     *       4096,
     *       (void*) 1,
     *       2,
     *       &receiveLoRaMessage_Handle);
     *   if (res != pdPASS) {
     *       ESP_LOGE(LM_TAG, "Receive User Task creation gave error: %d", res);
     *   }
     *
     *   radio.setReceiveAppDataTaskHandle(receiveLoRaMessage_Handle);
     *
     * @endcode
     *
     * @ref RadioLib reference begin code
     */
    void begin(LoraMesherConfig config = LoraMesherConfig());

    /**
     * @brief Start/Resume LoRaMesher. After calling begin(...) or standby() you can Start/resume the LoRaMesher.
     * After Start/Resume LoRaMesher it will create and send a Routing Message. Do not abuse this function, it will prevent the duty cycle to function as intended.
     *
     */
    void start();

    /**
     * @brief Standby LoRaMesher. Including tasks and reception and send packets.
     *
     */
    void standby();

    /**
     * @brief Destroy the LoraMesher
     *
     */
    ~LoraMesher();

    /**
     * @brief Set a new LoRaMesher configuration. This function will stop the LoRaMesher and restart it with the new configuration.
     *
     */
    void setConfig(LoraMesherConfig config);

    /**
     * @brief Get the LoRaMesher configuration
     *
     * @return LoraMesherConfig
     */
    LoraMesherConfig getConfig() { return *loraMesherConfig; }

    /**
     * @brief Restart the radio module
     *
     */
    void restartRadio();

    /**
     * @brief Set the Frequency. Allowed values range from 137.0 MHz to 525.0 MHz.
     *
     * @param freq Frequency to be set in MHz
     */
    void setFrequency(float freq) { radio->setFrequency(freq); recalculateMaxTimeOnAir(); }

    /**
     * @brief Sets LoRa bandwidth. Allowed values are 10.4, 15.6, 20.8, 31.25, 41.7, 62.5, 125, 250 and 500 kHz.
     *
     * @param bw LoRa bandwidth to be set in kHz.
     */
    void setBandwidth(float bw) { radio->setBandwidth(bw); recalculateMaxTimeOnAir(); }

    /**
     * @brief Sets LoRa spreading factor. Allowed values range from 6 to 12.
     *
     * @param sf LoRa spreading factor to be set.
     */
    void setSpreadingFactor(uint8_t sf) { radio->setSpreadingFactor(sf); recalculateMaxTimeOnAir(); }

    /**
     * @brief Sets LoRa coding rate denominator. Allowed values range from 5 to 8.
     *
     * @param cr LoRa coding rate denominator to be set.
     */
    void setCodingRate(uint8_t cr) { radio->setCodingRate(cr); recalculateMaxTimeOnAir(); }

    /**
     * @brief Sets transmission output power. Allowed values range from -3 to 15 dBm (RFO pin) or +2 to +17 dBm (PA_BOOST pin).
     * High power +20 dBm operation is also supported, on the PA_BOOST pin.
     *
     * @param power Transmission output power in dBm.
     * @param useRfo Whether to use the RFO (true) or the PA_BOOST (false) pin for the RF output. Defaults to PA_BOOST.
     */
    void setOutputPower(int8_t power, bool useRfo = false) { radio->setOutputPower(power, useRfo); }

    /**
     * @brief Set the Receive App Data Task Handle, every time a received packet for this node is detected, this task will be notified.
     *
     * @param ReceiveAppDataTaskHandle Task handle which will be notified every time a packet for the application is detected.
     */
    void setReceiveAppDataTaskHandle(TaskHandle_t ReceiveAppDataTaskHandle) { ReceiveAppData_TaskHandle = ReceiveAppDataTaskHandle; }

    /**
     * @brief A copy of the routing table list. Delete it after using the list.
     *
     */
    LM_LinkedList<RouteNode>* routingTableListCopy() { return new LM_LinkedList<RouteNode>(*RoutingTableService::routingTableList); }

    /**
     * @brief Create a Packet And Send it
     *
     * @tparam T
     * @param dst Destination
     * @param payload Payload of type T
     * @param payloadSize Length of the payload in T
     */
    template <typename T>
    void createPacketAndSend(uint16_t dst, T* payload, uint8_t payloadSize) {
        //Cannot send an empty packet
        if (payloadSize == 0)
            return;

        //Get the size of the payload in bytes
        size_t payloadSizeInBytes = payloadSize * sizeof(T);

        ESP_LOGV(LM_TAG, "Creating a packet for send with %d bytes", payloadSizeInBytes);

        //Create a data packet with the payload
        DataPacket* dPacket = PacketService::createDataPacket(dst, getLocalAddress(), DATA_P, reinterpret_cast<uint8_t*>(payload), payloadSizeInBytes);

        //Create the packet and set it to the send queue
        setPackedForSend(reinterpret_cast<Packet<uint8_t>*>(dPacket), DEFAULT_PRIORITY);
    }

    /**
     * @brief Send the payload reliable.
     * It will wait for an ACK back from the destination to send the next packet.
     *
     * @param dst destination address
     * @param payload payload to send
     * @param payloadSize payload size to be send in Bytes
     */
    void sendReliablePacket(uint16_t dst, uint8_t* payload, uint32_t payloadSize);

    /**
     * @brief Send the payload reliable. It will wait for an ack of the destination.
     *
     * @tparam T
     * @param dst Destination
     * @param payload Payload of type T
     * @param payloadSize Length of the payload in T
     */
    template <typename T>
    void sendReliable(uint16_t dst, T* payload, uint32_t payloadSize) {
        sendReliablePacket(dst, reinterpret_cast<uint8_t*>(payload), sizeof(T) * payloadSize);
    }

    /**
     * @brief Returns the number of packets inside the received packets queue
     *
     * @return size_t Received Queue Size
     */
    size_t getReceivedQueueSize();

    /**
     * @brief Get the Send Queue Size. Packets that are waiting to be send.
     *
     * @return size_t Send Queue Size
     */
    size_t getSendQueueSize();

    /**
      * @brief Get the Next Application Packet
      *
      * @tparam T Type to be converted
      * @return AppPacket<T>*
      */
    template<typename T>
    AppPacket<T>* getNextAppPacket() {
        ReceivedAppPackets->setInUse();
        AppPacket<T>* appPacket = reinterpret_cast<AppPacket<T>*>(ReceivedAppPackets->Pop());
        ReceivedAppPackets->releaseInUse();
        return appPacket;
    }

    /**
     * @brief Delete the packet from memory
     *
     * @tparam T Type of packet
     * @param p Packet to delete
     */
    template <typename T>
    static void deletePacket(AppPacket<T>* p) {
        vPortFree(p);
    }

    /**
     * @brief Returns the routing table size
     *
     * @return size_t Routing Table Size
     */
    size_t routingTableSize();


    /**
     * @brief Get the Local Address
     *
     * @return uint16_t Address
     */
    uint16_t getLocalAddress();

    /**
     * @brief Get the Received Data Packets Num
     *
     * @return uint32_t
     */
    uint32_t getReceivedDataPacketsNum() { return receivedDataPacketsNum; }

    /**
     * @brief Get the Send Packets Num
     *
     * @return uint32_t
     */
    uint32_t getSendPacketsNum() { return sendPacketsNum; }

    /**
     * @brief Get the Received Hello Packets Num
     *
     * @return uint32_t
     */
    uint32_t getReceivedHelloPacketsNum() { return receivedHelloPacketsNum; }

    /**
     * @brief Get the Sent Hello Packets Num
     *
     * @return uint32_t
     */
    uint32_t getSentHelloPacketsNum() { return sentHelloPacketsNum; }

    /**
     * @brief Get the Received Broadcast Packets Num
     *
     * @return uint32_t
     */
    uint32_t getReceivedBroadcastPacketsNum() { return receivedBroadcastPacketsNum; }

    /**
     * @brief Get the Received Broadcast Packets Num
     *
     * @return uint32_t
     */
    uint32_t getForwardedPacketsNum() { return forwardedPacketsNum; }

    /**
     * @brief Get the Data Packets For Me Num
     *
     * @return uint32_t
     */
    uint32_t getDataPacketsForMeNum() { return dataPacketForMeNum; }

    /**
     * @brief Get the Received I Am Via Num
     *
     * @return uint32_t
     */
    uint32_t getReceivedIAmViaNum() { return receivedIAmViaNum; }

    /**
     * @brief Get the Destiny Unreachable Num
     *
     * @return uint32_t
     */
    uint32_t getDestinyUnreachableNum() { return sendPacketDestinyUnreachableNum; }

    /**
     * @brief Get the Received Not For Me
     *
     * @return uint32_t
     */
    uint32_t getReceivedNotForMe() { return receivedPacketNotForMeNum; }

    /**
     * @brief Get the payload received bytes
     *
     * @return uint32_t
     */
    uint32_t getReceivedPayloadBytes() { return receivedPayloadBytes; }

    /**
     * @brief Get the control received bytes
     *
     * @return uint32_t
     */
    uint32_t getReceivedControlBytes() { return receivedControlBytes; }

    /**
     * @brief Get the payload sent bytes
     *
     * @return uint32_t
     */
    uint32_t getSentPayloadBytes() { return sentPayloadBytes; }

    /**
     * @brief Get the control sent bytes
     *
     * @return uint32_t
     */
    uint32_t getSentControlBytes() { return sentControlBytes; }

    /**
     * @brief Defines that the node is a gateway
     *
     */
    static void addGatewayRole() { RoleService::setRole(ROLE_GATEWAY); };

    /**
     * @brief Defines that the node is not a gateway
     *
     */
    static void removeGatewayRole() { RoleService::removeRole(ROLE_GATEWAY); };

    /**
     * @brief Defines any type of Role
     *
     * @param role Role to be defined
     */
    static void addRole(uint8_t role) { RoleService::setRole(role); };

    /**
     * @brief Get the Nearest Gateway object
     *
     * @return RouteNode*
     */
    static RouteNode* getClosestGateway() { return RoutingTableService::getBestNodeByRole(ROLE_GATEWAY); };

    /**
     * @brief Get the Best Node With Role
     *
     * @param role Role to be searched
     * @return RouteNode* Route Node
     */
    static RouteNode* getBestNodeWithRole(uint8_t role) { return RoutingTableService::getBestNodeByRole(role); };

    /**
     * @brief Set the Simulator Service object
     *
     * @param service Simulator Service
     */
    void setSimulatorService(SimulatorService* service) { simulatorService = service; }

    /**
     * @brief Remove the Simulator Service object
     *
     */
    void removeSimulatorService() { simulatorService = nullptr; }

#ifndef LM_GOD_MODE
private:
#endif

    /**
     * @brief Construct a new LoraMesher object
     *
     */
    LoraMesher();

    /**
     * @brief Configuration of the LoRaMesher
     *
     */
    LoraMesherConfig* loraMesherConfig = new LoraMesherConfig();

    LM_LinkedList<AppPacket<uint8_t>>* ReceivedAppPackets = new LM_LinkedList<AppPacket<uint8_t>>();

    LM_LinkedList<QueuePacket<Packet<uint8_t>>>* ReceivedPackets = new LM_LinkedList<QueuePacket<Packet<uint8_t>>>();

    LM_LinkedList<QueuePacket<Packet<uint8_t>>>* ToSendPackets = new LM_LinkedList<QueuePacket<Packet<uint8_t>>>();

    /**
     * @brief RadioLib module
     *
     */
    LM_Module* radio = nullptr;

    /**
     * @brief Hello task handle. It will send a hello packet every HELLO_PACKETS_DELAY s
     *
     */
    TaskHandle_t Hello_TaskHandle = nullptr;

    /**
     * @brief Receive packets task handle. Every time a LoRa packet is detected it will create a packet,
     *  store it into the received packets queue and notify the receive data task handle
     *
     */
    TaskHandle_t ReceivePacket_TaskHandle = nullptr;

    /**
     * @brief Receive Data task handle. It will process all the packets inside the received packets queue.
     * It will be notified by the ReceivePacket_TaskHandle
     *
     */
    TaskHandle_t ReceiveData_TaskHandle = nullptr;

    /**
     * @brief Send data task handle. Every SEND_PACKETS_DELAY s it will get the packet from send packets queue,
     * add the via if it is necessary and send it.
     *
     */
    TaskHandle_t SendData_TaskHandle = nullptr;

    /**
     * @brief Receive App data task handle. Every time that a packet is received and is for this node, this task will be notified.
     * This task is implemented by the user.
     *
     */
    TaskHandle_t ReceiveAppData_TaskHandle = nullptr;

    /**
     * @brief Queue manager task handle. This task manages the queues inside LoRaMesher, checking for timeouts and resending messages.
     *
     */
    TaskHandle_t QueueManager_TaskHandle = nullptr;

    /**
     * @brief Routing table manager task handle. This manages the routing table, checking for timeouts and removing nodes.
     *
     */
    TaskHandle_t RoutingTableManager_TaskHandle = nullptr;

    void initConfiguration();

    static void onReceive(void);

    void setDioActionsForScanChannel();

    void setDioActionsForReceivePacket();

    void clearDioActions();

    int startReceiving();

    /**
     * @brief Scan activity channel
     *
     */
    void channelScan();

    int startChannelScan();

    void receivingRoutine();

    void initializeLoRa();

    void initializeSchedulers();

    void sendHelloPacket();

    void routingTableManager();

    void queueManager();

    /**
     * @brief Region Monitoring variables
     *
     */

    uint32_t receivedDataPacketsNum = 0;
    void incReceivedDataPackets() { receivedDataPacketsNum++; }

    uint32_t sendPacketsNum = 0;
    void incSendPackets() { sendPacketsNum++; }

    uint32_t receivedHelloPacketsNum = 0;
    void incRecHelloPackets() { receivedHelloPacketsNum++; }

    uint32_t sentHelloPacketsNum = 0;
    void incSentHelloPackets() { sentHelloPacketsNum++; }

    uint32_t receivedBroadcastPacketsNum = 0;
    void incReceivedBroadcast() { receivedBroadcastPacketsNum++; }

    uint32_t forwardedPacketsNum = 0;
    void incForwardedPackets() { forwardedPacketsNum++; }

    uint32_t dataPacketForMeNum = 0;
    void incDataPacketForMe() { dataPacketForMeNum++; }

    uint32_t receivedIAmViaNum = 0;
    void incReceivedIAmVia() { receivedIAmViaNum++; }

    uint32_t sendPacketDestinyUnreachableNum = 0;
    void incDestinyUnreachable() { sendPacketDestinyUnreachableNum++; }

    uint32_t receivedPacketNotForMeNum = 0;
    void incReceivedNotForMe() { receivedPacketNotForMeNum++; }

    uint32_t receivedPayloadBytes = 0;
    void incReceivedPayloadBytes(uint32_t numBytes) { receivedPayloadBytes += numBytes; }

    uint32_t receivedControlBytes = 0;
    void incReceivedControlBytes(uint32_t numBytes) { receivedControlBytes += numBytes; }

    uint32_t sentPayloadBytes = 0;
    void incSentPayloadBytes(uint32_t numBytes) { sentPayloadBytes += numBytes; }

    uint32_t sentControlBytes = 0;
    void incSentControlBytes(uint32_t numBytes) { sentControlBytes += numBytes; }

    /**
     * @brief Function that process the packets inside Received Packets
     * Task executed every time that a packet arrive.
     *
     */
    void processPackets();

    /**
     * @brief Delete the packet from memory
     *
     * @tparam T Type of packet
     * @param p Packet to delete
     */
    template <typename T>
    static void deletePacket(Packet<T>* p) {
        vPortFree(p);
    }

    /**
     * @brief Sets the packet in a Fifo with priority and will send the packet when needed.
     *
     * @param p packet<uint8_t>*
     * @param priority Priority set DEFAULT_PRIORITY by default. 0 most priority
     */
    void setPackedForSend(Packet<uint8_t>* p, uint8_t priority) {
        ESP_LOGI(LM_TAG, "Adding packet to Q_SP");
        QueuePacket<Packet<uint8_t>>* send = PacketQueueService::createQueuePacket(p, priority);
        ESP_LOGI(LM_TAG, "Created packet to Q_SP");
        addToSendOrderedAndNotify(send);
        //TODO: Using vTaskDelay to kill the packet inside LoraMesher
    }

    /**
     * @brief Add the Queue packet into the ToSendPackets and notify the SendData Task Handle
     *
     * @param qp
     */
    void addToSendOrderedAndNotify(QueuePacket<Packet<uint8_t>>* qp);

    /**
     * @brief Notify the QueueManager_TaskHandle that a new sequence has been started
     *
     */
    void notifyNewSequenceStarted();

    /**
     * @brief Process the data packet
     *
     * @param pq packet queue to be processed as data packet
     */
    void processDataPacket(QueuePacket<DataPacket>* pq);

    /**
     * @brief Process the data packet that destination is this node
     *
     * @param pq packet queue to be processed as data packet
     */
    void processDataPacketForMe(QueuePacket<DataPacket>* pq);

    /**
     * @brief Notifies the ReceivedUserData_TaskHandle that a packet has been arrived
     *
     * @param appPq App packet
     */
    void notifyUserReceivedPacket(AppPacket<uint8_t>* appPq);

    /**
     * @brief Send a packet through Lora
     *
     * @param p Packet to send
     * @return true has been send correctly
     * @return false has not been send
     */
    bool sendPacket(Packet<uint8_t>* p);

    /**
     * @brief Proccess that sends the data inside the FIFO
     *
     */
    void sendPackets();

    /**
     * @brief Send a packet to start the sequence of the packets
     *
     * @param destination destination address
     * @param seq_id Sequence Id
     * @param num_packets Number of packets of the sequence
     */
    void sendStartSequencePackets(uint16_t destination, uint8_t seq_id, uint16_t num_packets);


    /**
     * @brief Get the Start Sequence Packet Queue object
     *
     * @param destination destination address
     * @param seq_id Sequence Id
     * @param num_packets Number of packets of the sequence
     * @return QueuePacket<ControlPacket>*
     */
    QueuePacket<ControlPacket>* getStartSequencePacketQueue(uint16_t destination, uint8_t seq_id, uint16_t num_packets);

    /**
     * @brief Sends an ACK packet to the destination
     *
     * @param destination destination address
     * @param seq_id Id of the sequence
     * @param seq_num Number of the ack
     */
    void sendAckPacket(uint16_t destination, uint8_t seq_id, uint16_t seq_num);

    /**
     * @brief Send a lost packet
     *
     * @param destination destination address
     * @param seq_id Id of the sequence
     * @param seq_num Number of the lost packet
     */
    void sendLostPacket(uint16_t destination, uint8_t seq_id, uint16_t seq_num);

    /**
     * @brief Prints the header of the packet without the payload
     *
     * @param p packet to be printed
     * @param title Title to print the header
     */
    void printHeaderPacket(Packet<uint8_t>* p, String title);

    /**
     * @brief Process a large payload packet
     *
     * @param pq PacketQueue packet queue to be processed
     * @return true if processed correctly
     * @return false if not processed correctly
     */
    bool processLargePayloadPacket(QueuePacket<ControlPacket>* pq);

    /**
     * @brief Process a received synchronization packet
     *
     * @param source Source Id
     * @param seq_id Sequence Id
     * @param seq_num Sequence number
     */
    void processSyncPacket(uint16_t source, uint8_t seq_id, uint16_t seq_num);

    /**
     * @brief Add the ack number to the respectively sequence and reset the timeout numbers
     *
     * @param source Source of the packet
     * @param seq_id Sequence id of the packet
     * @param seq_num Sequence number that has been Acknowledged
     */
    void addAck(uint16_t source, uint8_t seq_id, uint16_t seq_num);

    /**
     * @brief Sequence Id, used to get the id of the packet sequence
     *
     */
    uint8_t sequence_id = 0;

    /**
     * @brief Get the Sequence Id for the packet sequence
     *
     * @return uint8_t
     */
    uint8_t getSequenceId();

    /**
     * @brief Manage all the packets inside the Q_WSP, checking for timeouts and erasing them if lost connection
     *
     */
    void managerSendQueue();

    /**
     * @brief Manage all the packets inside the Q_WRP, checking for timeouts and erasing them if lost connection
     *
     */
    void managerReceivedQueue();

    /**
     * @brief Used to set the configuration of the sequence of packets of the lists of packets
     *
     */
    struct sequencePacketConfig {
        //Identification is Sequence Id and Source address
        uint8_t seq_id; //Sequence Id
        uint16_t source; //Source Address

        uint16_t number{0}; //Number of packets of the sequence
        uint8_t firstAckReceived{0}; //If this value is set to 0, there has not been received any ack.
        uint16_t lastAck{0}; //Last ack received/send. 0 to n ACKs where n is the number of packets. 
        unsigned long timeout{0}; //Timeout of the sequence
        unsigned long previousTimeout{0}; //Previous timeout of the sequence
        uint8_t numberOfTimeouts{0}; //Number of timeouts that has been occurred
        unsigned long calculatingRTT{0}; // Calculating RTT
        RouteNode* node; //Node of the routing table sequence

        sequencePacketConfig(uint8_t seq_id, uint16_t source, uint16_t number, RouteNode* node): seq_id(seq_id), source(source), number(number), node(node) {};
    };

    /**
     * @brief List configuration
     *
     */
    struct listConfiguration {
        sequencePacketConfig* config;
        LM_LinkedList<QueuePacket<ControlPacket>>* list;
    };

    enum QueueType {
        WRP,
        WSP
    };

    void managerTimeouts(LM_LinkedList<listConfiguration>* queue, QueueType type);

    /**
     * @brief Actualize the RTT field
     *
     * @param config configuration to be actualized
     */
    void actualizeRTT(sequencePacketConfig* config);

    /**
     * @brief Send a packet of the sequence_id and sequence_num
     *
     * @param destination Destination to send the packet
     * @param seq_id sequence_id of the packet
     * @param seq_num number of the packet inside the sequence id
     * @return true If has been send
     * @return false If not
     */
    void processLostPacket(uint16_t destination, uint8_t seq_id, uint16_t seq_num);

    /**
     * @brief Send a packet of the sequence of the specific list configuration and sequence_num
     *
     * @param lstConfig List configuration
     * @param seq_num number of the packet inside the sequence id
     * @return true If has been send
     * @return false If not
     */
    bool sendPacketSequence(listConfiguration* lstConfig, uint16_t seq_num);

    /**
     * @brief Join all the packets inside the list configuration and notify the user
     *
     * @param listConfig list configuration to join
     */
    void joinPacketsAndNotifyUser(listConfiguration* listConfig);

    /**
     * @brief If executed it will reset the number of timeouts to 0 and reset the timeout
     *
     * @param queue queue to be found the sequence
     * @param seq_id sequence id
     * @param source source address
     */
    void addTimeout(LM_LinkedList<listConfiguration>* queue, uint8_t seq_id, uint16_t source);

    /**
     * @brief If executed it will reset the number of timeouts to 0 and reset the timeout
     *
     * @param configPacket configuration packet to be modified
     */
    void resetTimeout(sequencePacketConfig* configPacket);

    /**
     * @brief Get the Maximum Timeout object
     *
     * @param configPacket configuration packet to be calculated
     * @return unsigned long maximum timeout
     */
    unsigned long getMaximumTimeout(sequencePacketConfig* configPacket);

    /**
     * @brief Calculate the timeout of the sequence
     *
     * @param configPacket configuration packet to be calculated
     * @return unsigned long timeout
     */
    unsigned long calculateTimeout(sequencePacketConfig* configPacket);

    /**
     * @brief Adds the micros() + default timeout to the config packet
     *
     * @param configPacket config packet to be modified
     */
    void addTimeout(sequencePacketConfig* configPacket);

    /**
     * @brief Recalculate the timeout after a timeout
     *
     * @param configPacket config packet to be modified
     */
    void recalculateTimeoutAfterTimeout(sequencePacketConfig* configPacket);

    /**
     * @brief Clear the Linked List deleting all the elements inside
     *
     * @param list List to be cleared
     */
    void clearLinkedList(listConfiguration* listConfig);

    /**
     * @brief Find the listConfig element inside the queue and delete the element from the list
     *
     * @param queue Queue where to find and delete the element
     * @param listConfig Element that needs to be found in the queue
     */
    void findAndClearLinkedList(LM_LinkedList<listConfiguration>* queue, listConfiguration* listConfig);

    /**
     * @brief With a sequence id and a linkedList, it will find the first sequence inside a queue that have the sequence id
     *
     * @param queue Queue to find the sequence id
     * @param seq_id Sequence id to find
     * @param source Source of the list
     * @return listConfiguration*
     */
    listConfiguration* findSequenceList(LM_LinkedList<listConfiguration>* queue, uint8_t seq_id, uint16_t source);

    /**
     * @brief Queue Waiting Sending Packets (Q_WSP)
     * List pairs (sequencePacketConfig defines the configuration of the following packets, id and number of packets,
     * then there is another linked list with all the packets of the sequence)
     *
     */
    LM_LinkedList<listConfiguration>* q_WSP = new LM_LinkedList<listConfiguration>();

    /**
     * @brief Queue Waiting Received Packet (Q_WRP)
     *
     */
    LM_LinkedList<listConfiguration>* q_WRP = new LM_LinkedList<listConfiguration>();

    /**
     * @brief Max time on air for a given configuration in ms
     *
     */
    uint32_t maxTimeOnAir = 0;

    /**
     * @brief Wait before sending function
     *
     * @param repeatedDetectPreambles Number of repeated detected preambles
     */
    void waitBeforeSend(uint8_t repeatedDetectPreambles);

    /**
     * @brief Max propagation time for a given configuration in ms
     * @return uint32_t Max propagation time
     */
    uint32_t getMaxPropagationTime();

    /**
     * @brief Get the Propagation Time With Random ms
     * @param multiplayer Multiplayer of the random
     *
     * @return uint32_t Propagation time with random
     */
    uint32_t getPropagationTimeWithRandom(uint8_t multiplayer);

    /**
     * @brief Max Time on air for a given configuration, Used for time slots
     *
     */
    void recalculateMaxTimeOnAir();

    /**
     * @brief Has received a Message when scanning channels
     *
     */
    bool hasReceivedMessage = false;

    /** @brief Get the Simulator Service object
     *
     * @return SimulatorService*
     */
    SimulatorService* simulatorService = nullptr;

    /**
     * @brief Record the state of the LoRaMesher
     *
     * @param type Type of the state
     * @param packet Packet to be recorded
     */
    void recordState(LM_StateType type, Packet<uint8_t>* packet = nullptr);

#ifdef LM_TESTING
    /**
     * @brief Returns if the packet can be received. Only for testing
     *
     * @param source The source of the packet
     * @return true The packet can be received
     * @return false The packet will be dropped
     */
    bool canReceivePacket(uint16_t source);

    /**
     * @brief Returns if is a data packet and the via is the local address. Only for testing
     *
     * @param packet Packet to be checked
     * @param localAddress Local address
     * @return true Is a data packet and the via is the local address
     */
    bool isDataPacketAndLocal(DataPacket* packet, uint16_t localAddress);

    /**
     * @brief Returns if the packet should be processed. Only for testing
     *
     * @param packet Packet to be checked
     * @return true
     * @return false
     */
    bool shouldProcessPacket(Packet<uint8_t>* packet);

#endif


public:

    /**
     * @brief Has active sent connections, Q_WSP greater than 0
     *
     * @return true if has active sent connections
     * @return false if not
     */
    bool hasActiveSentConnections() { return q_WSP->getLength() > 0; }

    /**
     * @brief Has active received connections, Q_WRP greater than 0
     *
     * @return true if has active received connections
     * @return false if not
     */
    bool hasActiveReceivedConnections() { return q_WRP->getLength() > 0; }

    /**
     * @brief Returns if there are active connections, Q_WRP or Q_WSP or Queue ToSendPackets or Queue ReceivedPackets greater than 0
     *
     * @return true
     * @return false
     */
    bool hasActiveConnections() { return hasActiveReceivedConnections() || hasActiveSentConnections() || ToSendPackets->getLength() > 0 || ReceivedPackets->getLength() > 0; };

    /**
     * @brief Returns the number of packets inside the waiting send packets queue
     *
     * @return size_t number of packets
     */
    size_t queueWaitingSendPacketsLength() { return q_WSP->getLength(); }

    /**
     * @brief Returns the number of packets inside the waiting received packets queue
     *
     * @return size_t number of packets
     */
    size_t queueWaitingReceivedPacketsLength() { return q_WRP->getLength(); }
};

#endif