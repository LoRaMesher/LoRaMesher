#ifndef __LORAMESHER__
#define __LORAMESHER__

// LoRa libraries
#include "RadioLib.h"

// WiFi libraries
#include <WiFi.h>

#include "helpers/LinkedQueue.hpp"

// Logger
//#define DISABLE_LOGGING
#include <ArduinoLog.h>

// LoRa band definition
// 433E6 for Asia
// 866E6 for Europe
// 915E6 for North America
#define BAND 868.0F
#define BANDWIDTH 125.0F
#define LORASF 7U // Spreading factor 6-12 (default 7)

// Comment this line if you want to remove the crc for each packet
#define ADDCRC_PAYLOAD

// Routing table max size
#define RTMAXSIZE 256

//MAX packet size per packet
//If exceed it will be automatically separated through multiple packets 
//In bytes (226 bytes [UE max allowed with SF7 and 125khz])
//MAX payload size for hello packets = MAXPACKETSIZE - 6 bytes of header
//MAX payload size for data packets = MAXPACKETSIZE - 6 bytes of header - 2 bytes of via
//MAX payload size for reliable and large packets = MAXPACKETSIZE - 6 bytes of header - 2 bytes of via - 3 of control packet
#define MAXPACKETSIZE 222

// Packet types
#define NEED_ACK_P 0b00000001
#define DATA_P     0b00000010
#define HELLO_P    0b00000100
#define ACK_P      0b00001000
#define XL_DATA_P  0b00010000
#define LOST_P     0b00100000
#define SYNC_P     0b01000000

// Packet configuration
#define BROADCAST_ADDR 0xFFFF
#define DEFAULT_PRIORITY 20
#define MAX_PRIORITY 40

//Definition Times in seconds
#define DEFAULT_TIMEOUT 60
#define HELLO_PACKETS_DELAY 180
#define SEND_PACKETS_DELAY 60

//Maximum times that a sequence of packets reach the timeout
#define MAX_TIMEOUTS 3
#define MAX_RESEND_PACKET 3

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
     *          // Get the receivedUserPackets and get all the elements
     *          while (radio.getReceivedQueueSize() > 0) {
     *              //Get the first element inside the Received User Packets FiFo
     *              LoraMesher::userPacket<dataPacket>* packet = radio.getNextUserPacket<dataPacket>();
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

#pragma pack(push, 1)
    /**
     * @brief User packet, it is used to send the packet to the user
     *
     * @tparam T
     */
    template <typename T>
    struct userPacket {
        /**
         * @brief Destination address, normally it will be local address or BROADCAST_ADDR
         *
         */
        uint16_t dst;

        /**
         * @brief Source Address
         *
         */
        uint16_t src;

        /**
         * @brief Payload Size in bytes
         *
         */
        uint32_t payloadSize = 0;

        /**
         * @brief Payload Array
         *
         */
        T payload[];
    };
#pragma pack(pop)

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

        //Create the packet and set it to the send queue
        setPackedForSend(createPacket(dst, getLocalAddress(), DATA_P, payload, payloadSizeInBytes), DEFAULT_PRIORITY);
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
        sendReliablePacket(dst, (uint8_t*) payload, sizeof(T) * payloadSize);
    }

    /**
     * @brief Returns the number of packets inside the received packets queue
     *
     * @return size_t
     */
    size_t getReceivedQueueSize();

    /**
      * @brief Get the Next User Packet
      *
      * @tparam T Type to be converted
      * @return userPacket<T>*
      */
    template<typename T>
    userPacket<T>* getNextUserPacket() {
        packetQueue<userPacket<T>>* pq = ReceivedUserPackets->Pop<userPacket<T>>();
        userPacket<T>* packet = pq->packet;

        deletePacketQueue(pq);

        return packet;
    }

    /**
     * @brief Get the payload length in number of T
     *
     * @tparam T Type to be converted
     * @param p user packet
     * @return size_t size in number of T
     */
    template <typename T>
    size_t getPayloadLength(userPacket<T>* p) {
        return p->payloadSize / sizeof(T);
    }

    /**
     * @brief Delete the packet from memory
     *
     * @tparam T Type of packet
     * @param p Packet to delete
     */
    template <typename T>
    static void deletePacket(userPacket<T>* p) {
        Log.traceln(F("Deleting user packet"));
        free(p);
    }

