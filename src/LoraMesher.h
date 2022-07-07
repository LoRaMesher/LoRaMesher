#ifndef _LORAMESHER_H
#define _LORAMESHER_H

#include <ArduinoLog.h>

// LoRa libraries
#include "RadioLib.h"

// Logger
#include <ArduinoLog.h>
//#define DISABLE_LOGGING

//Actual LoRaMesher Libraries
#include "BuildOptions.h"

#include "utilities/LinkedQueue.hpp"

#include "services/PacketService.h"

#include "services/RoutingTableService.h"

#include "services/PacketQueueService.h"

#include "services/WiFiService.h"

class LoraMesher {

public:
    static LoraMesher& getInstance() {
        static LoraMesher instance;
        return instance;
    }

    /**
     * @brief Initialize the LoraMesher object
     *
     * @param receiverFunction Receiver function. It will be notified when data for the user is available.
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
     *   radio.init(processReceivedPackets);
     * @endcode
     */
    void init(void (*receiverFunction)(void*));

    /**
     * @brief Destroy the LoraMesher
     *
     */
    ~LoraMesher();

    /**
     * @brief Routing table List
     *
     */
    LM_LinkedList<RouteNode>* routingTableList() { return RoutingTableService::routingTableList; }

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
     * @return size_t
     */
    size_t getReceivedQueueSize();

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
        delete p;
    }

    /**
     * @brief Returns the routing table size
     *
     * @return int
     */
    int routingTableSize();


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

private:

    /**
     * @brief Construct a new LoraMesher object
     *
     */
    LoraMesher();

    LM_LinkedList<AppPacket<uint8_t>>* ReceivedAppPackets = new LM_LinkedList<AppPacket<uint8_t>>();

    LM_LinkedList<QueuePacket<Packet<uint8_t>>>* ReceivedPackets = new LM_LinkedList<QueuePacket<Packet<uint8_t>>>();

    LM_LinkedList<QueuePacket<Packet<uint8_t>>>* ToSendPackets = new LM_LinkedList<QueuePacket<Packet<uint8_t>>>();

    /**
     * @brief Routable node timeout (µs)
     *
     */
    unsigned long routeTimeout;

    /**
     * @brief RadioLib module
     *
     */
    SX1276* radio;

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
     * @brief Received user data task handle. Every time the receive data task handle found a data packet for the user it will be notified.
     * This function is implemented by the user.
     *
     */
    TaskHandle_t ReceivedUserData_TaskHandle = nullptr;

    /**
     * @brief Packet manager task handle. This manages all the routing table and large and reliable payloads, checking for timeouts and resending messages.
     *
     */
    TaskHandle_t PacketManager_TaskHandle = nullptr;

    static void onReceive(void);

    void receivingRoutine();

    void initializeLoRa();

    void initializeScheduler(void (*func)(void*));

    void sendHelloPacket();

    void packetManager();


    /**
     * @brief Region Monitoring variables
     *
     */

    uint32_t receivedDataPacketsNum = 0;
    void incReceivedDataPackets() { receivedDataPacketsNum++; }//minetwork -> incReceivedPackets();

    uint32_t sendPacketsNum = 0;
    void incSendPackets() { sendPacketsNum++; }//

    uint32_t receivedHelloPacketsNum = 0;
    void incRecHelloPackets() { receivedHelloPacketsNum++; }//

    uint32_t sentHelloPacketsNum = 0;
    void incSentHelloPackets() { sentHelloPacketsNum++; }//

    uint32_t receivedBroadcastPacketsNum = 0;
    void incReceivedBroadcast() { receivedBroadcastPacketsNum++; }//

    uint32_t forwardedPacketsNum = 0;
    void incForwardedPackets() { forwardedPacketsNum++; }//

    uint32_t dataPacketForMeNum = 0;
    void incDataPacketForMe() { dataPacketForMeNum++; }//

    uint32_t receivedIAmViaNum = 0;
    void incReceivedIAmVia() { receivedIAmViaNum++; }//

    uint32_t sendPacketDestinyUnreachableNum = 0;
    void incDestinyUnreachable() { sendPacketDestinyUnreachableNum++; }//

    uint32_t receivedPacketNotForMeNum = 0;
    void incReceivedNotForMe() { receivedPacketNotForMeNum++; }


    /**
     * @brief End region Monitoring variables
     *
     */




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
        free(p);
    }

    /**
     * @brief Sets the packet in a Fifo with priority and will send the packet when needed.
     *
     * @param p packet<uint8_t>*
     * @param priority Priority set DEFAULT_PRIORITY by default. 0 most priority
     */
    void setPackedForSend(Packet<uint8_t>* p, uint8_t priority) {
        Log.traceln(F("Adding packet to Q_SP"));
        QueuePacket<Packet<uint8_t>>* send = PacketQueueService::createQueuePacket(p, priority);
        Log.traceln(F("Created packet to Q_SP"));
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
        unsigned long timeout{0};; //Timeout of the sequence
        uint8_t numberOfTimeouts{0}; //Number of timeouts that has been occurred
        uint32_t RTT{0}; //Round Trip time

        sequencePacketConfig(uint8_t seq_id, uint16_t source, uint16_t number) : seq_id(seq_id), source(source), number(number) {};
    };

    /**
     * @brief List configuration
     *
     */
    struct listConfiguration {
        sequencePacketConfig* config;
        LM_LinkedList<QueuePacket<ControlPacket>>* list;
    };

    /**
     * @brief Actualize the RTT field
     *
     * @param config configuration to be actualized
     */
    void actualizeRTT(listConfiguration* config);

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
     * @brief Adds the micros() + default timeout to the config packet
     *
     * @param configPacket config packet to be modified
     */
    void addTimeout(sequencePacketConfig* configPacket);

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
     * @brief Flag used when trying to send to detect a packet preamble.
     *
     */
    uint8_t preambleReceivedWhenSending = 0;

    /**
     * @brief Wait before sending function
     *
     * @param repeatedDetectPreambles Number of repeated detected preambles
     */
    void waitBeforeSend(uint8_t repeatedDetectPreambles);

    /**
     * @brief Max Time on air for a given configuration, Used for time slots
     *
     */
    void recalculateMaxTimeOnAir();

};

#endif