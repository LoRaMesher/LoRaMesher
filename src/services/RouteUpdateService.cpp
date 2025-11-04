#include "RouteUpdateService.h"

void RouteUpdateService::init() {
    // Allocate duplicate detection cache
    duplicateCache = new PacketIdentifier[DUPLICATE_CACHE_SIZE];
    duplicateCacheIndex = 0;
    duplicateCacheSize = 0;

    // Allocate per-route cooldown tracking
    routeCooldowns = new RouteCooldown[RTMAXSIZE];
    routeCooldownCount = 0;

    // Initialize rate limiting
    lastTriggeredUpdate = 0;
    stormBackoffCounter = 0;

    // Initialize statistics
    duplicatesDetected = 0;
    triggeredUpdatesSent = 0;
    updatesSuppressed = 0;

    ESP_LOGI(LM_TAG, "RouteUpdateService initialized: cache_size=%d", DUPLICATE_CACHE_SIZE);
}

bool RouteUpdateService::isDuplicatePacket(uint16_t sourceAddress, uint8_t packetId) {
    // Clean up expired entries periodically
    static unsigned long lastCleanup = 0;
    unsigned long now = millis();
    if (now - lastCleanup > 60000) { // Every 60 seconds
        cleanup();
        lastCleanup = now;
    }

    // Search for packet in cache
    int index = findPacketInCache(sourceAddress, packetId);

    if (index >= 0) {
        // Check if entry is still valid (not expired)
        unsigned long timestamp = duplicateCache[index].timestamp;
        if (now - timestamp < DUPLICATE_CACHE_TIMEOUT) {
            duplicatesDetected++;
            ESP_LOGD(LM_TAG, "Duplicate packet detected: src=%X id=%d age=%lums",
                     sourceAddress, packetId, now - timestamp);
            return true;
        }
    }

    return false;
}

void RouteUpdateService::recordPacket(uint16_t sourceAddress, uint8_t packetId) {
    // Add packet to circular buffer
    duplicateCache[duplicateCacheIndex] = PacketIdentifier(sourceAddress, packetId);

    // Advance index (circular buffer)
    duplicateCacheIndex = (duplicateCacheIndex + 1) % DUPLICATE_CACHE_SIZE;

    // Track actual size (up to max)
    if (duplicateCacheSize < DUPLICATE_CACHE_SIZE) {
        duplicateCacheSize++;
    }

    ESP_LOGV(LM_TAG, "Recorded packet: src=%X id=%d cache_size=%d",
             sourceAddress, packetId, duplicateCacheSize);
}

bool RouteUpdateService::shouldSendTriggeredUpdate() {
    unsigned long now = millis();
    unsigned long timeSinceLastUpdate = now - lastTriggeredUpdate;

    // Calculate backoff interval (exponential backoff on storm detection)
    unsigned long minInterval = MIN_TRIGGERED_UPDATE_INTERVAL * 1000; // Convert to ms
    unsigned long backoffInterval = minInterval;

    if (stormBackoffCounter > 0) {
        // Exponential backoff: 2^counter * min_interval
        backoffInterval = minInterval * (1 << stormBackoffCounter);
        if (backoffInterval > MAX_TRIGGERED_UPDATE_INTERVAL * 1000) {
            backoffInterval = MAX_TRIGGERED_UPDATE_INTERVAL * 1000;
        }
    }

    // Check if sufficient time has passed
    if (timeSinceLastUpdate < backoffInterval) {
        updatesSuppressed++;
        ESP_LOGD(LM_TAG, "Triggered update suppressed: backoff=%lums time_since_last=%lums",
                 backoffInterval, timeSinceLastUpdate);
        return false;
    }

    // Detect storm (multiple updates in quick succession)
    if (timeSinceLastUpdate < minInterval * 2) {
        // Rapid updates detected - increase backoff
        if (stormBackoffCounter < MAX_STORM_BACKOFF_COUNTER) {
            stormBackoffCounter++;
            ESP_LOGW(LM_TAG, "Update storm detected: backoff_counter=%d", stormBackoffCounter);
        }
    } else if (stormBackoffCounter > 0) {
        // Things have calmed down - reduce backoff
        stormBackoffCounter--;
        ESP_LOGI(LM_TAG, "Storm subsiding: backoff_counter=%d", stormBackoffCounter);
    }

    return true;
}

void RouteUpdateService::recordTriggeredUpdate() {
    lastTriggeredUpdate = millis();
    triggeredUpdatesSent++;
    ESP_LOGD(LM_TAG, "Triggered update sent: total=%u suppressed=%u",
             triggeredUpdatesSent, updatesSuppressed);
}