#pragma pack(push, 1)
    /**
     * @brief Network node, used to send and store the routing table
     *
     */
    struct networkNode {
        /**
         * @brief Address
         *
         */
        uint16_t address = 0;

        /**
         * @brief Metric, how many hops to reach the previous address
         *
         */
        uint8_t metric = 0;
    };

    /**
     * @brief Routable node, store the network node, via and timeout of this route
     *
     */
    struct routableNode {
        /**
         * @brief Network node
         *
         */
        LoraMesher::networkNode networkNode;

        /**
         * @brief Timeout of the route
         *
         */
        unsigned long timeout = 0;

        /**
         * @brief Next hop to send the message
         *
         */
        uint16_t via = 0;
    };
#pragma pack(pop)

    /**
     * @brief Routing table array
     *
     */
    routableNode routingTable[RTMAXSIZE];

    /**
     * @brief Prints the actual routing table in the log
     *
     */
    void printRoutingTable();

    /**
     * @brief Returns the routing table size
     *
     * @return int
     */
    int routingTableSize();

    /**
     * @brief Returns if address is inside the routing table
     *
     * @param address Address you want to check if is inside the routing table
     * @return true If the address is inside the routing table
     * @return false If the address is not inside the routing table
     */
    bool hasAddressRoutingTable(uint16_t address);

    /**
     * @brief Get the Next Hop address
     *
     * @param dst address of the next hop
     * @return uint16_t address of the next hop
     */
    uint16_t getNextHop(uint16_t dst);

    /**
     * @brief Get the Local Address
     *
     * @return uint16_t Address
     */
    uint16_t getLocalAddress();


private:

    /**
     * @brief Construct a new LoraMesher object
     *
     */
    LoraMesher();

    /**
     * @brief Local Address
     *
     */
    uint16_t localAddress;

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

    void initializeLocalAddress();

    void initializeLoRa();

    void initializeScheduler(void (*func)(void*));

    void sendHelloPacket();

    void packetManager();

    /**
     * @brief Function that process the packets inside Received Packets
     * Task executed every time that a packet arrive.
     *
     */
    void processPackets();

#pragma pack(push, 1)
    template <typename T>
    struct packet {
        uint16_t dst;
        uint16_t src;
        uint8_t type;
        //TODO: REMOVE THIS ID
        uint8_t id;
        uint8_t payloadSize = 0;
        T payload[];
    };

    template <typename T>
    struct dataPacket {
        uint16_t via;
        T payload[];
    };

    template <typename T>
    struct controlPacket {
        uint8_t seq_id;
        uint16_t number;
        T payload[];
    };

