#ifndef _LORAMESHER_ROUTE_UPDATE_SERVICE_H
#define _LORAMESHER_ROUTE_UPDATE_SERVICE_H

#include "BuildOptions.h"

/**
 * @brief Packet identifier for duplicate detection
 * Tracks (sourceAddress, packetId) pairs to prevent routing loops
 */
struct PacketIdentifier {
    uint16_t sourceAddress;
    uint8_t packetId;
    unsigned long timestamp;

    PacketIdentifier(): sourceAddress(0), packetId(0), timestamp(0) {}

    PacketIdentifier(uint16_t src, uint8_t id):
        sourceAddress(src), packetId(id), timestamp(millis()) {}
};

/**
 * @brief Per-route cooldown tracking
 * Prevents rapid triggered updates for the same destination
 */
struct RouteCooldown {
    uint16_t address;
    unsigned long lastUpdate;

    RouteCooldown(): address(0), lastUpdate(0) {}

    RouteCooldown(uint16_t addr):
        address(addr), lastUpdate(millis()) {}
};

/**
 * @brief Route Update Service
 * Manages triggered routing updates with duplicate detection and storm prevention
 */
class RouteUpdateService {
public:
    /**
     * @brief Initialize the route update service
     * Must be called once at startup
     */
    static void init();

    /**
     * @brief Check if a packet is a duplicate
     *
     * @param sourceAddress Source address of the packet
     * @param packetId Packet ID
     * @return true If packet was already seen recently
     * @return false If packet is new
     */
    static bool isDuplicatePacket(uint16_t sourceAddress, uint8_t packetId);

    /**
     * @brief Record a packet as seen
     * Adds (sourceAddress, packetId) to the duplicate detection cache
     *
     * @param sourceAddress Source address of the packet
     * @param packetId Packet ID
     */
    static void recordPacket(uint16_t sourceAddress, uint8_t packetId);

    /**
     * @brief Check if a triggered update should be sent (rate limiting)
     *
     * @return true If sufficient time has passed since last triggered update
     * @return false If rate limit prevents sending
     */
    static bool shouldSendTriggeredUpdate();

    /**
     * @brief Record that a triggered update was sent
     * Updates rate limiting timestamp and statistics
     */
    static void recordTriggeredUpdate();

    /**
     * @brief Check if a specific route can trigger an update (per-route cooldown)
     *
     * @param address Address of the route that changed
     * @return true If route can trigger an update
     * @return false If route is in cooldown period
     */
    static bool canRouteTriggerUpdate(uint16_t address);

    /**
     * @brief Record that a route triggered an update
     *
     * @param address Address of the route
     */
    static void recordRouteTrigger(uint16_t address);

    /**
     * @brief Clean up expired entries in caches
     * Removes old packet IDs and cooldown entries
     */
    static void cleanup();

    /**
     * @brief Get duplicate packet detection statistics
     *
     * @return Number of duplicates detected
     */
    static uint32_t getDuplicatesDetected();

    /**
     * @brief Get triggered update statistics
     *
     * @return Number of triggered updates sent
     */
    static uint32_t getTriggeredUpdatesSent();

    /**
     * @brief Get suppressed update statistics
     *
     * @return Number of updates suppressed due to rate limiting
     */
    static uint32_t getUpdatesSuppressed();

private:
    // Duplicate detection cache (circular buffer)
    static PacketIdentifier* duplicateCache;
    static uint8_t duplicateCacheIndex;
    static uint8_t duplicateCacheSize;

    // Per-route cooldown tracking
    static RouteCooldown* routeCooldowns;
    static uint8_t routeCooldownCount;

    // Rate limiting
    static unsigned long lastTriggeredUpdate;
    static uint8_t stormBackoffCounter;

    // Statistics
    static uint32_t duplicatesDetected;
    static uint32_t triggeredUpdatesSent;
    static uint32_t updatesSuppressed;

    /**
     * @brief Find a packet in the duplicate cache
     *
     * @param sourceAddress Source address
     * @param packetId Packet ID
     * @return Index in cache, or -1 if not found
     */
    static int findPacketInCache(uint16_t sourceAddress, uint8_t packetId);

    /**
     * @brief Find a route in the cooldown list
     *
     * @param address Route address
     * @return Pointer to RouteCooldown, or nullptr if not found
     */
    static RouteCooldown* findRouteCooldown(uint16_t address);

    /**
     * @brief Add or update a route cooldown entry
     *
     * @param address Route address
     */
    static void updateRouteCooldown(uint16_t address);
};

#endif
