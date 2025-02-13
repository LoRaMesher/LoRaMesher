// src/loramesher/messages/message_factory.h
#pragma once

#include "message.hpp"
#include "routing_message.hpp"

namespace loramesher {

class MessageFactory {
   public:
    static std::unique_ptr<BaseMessage> createMessage(
        MessageType type, AddressType dest, AddressType src,
        const std::vector<uint8_t>& data) {
        // switch (type) {
        //     case MessageType::DATA:
        //         return std::make_unique<RoutingMessage>(dest, src, data);
        //     // Other cases
        //     default:
        //         return nullptr;
        // }
        return nullptr;
    }
};

}  // namespace loramesher