#pragma pack(pop)


    /**
     * @brief Get the Payload of the packet
     *
     * @tparam T type of the payload
     * @param packet Packet that you want to get the payload
     * @return T* pointer of the packet payload
     */
    template <typename T>
    T* getPayload(packet<T>* packet) {
        return (T*) ((unsigned long) packet->payload + getExtraLengthToPayload(packet->type));
    }

    /**
     * @brief Create a Packet<T>
     *
     * @tparam T
     * @param dst Destination
     * @param src Source, normally local address, use getLocalAddress()
     * @param type Type of packet
     * @param payload Payload of type T
     * @param payloadSize Length of the payload in bytes
     * @return struct packet<uint8_t>*
     */
    template <typename T>
    struct packet<uint8_t>* createPacket(uint16_t dst, uint16_t src, uint8_t type, T* payload, uint8_t payloadSize) {
        uint8_t extraSize = getExtraLengthToPayload(type);

        packet<uint8_t>* p = createPacket((uint8_t*) payload, payloadSize, extraSize);
        p->dst = dst;
        p->type = type;
        p->src = src;

        return p;
    }

    /**
     * @brief Create a User Packet
     *
     * @param dst destination address
     * @param src source address
     * @param payload payload array
     * @param payloadSize payload size in bytes
     * @return struct userPacket<uint8_t>*
     */
    struct userPacket<uint8_t>* createUserPacket(uint16_t dst, uint16_t src, uint8_t* payload, uint32_t payloadSize);

    /**
     * @brief given a Packet<uint8_t> it will be converted to a userPacket
     *
     * @param p packet of type packet<uint8_t>
     * @return struct userPacket<uint8_t>*
     */
    struct userPacket<uint8_t>* convertPacket(packet<uint8_t>* p);

    /**
     * @brief Create a Packet<uint8_t>
     *
     * @param payload Payload
     * @param payloadSize Length of the payload in bytes
     * @param extraSize Indicates the function that it need to allocate extra space before the payload
     * @return struct packet<uint8_t>*
     */
    struct packet<uint8_t>* createPacket(uint8_t* payload, uint8_t payloadSize, uint8_t extraSize);

    /**
     * @brief Create an Empty Packet
     *
     * @param packetSize Packet size in bytes
     * @return struct packet<uint8_t>*
     */
    struct packet<uint8_t>* createEmptyPacket(size_t packetSize);

    /**
     * @brief Copy a packet
     *
     * @param p packet to be copied
     * @return struct packet<uint8_t>*
     */
    struct packet<uint8_t>* copyPacket(packet<uint8_t>* p);

    /**
     * @brief Delete the packet from memory
     *
     * @tparam T Type of packet
     * @param p Packet to delete
     */
    template <typename T>
    static void deletePacket(packet<T>* p) {
        Log.traceln(F("Deleting packet"));
        free(p);
    }

    /**
     * @brief Sets the packet in a Fifo with priority and will send the packet when needed.
     *
     * @tparam T
     * @param p packet of type T
     * @param priority Priority set DEFAULT_PRIORITY by default. 0 most priority
     */
    template <typename T>
    void setPackedForSend(packet<T>* p, uint8_t priority) {
        packetQueue<uint8_t>* send = createPacketQueue(p, priority);
        ToSendPackets->Add(send);
        //TODO: Using vTaskDelay to kill the packet inside LoraMesher
    }

    /**
     * @brief Get the payload in number of elements
     *
     * @tparam T
     * @param p
     * @return size_t
     */
    template<typename T>
    size_t getPayloadLength(packet<T>* p) {
        return (p->payloadSize - getExtraLengthToPayload(p->type)) / sizeof(T);
    }

    /**
     * @brief packetQueue template
     *
     * @tparam T
     */
    template <typename T>
    struct packetQueue {
        uint16_t number{0};
        uint8_t priority = DEFAULT_PRIORITY;
        T* packet;
        packetQueue<T>* next;
    };

    /**
     * @brief It will delete the packet queue and the packet inside it
     *
     * @tparam T Type of the packet queue
     * @param pq packet queue to be deleted
     */
    template <typename T>
    static void deletePacketQueueAndPacket(packetQueue<T>* pq) {
        deletePacket((packet<T>*) pq->packet);
        deletePacketQueue(pq);
    }

    /**
     * @brief It will delete the packet queue
     *
     * @tparam T Type of packetQueue
     * @param pq Packet queue to be deleted
     */
    template <typename T>
    static void deletePacketQueue(packetQueue<T>* pq) {
        Log.traceln(F("Deleting packet queue"));
        delete pq;
    }

    /**
     * @brief Create a Packet Queue element
     *
     * @tparam T type of the packet queue
     * @tparam I type of the packet
     * @param p packet
     * @param priority priority inside the queue
     * @param number Number of the sequence
     * @return packetQueue<T>*
     */
    template <typename T>
    packetQueue<uint8_t>* createPacketQueue(T* p, uint8_t priority, uint16_t number = 0) {
        packetQueue<uint8_t>* pq = new packetQueue<uint8_t>();
        pq->priority = priority;
        pq->packet = (uint8_t*) p;
        pq->next = nullptr;
        pq->number = number;
        return pq;
    }

    class PacketQueue {
    public:
        /**
         * @brief Add pq in order of priority
         *
         * @param pq
         */
        void Add(packetQueue<uint8_t>* pq);

        /**
         * @brief Get the first packetQueue of the list and deletes it from the list
         *
         * @tparam T
         * @return packetQueue<T>*
         */
        template <typename T>
        packetQueue<T>* Pop() {
            WaitAndDisable();

            if (first == nullptr) {
                Enable();
                return nullptr;
            }

            packetQueue<T>* firstCp = (packetQueue<T>*) first;
            first = first->next;

            Enable();
            return firstCp;
        }

        /**
         * @brief Returns the size of the queue
         *
         * @return size_t
         */
        size_t Size();

        /**
         * @brief Clear the packetQueueFifo, deleting all the packets inside too.
         *
         */
        void Clear();

    private:
        bool* enabled = new bool(true);
        packetQueue<uint8_t>* first = nullptr;

        /**
         * @brief Wait for the enabled flag to be true, set it to false then
         *
         */
        void WaitAndDisable();

        /**
         * @brief Set the enabled flag to true
         *
         */
        void Enable();
    };

    PacketQueue* ReceivedUserPackets = new PacketQueue();

    PacketQueue* ReceivedPackets = new PacketQueue();

    PacketQueue* ToSendPackets = new PacketQueue();


    /**
     * @brief process the network node, adds the node in the routing table if can
     *
     * @param via via address
     * @param node networkNode
     */
    void processRoute(uint16_t via, networkNode* node);

    /**
     * @brief Process the network packet
     *
     * @param p Packet of type networkNode
     */
    void processRoute(packet<networkNode>* p);

    /**
     * @brief Process the data packet
     *
     * @param pq packet queue to be processed as data packet
     */
    void processDataPacket(packetQueue<packet<dataPacket<uint8_t>>>* pq);

    /**
     * @brief Process the data packet that destination is this node
     *
     * @param pq packet queue to be processed as data packet
     */
    void processDataPacketForMe(packetQueue<packet<dataPacket<uint8_t>>>* pq);

    /**
     * @brief Notifies the ReceivedUserData_TaskHandle that a packet has been arrived
     *
     */
    void notifyUserReceivedPacket(packetQueue<userPacket<uint8_t>>* pq);

    /**
     * @brief Add node to the routing table
     *
     * @param node Network node that includes the address and the metric
     * @param via Address to next hop to reach the network node address
     */
    void addNodeToRoutingTable(networkNode* node, uint16_t via);

    /**
     * @brief Create a Routing Packet adding the routing table to the payload
     *
     * @return struct packet*
     */
    struct packet<uint8_t>* createRoutingPacket();

    /**
     * @brief Send a packet through Lora
     *
     * @param p Packet to send
     * @return true has been send correctly
     * @return false has not been send
     */
    bool sendPacket(packet<uint8_t>* p);

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
     * @return packetQueue<uint8_t>*
     */
    packetQueue<uint8_t>* getStartSequencePacketQueue(uint16_t destination, uint8_t seq_id, uint16_t num_packets);

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
     * @brief Get the Packet Length
     *
     * @tparam T
     * @param p Packet of Type T
     * @return size_t Packet size in bytes
     */
    template <typename T>
    size_t getPacketLength(packet<T>* p) { return sizeof(LoraMesher::packet<T>) + p->payloadSize; }

    /**
     * @brief Get the number of bytes to the payload, between a Packet<T> and their real payload
     *
     * @param type type of the packet
     * @return size_t number of bytes
     */
    uint8_t getExtraLengthToPayload(uint8_t type);

    /**
     * @brief Get the Maximum Payload Length with a MAXPACKETSIZE defined
     *
     * @param type
     * @return uint8_t
     */
    uint8_t getMaximumPayloadLength(uint8_t type);

    /**
     * @brief Get the Packet Payload Length in bytes
     *
     * @tparam T Type of packets
     * @param p packet to know the payload length in bytes
     * @return size_t The payload length in bytes of the packet
     */
    template <typename T>
    size_t getPacketPayloadLength(packet<T>* p) { return p->payloadSize - getExtraLengthToPayload(p->type); }

    /**
     * @brief Given a type returns if needs a data packet
     *
     * @param type type of the packet
     * @return true True if needed
     * @return false If not
     */
    bool hasDataPacket(uint8_t type);

    /**
     * @brief Given a type returns if needs a control packet
     *
     * @param type type of the packet
     * @return true True if needed
     * @return false If not
     */
    bool hasControlPacket(uint8_t type);

    /**
     * @brief Print the packet in the Log verbose, if the type is not defined it will print the payload in Hexadecimals
     *
     * @tparam T
     * @param p Packet of Type T
     * @param received If true it will print received, else will print Created
     */
    template <typename T>
    void printPacket(packet<T>* p, bool received) {
        Log.verboseln(F("-----------------------------------------"));
        Log.verboseln(F("Current Packet: %s"), received ? "Received" : "Created");
        Log.verboseln(F("Destination: %X"), p->dst);
        Log.verboseln(F("Source: %X"), p->src);
        Log.verboseln(F("Type: %d"), p->type);
        Log.verboseln(F("----Packet of size %d bytes----"), getPacketLength(p));
        switch (p->type) {
            case HELLO_P:
                for (int i = 0; i < getPayloadLength((packet<networkNode>*) p); i++)
                    Log.verboseln(F("%d ->(%u) Address: %X - Metric: %d"), i, &p->payload[i], ((networkNode*) &p->payload[i])->address, ((networkNode*) &p->payload[i])->metric);

        }

        Log.verboseln(F("-----------------------------------------"));
    }

    /**
     * @brief Prints the header of the packet without the payload
     *
     * @param p packet to be printed
     * @param title Title to print the header
     */
    void printHeaderPacket(packet<uint8_t>* p, String title);

    /**
     * @brief Send a packet of the sequence_id and sequence_num
     *
     * @param destination Destination to send the packet
     * @param seq_id sequence_id of the packet
     * @param seq_num number of the packet inside the sequence id
     * @return true If has been send
     * @return false If not
     */
    bool sendPacketSequence(uint16_t destination, uint8_t seq_id, uint16_t seq_num);

    /**
     * @brief Process a large payload packet
     *
     * @param pq PacketQueue packet queue to be processed
     */
    void processLargePayloadPacket(packetQueue<packet<dataPacket<uint8_t>>>* pq);

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

        uint16_t number; //Number of packets of the sequence
        uint8_t firstAckReceived{0}; //If this value is set to 0, there has not been received any ack.
        uint16_t lastAck{0}; //Last ack received/send. 0 to n ACKs where n is the number of packets. 
        unsigned long timeout{0};; //Timeout of the sequence
        uint32_t numberOfTimeouts{0}; //Number of timeouts that has been occurred

        sequencePacketConfig(uint8_t seq_id, uint16_t source, uint16_t number) : seq_id(seq_id), source(source), number(number) {};
    };

    /**
     * @brief List configuration
     *
     */
    struct listConfiguration {
        sequencePacketConfig* config;
        LM_LinkedList<packetQueue<uint8_t>>* list;
    };

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
    void resetTimeout(LM_LinkedList<listConfiguration>* queue, uint8_t seq_id, uint16_t source);

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
    void addTimeoutTime(sequencePacketConfig* configPacket);

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
     * @brief Find the packet queue inside the list of packet queues
     *
     * @param queue Queue to find the packet queue
     * @param num Number of the sequence of the packet queue
     * @return packetQueue<uint8_t>*
     */
    packetQueue<uint8_t>* findPacketQueue(LM_LinkedList<packetQueue<uint8_t>>* queue, uint8_t num);

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

};

#endif