bool RouteUpdateService::canRouteTriggerUpdate(uint16_t address) {
    RouteCooldown* cooldown = findRouteCooldown(address);

    if (cooldown == nullptr) {
        // Route not in cooldown list - can trigger
        return true;
    }

    unsigned long now = millis();
    unsigned long timeSinceLast = now - cooldown->lastUpdate;
    unsigned long cooldownPeriod = PER_ROUTE_COOLDOWN * 1000; // Convert to ms

    if (timeSinceLast < cooldownPeriod) {
        ESP_LOGV(LM_TAG, "Route %X in cooldown: %lums remaining",
                 address, cooldownPeriod - timeSinceLast);
        return false;
    }

    return true;
}

void RouteUpdateService::recordRouteTrigger(uint16_t address) {
    updateRouteCooldown(address);
    ESP_LOGV(LM_TAG, "Route %X triggered update", address);
}

void RouteUpdateService::cleanup() {
    unsigned long now = millis();
    uint8_t expiredCount = 0;

    // Clean up expired packet identifiers
    for (uint8_t i = 0; i < duplicateCacheSize; i++) {
        if (now - duplicateCache[i].timestamp > DUPLICATE_CACHE_TIMEOUT) {
            // Entry expired - could compact, but circular buffer will overwrite naturally
            expiredCount++;
        }
    }

    // Clean up expired route cooldowns
    uint8_t newCount = 0;
    for (uint8_t i = 0; i < routeCooldownCount; i++) {
        if (now - routeCooldowns[i].lastUpdate < DUPLICATE_CACHE_TIMEOUT) {
            // Keep this entry
            if (i != newCount) {
                routeCooldowns[newCount] = routeCooldowns[i];
            }
            newCount++;
        }
    }
    routeCooldownCount = newCount;

    if (expiredCount > 0) {
        ESP_LOGD(LM_TAG, "Cleanup: expired_packets=%d active_cooldowns=%d",
                 expiredCount, routeCooldownCount);
    }
}

uint32_t RouteUpdateService::getDuplicatesDetected() {
    return duplicatesDetected;
}

uint32_t RouteUpdateService::getTriggeredUpdatesSent() {
    return triggeredUpdatesSent;
}

uint32_t RouteUpdateService::getUpdatesSuppressed() {
    return updatesSuppressed;
}

int RouteUpdateService::findPacketInCache(uint16_t sourceAddress, uint8_t packetId) {
    for (uint8_t i = 0; i < duplicateCacheSize; i++) {
        if (duplicateCache[i].sourceAddress == sourceAddress &&
            duplicateCache[i].packetId == packetId) {
            return i;
        }
    }
    return -1;
}

RouteCooldown* RouteUpdateService::findRouteCooldown(uint16_t address) {
    for (uint8_t i = 0; i < routeCooldownCount; i++) {
        if (routeCooldowns[i].address == address) {
            return &routeCooldowns[i];
        }
    }
    return nullptr;
}

void RouteUpdateService::updateRouteCooldown(uint16_t address) {
    RouteCooldown* cooldown = findRouteCooldown(address);

    if (cooldown != nullptr) {
        // Update existing entry
        cooldown->lastUpdate = millis();
    } else {
        // Add new entry (if space available)
        if (routeCooldownCount < RTMAXSIZE) {
            routeCooldowns[routeCooldownCount] = RouteCooldown(address);
            routeCooldownCount++;
        } else {
            // No space - find oldest entry and replace
            uint8_t oldestIndex = 0;
            unsigned long oldestTime = routeCooldowns[0].lastUpdate;
            for (uint8_t i = 1; i < routeCooldownCount; i++) {
                if (routeCooldowns[i].lastUpdate < oldestTime) {
                    oldestTime = routeCooldowns[i].lastUpdate;
                    oldestIndex = i;
                }
            }
            routeCooldowns[oldestIndex] = RouteCooldown(address);
        }
    }
}

// Static member initialization
PacketIdentifier* RouteUpdateService::duplicateCache = nullptr;
uint8_t RouteUpdateService::duplicateCacheIndex = 0;
uint8_t RouteUpdateService::duplicateCacheSize = 0;

RouteCooldown* RouteUpdateService::routeCooldowns = nullptr;
uint8_t RouteUpdateService::routeCooldownCount = 0;

unsigned long RouteUpdateService::lastTriggeredUpdate = 0;
uint8_t RouteUpdateService::stormBackoffCounter = 0;

uint32_t RouteUpdateService::duplicatesDetected = 0;
uint32_t RouteUpdateService::triggeredUpdatesSent = 0;
uint32_t RouteUpdateService::updatesSuppressed = 0;
