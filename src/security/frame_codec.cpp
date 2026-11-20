/**
 * @file frame_codec.cpp
 * @brief Seals plain frames into secured on-air frames and opens them again
 */

#include "security/frame_codec.hpp"

#include <cstring>
#include <utility>

#include "security/aes_modes.hpp"

namespace loramesher {
namespace security {

namespace {

/// Domain bytes of the nonce block and of the hop MIC input
constexpr uint8_t kDomainControl = 0x01;
constexpr uint8_t kDomainBeacon = 0x02;
constexpr uint8_t kDomainGroupBody = 0x03;
constexpr uint8_t kDomainHopMic = 0x06;

/// Offsets inside the BaseHeader
constexpr size_t kTypeOffset = 4;
constexpr size_t kPayloadSizeOffset = 5;

/// Offsets inside the join fields of a JOIN_REQUEST payload
constexpr size_t kJoinNextHopOffset = 1;
constexpr size_t kJoinSponsorOffset = 3;

/// Offset of target_address inside the join fields of a JOIN_RESPONSE
constexpr size_t kJoinTargetOffset = 6;

/// Size of the counter field in a class C or class D trailer
constexpr size_t kCounterFieldSize = 4;

/// Offset of ttl inside the DataHeader extension
constexpr size_t kTtlOffset = 2;

/// Largest input of any MIC: nonce block, header, payload, bound bytes
constexpr size_t kMacScratchSize =
    kAesBlockSize + kMaxPlainFrameSize + kMaxTrailerSize;

uint16_t ReadU16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0] | (data[1] << 8));
}

uint32_t ReadU32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

uint32_t ReadU24(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16);
}

void WriteU16(uint8_t* data, uint16_t value) {
    data[0] = static_cast<uint8_t>(value);
    data[1] = static_cast<uint8_t>(value >> 8);
}

void WriteU32(uint8_t* data, uint32_t value) {
    data[0] = static_cast<uint8_t>(value);
    data[1] = static_cast<uint8_t>(value >> 8);
    data[2] = static_cast<uint8_t>(value >> 16);
    data[3] = static_cast<uint8_t>(value >> 24);
}

/// Nonce block A: domain, sender, counter, zeros. Its block index is 0
AesBlock NonceBlock(uint8_t domain, AddressType sender, uint32_t counter) {
    AesBlock block{};
    block[0] = domain;
    WriteU16(&block[1], sender);
    WriteU32(&block[3], counter);
    return block;
}

/// Counter block of the first key stream block: A with block index 1
AesBlock FirstKeyStreamBlock(const AesBlock& nonce) {
    AesBlock block = nonce;
    block[kAesBlockSize - 2] = 0;
    block[kAesBlockSize - 1] = 1;
    return block;
}

/**
 * @brief Contiguous input of one MIC, built on the stack
 */
class MacInput {
   public:
    void Append(const uint8_t* data, size_t size) {
        if (size > sizeof(buffer_) - size_) {
            overflow_ = true;
            return;
        }
        if (size > 0) {
            std::memcpy(buffer_ + size_, data, size);
            size_ += size;
        }
    }

    void Append(uint8_t byte) { Append(&byte, 1); }

    /// Drop everything after the first `size` bytes
    void Truncate(size_t size) {
        if (size < size_) {
            size_ = size;
        }
    }

    size_t Size() const { return size_; }

    bool IsValid() const { return !overflow_; }

    std::span<const uint8_t> View() const { return {buffer_, size_}; }

   private:
    uint8_t buffer_[kMacScratchSize];
    size_t size_ = 0;
    bool overflow_ = false;
};

bool IsPlainTypeByte(uint8_t type) {
    return (type & static_cast<uint8_t>(~message_type::kPlainTypeMask)) == 0;
}

SealStatus FromCrypto(CryptoStatus status) {
    return status == CryptoStatus::kOk ? SealStatus::kOk
                                       : SealStatus::kCryptoError;
}

/// Verdict of a strict counter or a window for a frame whose MIC verified.
/// Reads the state and changes nothing
template <typename Check>
bool PeerAccepts(const HopReplayState* state, uint8_t epoch, Check check) {
    if (state == nullptr || epoch > state->epoch) {
        // Unknown peer, or a newer epoch that clears the state on commit
        return true;
    }
    if (epoch < state->epoch) {
        return false;
    }
    return check(*state) == ReplayVerdict::kAccept;
}

}  // namespace

struct FrameCodec::Header {
    AddressType destination;
    AddressType source;
    uint8_t type;
    uint8_t payload_size;

    static Header Parse(const uint8_t* data) {
        return Header{ReadU16(data), ReadU16(data + 2), data[kTypeOffset],
                      data[kPayloadSizeOffset]};
    }
};

FrameCodec::FrameCodec(const FrameCodecConfig& config,
                       const ICryptoBackend& backend,
                       std::shared_ptr<IKeyProvider> key_provider,
                       FrameCounters& counters)
    : config_(config),
      backend_(backend),
      key_provider_(std::move(key_provider)),
      counters_(counters),
      peers_(config.max_peers) {}

size_t FrameCodec::LoadKeys() {
    keys_.Clear();
    if (!key_provider_) {
        return 0;
    }
    std::array<NetworkKey, 2> network_keys{};
    size_t count = key_provider_->GetNetworkKeys(network_keys);
    if (count > network_keys.size()) {
        count = network_keys.size();
    }
    for (size_t i = 0; i < count; ++i) {
        keys_.Install(backend_, network_keys[i]);
    }
    for (NetworkKey& network_key : network_keys) {
        SecureZero(network_key.key);
    }
    return keys_.Count();
}

SecurityStats FrameCodec::GetStats() const {
    SecurityStats stats = stats_;
    stats.counter_store_writes = counters_.GetStoreWrites();
    stats.tx_epoch = counters_.GetEpoch();
    return stats;
}

// ---------------------------------------------------------------------------
// Seal
// ---------------------------------------------------------------------------

SealStatus FrameCodec::SealFrame(std::span<const uint8_t> plain,
                                 const SecurityContext& context,
                                 std::span<uint8_t> out, size_t& out_len) {
    out_len = 0;
    const SealStatus status = SealChecked(plain, context, out, out_len);
    if (status == SealStatus::kOk) {
        ++stats_.tx_sealed;
    } else {
        out_len = 0;
        ++stats_.tx_refused;
    }
    return status;
}

SealStatus FrameCodec::SealForwarded(std::span<const uint8_t> opened,
                                     const SecurityContext& context,
                                     std::span<uint8_t> out, size_t& out_len) {
    out_len = 0;
    const SealStatus status =
        SealForwardedChecked(opened, context, out, out_len);
    if (status == SealStatus::kOk) {
        ++stats_.tx_sealed;
    } else {
        out_len = 0;
        ++stats_.tx_refused;
    }
    return status;
}

SealStatus FrameCodec::SealChecked(std::span<const uint8_t> plain,
                                   const SecurityContext& context,
                                   std::span<uint8_t> out, size_t& out_len) {
    if (plain.size() < kBaseHeaderSize) {
        return SealStatus::kMalformed;
    }
    const Header header = Header::Parse(plain.data());
    if (plain.size() != kBaseHeaderSize + header.payload_size) {
        return SealStatus::kMalformed;
    }
    if (config_.mode != SecurityMode::kSecured) {
        // 2.0.0 behaviour: the plain frame is the on-air frame
        if (out.size() < plain.size()) {
            return SealStatus::kTooLarge;
        }
        std::memcpy(out.data(), plain.data(), plain.size());
        out_len = plain.size();
        return SealStatus::kOk;
    }
    if (!counters_.IsStarted()) {
        // Without a valid counter record the node stays silent
        return SealStatus::kNotReady;
    }
    if (!IsPlainTypeByte(header.type)) {
        return SealStatus::kMalformed;
    }
    switch (ClassOf(static_cast<MessageType>(header.type))) {
        case FrameClass::kBeacon:
            return SealBeacon(header, plain, out, out_len);
        case FrameClass::kControl:
        case FrameClass::kJoin:
            return SealControl(header, plain, out, out_len);
        case FrameClass::kData:
            // Only the origin runs the body layer. A frame of another
            // source can only be forwarded with its received ciphertext
            return header.source == config_.node_address
                       ? SealOriginData(header, plain, out, out_len)
                       : SealForwardedChecked(plain, context, out, out_len);
        default:
            return SealStatus::kUnsupportedType;
    }
}

SealStatus FrameCodec::TakeCounter(CounterSpace space, uint32_t& value) {
    switch (counters_.Next(space, value)) {
        case CounterStatus::kOk:
            return SealStatus::kOk;
        case CounterStatus::kStoreError:
            return SealStatus::kCounterUnavailable;
        case CounterStatus::kExhausted:
            return SealStatus::kCounterExhausted;
        case CounterStatus::kSealLocked:
            return SealStatus::kSealLocked;
        default:
            return SealStatus::kNotReady;
    }
}

SealStatus FrameCodec::SealBeacon(const Header& header,
                                  std::span<const uint8_t> plain,
                                  std::span<uint8_t> out, size_t& out_len) {
    // The nonce names the BaseHeader source, so it must be this node
    if (header.source != config_.node_address) {
        return SealStatus::kForeignSource;
    }
    if (header.payload_size != kBeaconPlainPayloadSize) {
        return SealStatus::kMalformed;
    }
    const uint8_t* payload = plain.data() + kBaseHeaderSize;
    if (payload[kBeaconDelayHighOffset] != 0) {
        // The delay does not fit the 3 bytes of the wire format
        return SealStatus::kBeaconDelayRange;
    }
    const uint8_t epoch = counters_.GetEpoch();
    const NetworkTrafficKeys* keys = keys_.FindByEpoch(epoch);
    if (keys == nullptr) {
        return SealStatus::kNoKey;
    }
    if (kSecuredSyncBeaconSize > config_.max_packet_size ||
        out.size() < kSecuredSyncBeaconSize) {
        return SealStatus::kTooLarge;
    }
    uint32_t counter = 0;
    const SealStatus counter_status =
        TakeCounter(CounterSpace::kBeacon, counter);
    if (counter_status != SealStatus::kOk) {
        return counter_status;
    }

    uint8_t* frame = out.data();
    std::memcpy(frame, plain.data(), kBaseHeaderSize);
    frame[kTypeOffset] =
        message_type::ToWireType(MessageType::SYNC_BEACON, epoch);
    frame[kPayloadSizeOffset] = static_cast<uint8_t>(kBeaconWirePayloadSize);
    uint8_t* body = frame + kBaseHeaderSize;
    std::memcpy(body, payload, kBeaconDelayHighOffset);
    std::memcpy(body + kBeaconDelayHighOffset,
                payload + kBeaconDelayHighOffset + 1,
                kBeaconPlainPayloadSize - kBeaconDelayHighOffset - 1);

    const AesBlock nonce = NonceBlock(kDomainBeacon, header.source, counter);
    const size_t body_end = kBaseHeaderSize + kBeaconWirePayloadSize;
    CryptoStatus crypto =
        backend_.CtrCrypt(keys->he, FirstKeyStreamBlock(nonce),
                          std::span<uint8_t>(body, kBeaconWirePayloadSize));
    if (crypto != CryptoStatus::kOk) {
        return SealStatus::kCryptoError;
    }
    MacInput mac_input;
    mac_input.Append(nonce.data(), nonce.size());
    mac_input.Append(frame, body_end);
    uint8_t* trailer = frame + body_end;
    trailer[0] = static_cast<uint8_t>(counter);
    trailer[1] = static_cast<uint8_t>(counter >> 8);
    trailer[2] = static_cast<uint8_t>(counter >> 16);
    crypto = backend_.Cmac(keys->hm, mac_input.View(),
                           std::span<uint8_t>(trailer + 3, kMicSize));
    out_len = kSecuredSyncBeaconSize;
    return FromCrypto(crypto);
}

SealStatus FrameCodec::SealControl(const Header& header,
                                   std::span<const uint8_t> plain,
                                   std::span<uint8_t> out, size_t& out_len) {
    const MessageType type = static_cast<MessageType>(header.type);
    const bool is_join = ClassOf(type) == FrameClass::kJoin;
    // Class C1 names its transmitter by the BaseHeader source alone, and
    // the nonce is built from it. Sealing another source would put this
    // node's counter under a foreign address
    if (!is_join && header.source != config_.node_address) {
        return SealStatus::kForeignSource;
    }
    const size_t trailer_size = SecuredTrailerLen(type);
    const size_t body_end = kBaseHeaderSize + header.payload_size;
    const size_t total = body_end + trailer_size;
    const uint8_t epoch = counters_.GetEpoch();
    const NetworkTrafficKeys* keys = keys_.FindByEpoch(epoch);
    if (keys == nullptr) {
        return SealStatus::kNoKey;
    }
    if (total > config_.max_packet_size || out.size() < total) {
        return SealStatus::kTooLarge;
    }

    // The last hop of a JOIN_RESPONSE has target_address 0 and binds the
    // nonce of the joiner into its MIC. A response on its way to the
    // sponsor names the joiner as target and is never bound, whatever the
    // table holds. The nonce is not sent
    JoinNonce bound_nonce{};
    const bool has_bound_nonce =
        type == MessageType::JOIN_RESPONSE &&
        header.payload_size >= kJoinResponseFieldsSize &&
        ReadU16(plain.data() + kBaseHeaderSize + kJoinTargetOffset) == 0 &&
        sponsor_nonces_.Find(header.destination, NowMs(),
                             config_.join_nonce_lifetime_ms, bound_nonce);

    uint32_t counter = 0;
    const SealStatus counter_status =
        TakeCounter(CounterSpace::kControl, counter);
    if (counter_status != SealStatus::kOk) {
        return counter_status;
    }

    const AddressType transmitter = config_.node_address;
    uint8_t* frame = out.data();
    std::memcpy(frame, plain.data(), body_end);
    frame[kTypeOffset] = message_type::ToWireType(type, epoch);

    const AesBlock nonce = NonceBlock(kDomainControl, transmitter, counter);
    CryptoStatus crypto = backend_.CtrCrypt(
        keys->he, FirstKeyStreamBlock(nonce),
        std::span<uint8_t>(frame + kBaseHeaderSize, header.payload_size));
    if (crypto != CryptoStatus::kOk) {
        return SealStatus::kCryptoError;
    }
    MacInput mac_input;
    mac_input.Append(nonce.data(), nonce.size());
    mac_input.Append(frame, body_end);
    if (has_bound_nonce) {
        mac_input.Append(bound_nonce.data(), bound_nonce.size());
    }
    uint8_t* trailer = frame + body_end;
    if (is_join) {
        WriteU16(trailer, transmitter);
        trailer += 2;
    }
    WriteU32(trailer, counter);
    crypto = backend_.Cmac(
        keys->hm, mac_input.View(),
        std::span<uint8_t>(trailer + kCounterFieldSize, kMicSize));
    if (crypto != CryptoStatus::kOk) {
        return SealStatus::kCryptoError;
    }

    if (has_bound_nonce) {
        // An entry is bound once
        sponsor_nonces_.Remove(header.destination);
    }

    // A joiner remembers the nonce of the request it just sealed
    if (type == MessageType::JOIN_REQUEST &&
        header.source == config_.node_address &&
        header.payload_size == kJoinRequestFieldsSize + kJoinNonceSize) {
        JoinNonce own_nonce;
        std::memcpy(own_nonce.data(),
                    plain.data() + kBaseHeaderSize + kJoinRequestFieldsSize,
                    own_nonce.size());
        if (own_join_nonces_.Count() == 0 ||
            own_join_nonces_.Get(0) != own_nonce) {
            own_join_nonces_.Push(own_nonce);
        }
    }
    out_len = total;
    return SealStatus::kOk;
}

SealStatus FrameCodec::SealOriginData(const Header& header,
                                      std::span<const uint8_t> plain,
                                      std::span<uint8_t> out, size_t& out_len) {
    const MessageType type = static_cast<MessageType>(header.type);
    const size_t extension_len = DataExtensionLen(type);
    if (header.payload_size < extension_len) {
        return SealStatus::kMalformed;
    }
    if (IsKeyDataType(type)) {
        // Key messages exist only under a node key (E = 1)
        return SealStatus::kNoKey;
    }
    const uint8_t epoch = counters_.GetEpoch();
    const NetworkTrafficKeys* keys = keys_.FindByEpoch(epoch);
    if (keys == nullptr) {
        return SealStatus::kNoKey;
    }
    const size_t body_end = kBaseHeaderSize + header.payload_size;
    const size_t total = body_end + kDataTrailerSize;
    if (total > config_.max_packet_size || out.size() < total) {
        return SealStatus::kTooLarge;
    }
    uint32_t counter = 0;
    const SealStatus counter_status = TakeCounter(CounterSpace::kData, counter);
    if (counter_status != SealStatus::kOk) {
        return counter_status;
    }

    uint8_t* frame = out.data();
    std::memcpy(frame, plain.data(), body_end);
    frame[kTypeOffset] = message_type::ToWireType(type, epoch);

    // Body layer: encrypt, then tag over the header, the extension bytes
    // no relay changes, and the ciphertext
    const size_t body_offset = kBaseHeaderSize + extension_len;
    const size_t body_len = body_end - body_offset;
    const AesBlock nonce = NonceBlock(kDomainGroupBody, header.source, counter);
    CryptoStatus crypto =
        backend_.CtrCrypt(keys->ge, FirstKeyStreamBlock(nonce),
                          std::span<uint8_t>(frame + body_offset, body_len));
    if (crypto != CryptoStatus::kOk) {
        return SealStatus::kCryptoError;
    }
    MacInput mac_input;
    mac_input.Append(nonce.data(), nonce.size());
    mac_input.Append(frame, kBaseHeaderSize);
    const uint8_t zeros[kMutableExtensionSize] = {0, 0, 0};
    mac_input.Append(zeros, sizeof(zeros));
    mac_input.Append(frame + kBaseHeaderSize + kMutableExtensionSize,
                     extension_len - kMutableExtensionSize);
    mac_input.Append(frame + body_offset, body_len);
    uint8_t* trailer = frame + body_end;
    WriteU32(trailer, counter);  // E = 0, H = 0
    crypto = backend_.Cmac(
        keys->gm, mac_input.View(),
        std::span<uint8_t>(trailer + kCounterFieldSize, kMicSize));
    if (crypto != CryptoStatus::kOk) {
        return SealStatus::kCryptoError;
    }
    const SealStatus status =
        FinishHopLayer(*keys, std::span<uint8_t>(frame, total), extension_len);
    if (status == SealStatus::kOk) {
        out_len = total;
    }
    return status;
}

SealStatus FrameCodec::SealForwardedChecked(std::span<const uint8_t> opened,
                                            const SecurityContext& context,
                                            std::span<uint8_t> out,
                                            size_t& out_len) {
    if (config_.mode != SecurityMode::kSecured) {
        return SealStatus::kNotReady;
    }
    if (opened.size() < kBaseHeaderSize) {
        return SealStatus::kMalformed;
    }
    const Header header = Header::Parse(opened.data());
    const MessageType type = static_cast<MessageType>(header.type);
    const size_t extension_len = DataExtensionLen(type);
    if (opened.size() != kBaseHeaderSize + header.payload_size ||
        !IsPlainTypeByte(header.type) || ClassOf(type) != FrameClass::kData ||
        header.payload_size < extension_len ||
        header.source == config_.node_address ||
        (context.ctr_field & kHopCounterFlag) != 0) {
        return SealStatus::kMalformed;
    }
    if (context.body_state != BodyState::kCipher) {
        // No path may encrypt a body under the counter of another node
        return SealStatus::kPlainForwardBody;
    }
    // The frame keeps the epoch it arrived under, also when this node
    // already transmits under a newer one
    const NetworkTrafficKeys* keys = keys_.FindByEpoch(context.epoch);
    if (keys == nullptr) {
        return SealStatus::kNoKey;
    }
    const size_t body_end = kBaseHeaderSize + header.payload_size;
    const size_t total = body_end + kDataTrailerSize;
    if (total > config_.max_packet_size || out.size() < total) {
        return SealStatus::kTooLarge;
    }
    uint8_t* frame = out.data();
    std::memcpy(frame, opened.data(), body_end);
    frame[kTypeOffset] = message_type::ToWireType(type, context.epoch);
    WriteU32(frame + body_end, context.ctr_field);
    std::memcpy(frame + body_end + kCounterFieldSize, context.tag.data(),
                kMicSize);
    const SealStatus status =
        FinishHopLayer(*keys, std::span<uint8_t>(frame, total), extension_len);
    if (status == SealStatus::kOk) {
        out_len = total;
    }
    return status;
}

SealStatus FrameCodec::FinishHopLayer(const NetworkTrafficKeys& keys,
                                      std::span<uint8_t> frame,
                                      size_t extension_len) {
    // Hop MIC over every byte before it, with the extension unmasked
    const size_t mic_offset = frame.size() - kMicSize;
    MacInput mac_input;
    mac_input.Append(kDomainHopMic);
    mac_input.Append(frame.data(), mic_offset);
    CryptoStatus crypto =
        backend_.Cmac(keys.hm, mac_input.View(),
                      std::span<uint8_t>(frame.data() + mic_offset, kMicSize));
    if (crypto != CryptoStatus::kOk) {
        return SealStatus::kCryptoError;
    }
    // Header mask from the trailer and the two addresses
    AesBlock sample;
    std::memcpy(sample.data(), frame.data() + frame.size() - kDataTrailerSize,
                kDataTrailerSize);
    std::memcpy(sample.data() + kDataTrailerSize, frame.data() + 2, 2);
    std::memcpy(sample.data() + kDataTrailerSize + 2, frame.data(), 2);
    AesBlock mask;
    crypto = backend_.EncryptBlock(keys.hp, sample, mask);
    if (crypto != CryptoStatus::kOk) {
        return SealStatus::kCryptoError;
    }
    for (size_t k = 0; k < extension_len; ++k) {
        frame[kBaseHeaderSize + k] ^= mask[k];
    }
    return SealStatus::kOk;
}

bool FrameCodec::RewriteForForward(std::span<uint8_t> opened,
                                   AddressType next_hop) {
    if (opened.size() < kBaseHeaderSize) {
        return false;
    }
    const Header header = Header::Parse(opened.data());
    const MessageType type = static_cast<MessageType>(header.type);
    const size_t extension_len = DataExtensionLen(type);
    if (!IsPlainTypeByte(header.type) || extension_len == 0 ||
        header.payload_size < extension_len ||
        opened.size() != kBaseHeaderSize + header.payload_size) {
        return false;
    }
    uint8_t* extension = opened.data() + kBaseHeaderSize;
    if (extension[kTtlOffset] <= 1) {
        return false;
    }
    --extension[kTtlOffset];
    if (!IsFloodType(type)) {
        WriteU16(extension, next_hop);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Open
// ---------------------------------------------------------------------------

OpenStatus FrameCodec::Count(OpenStatus status, AddressType source) {
    switch (status) {
        case OpenStatus::kPlainDropped:
            ++stats_.rx_plain_dropped;
            break;
        case OpenStatus::kMalformed:
            ++stats_.rx_malformed;
            break;
        case OpenStatus::kUnknownEpoch:
            ++stats_.rx_unknown_epoch;
            break;
        case OpenStatus::kAuthFailed:
            ++stats_.rx_auth_failed;
            last_failed_source_ = source;
            break;
        case OpenStatus::kTagFailed:
            ++stats_.rx_tag_failed;
            last_failed_source_ = source;
            break;
        case OpenStatus::kJoinUnverified:
            ++stats_.rx_join_unverified;
            break;
        case OpenStatus::kReplay:
            ++stats_.rx_replay;
            break;
        case OpenStatus::kE2eRequired:
            ++stats_.rx_e2e_required;
            break;
        case OpenStatus::kKeyUnavailable:
            ++stats_.rx_key_unavailable;
            break;
        default:
            break;
    }
    return status;
}

OpenStatus FrameCodec::OpenFrame(std::span<const uint8_t> frame,
                                 std::span<uint8_t> out, size_t& out_len,
                                 OpenInfo& info) {
    out_len = 0;
    info = OpenInfo{};
    info.on_air_size = frame.size();
    const OpenStatus status = OpenChecked(frame, out, out_len, info);
    if (status == OpenStatus::kOk) {
        ++stats_.rx_opened;
        return status;
    }
    out_len = 0;
    info.role = RxRole::kNone;
    return Count(status, frame.size() >= kBaseHeaderSize
                             ? ReadU16(frame.data() + 2)
                             : 0);
}

OpenStatus FrameCodec::VerifyHop(std::span<const uint8_t> frame,
                                 std::span<uint8_t> out, size_t& out_len,
                                 OpenInfo& info) {
    out_len = 0;
    info = OpenInfo{};
    info.on_air_size = frame.size();
    const OpenStatus status = VerifyHopChecked(frame, out, out_len, info);
    if (status == OpenStatus::kOk) {
        return status;
    }
    out_len = 0;
    return Count(status, frame.size() >= kBaseHeaderSize
                             ? ReadU16(frame.data() + 2)
                             : 0);
}

OpenStatus FrameCodec::OpenFrameBody(std::span<const uint8_t> opened,
                                     const SecurityContext& context,
                                     std::span<uint8_t> out, size_t& out_len) {
    out_len = 0;
    const OpenStatus status = OpenBodyChecked(opened, context, out, out_len);
    if (status == OpenStatus::kOk) {
        return status;
    }
    out_len = 0;
    return Count(status, opened.size() >= kBaseHeaderSize
                             ? ReadU16(opened.data() + 2)
                             : 0);
}

OpenStatus FrameCodec::OpenChecked(std::span<const uint8_t> frame,
                                   std::span<uint8_t> out, size_t& out_len,
                                   OpenInfo& info) {
    if (frame.size() < kBaseHeaderSize) {
        return OpenStatus::kMalformed;
    }
    const Header header = Header::Parse(frame.data());
    const size_t body_end = kBaseHeaderSize + header.payload_size;

    if (config_.mode != SecurityMode::kSecured) {
        // 2.0.0 behaviour. A secured frame is rejected by its type byte
        if (message_type::IsSecuredWireType(header.type)) {
            return OpenStatus::kSecuredDropped;
        }
        if (frame.size() < body_end) {
            return OpenStatus::kMalformed;
        }
        if (out.size() < body_end) {
            return OpenStatus::kBufferTooSmall;
        }
        std::memcpy(out.data(), frame.data(), body_end);
        out_len = body_end;
        info.plain_type = static_cast<MessageType>(header.type);
        info.role = RxRole::kLocal;
        return OpenStatus::kOk;
    }

    if (!message_type::IsSecuredWireType(header.type)) {
        return OpenStatus::kPlainDropped;
    }
    const MessageType type = message_type::ToPlainType(header.type);
    const FrameClass frame_class = ClassOf(type);
    info.plain_type = type;
    info.frame_class = frame_class;
    if (frame_class == FrameClass::kUnsupported ||
        frame_class == FrameClass::kRecovery) {
        // The recovery link frames are dropped until their step exists
        return OpenStatus::kMalformed;
    }
    if (frame.size() != body_end + SecuredTrailerLen(type)) {
        return OpenStatus::kMalformed;
    }
    if (frame_class == FrameClass::kData) {
        return OpenData(frame, out, out_len, info);
    }
    const NetworkTrafficKeys* keys = keys_.FindByParity(
        message_type::GetEpochParity(header.type), info.epoch);
    if (keys == nullptr) {
        return OpenStatus::kUnknownEpoch;
    }
    return OpenControl(header, frame, *keys, out, out_len, info);
}

OpenStatus FrameCodec::OpenControl(const Header& header,
                                   std::span<const uint8_t> frame,
                                   const NetworkTrafficKeys& keys,
                                   std::span<uint8_t> out, size_t& out_len,
                                   OpenInfo& info) {
    const MessageType type = info.plain_type;
    const bool is_beacon = info.frame_class == FrameClass::kBeacon;
    const bool is_join = info.frame_class == FrameClass::kJoin;
    if (is_beacon && header.payload_size != kBeaconWirePayloadSize) {
        return OpenStatus::kMalformed;
    }
    const size_t body_end = kBaseHeaderSize + header.payload_size;
    const size_t plain_size = is_beacon ? body_end + 1 : body_end;
    if (out.size() < plain_size) {
        return OpenStatus::kBufferTooSmall;
    }

    // Transmitter and counter, both in clear
    const uint8_t* trailer = frame.data() + body_end;
    AddressType transmitter = header.source;
    if (is_join) {
        transmitter = ReadU16(trailer);
        trailer += 2;
    }
    const uint32_t counter = is_beacon ? ReadU24(trailer) : ReadU32(trailer);
    const uint8_t* mic = frame.data() + frame.size() - kMicSize;
    info.has_transmitter = true;
    info.transmitter = transmitter;
    info.counter = counter;
    if (transmitter == config_.node_address) {
        return OpenStatus::kOwnFrame;
    }

    // MIC over the nonce block, the header and the ciphertext
    const AesBlock nonce = NonceBlock(
        is_beacon ? kDomainBeacon : kDomainControl, transmitter, counter);
    MacInput mac_input;
    mac_input.Append(nonce.data(), nonce.size());
    mac_input.Append(frame.data(), body_end);
    const size_t unbound_size = mac_input.Size();
    const std::span<const uint8_t> mic_span(mic, kMicSize);
    const bool to_this_node = header.destination == config_.node_address;
    CryptoStatus verdict = CryptoStatus::kAuthFailed;
    if (type == MessageType::JOIN_RESPONSE && to_this_node &&
        own_join_nonces_.Count() > 0) {
        // A joining node accepts only a response bound to one of its last
        // two nonces
        for (size_t i = 0; i < own_join_nonces_.Count() &&
                           verdict == CryptoStatus::kAuthFailed;
             ++i) {
            const JoinNonce& join_nonce = own_join_nonces_.Get(i);
            mac_input.Truncate(unbound_size);
            mac_input.Append(join_nonce.data(), join_nonce.size());
            verdict = backend_.CmacVerify(keys.hm, mac_input.View(), mic_span);
        }
    } else {
        verdict = backend_.CmacVerify(keys.hm, mac_input.View(), mic_span);
    }
    if (verdict == CryptoStatus::kAuthFailed) {
        // An overhearer cannot know the nonce of a final response
        return type == MessageType::JOIN_RESPONSE && !to_this_node
                   ? OpenStatus::kJoinUnverified
                   : OpenStatus::kAuthFailed;
    }
    if (verdict != CryptoStatus::kOk) {
        return OpenStatus::kCryptoError;
    }

    // Replay check on the counter of the transmitter. Nothing is written
    const uint32_t none = is_beacon ? kBeaconCounterNone : kControlCounterNone;
    if (counter == none) {
        return OpenStatus::kReplay;
    }
    const bool fresh =
        PeerAccepts(peers_.Find(transmitter), info.epoch,
                    [is_beacon, counter](const HopReplayState& state) {
                        return is_beacon ? state.beacon.Check(counter)
                                         : state.control.Check(counter);
                    });
    if (!fresh) {
        return OpenStatus::kReplay;
    }

    // Decrypt into the plain layout
    uint8_t* plain = out.data();
    std::memcpy(plain, frame.data(), body_end);
    plain[kTypeOffset] = static_cast<uint8_t>(type);
    const CryptoStatus crypto = backend_.CtrCrypt(
        keys.he, FirstKeyStreamBlock(nonce),
        std::span<uint8_t>(plain + kBaseHeaderSize, header.payload_size));
    if (crypto != CryptoStatus::kOk) {
        return OpenStatus::kCryptoError;
    }
    if (is_beacon) {
        // Put the top delay byte back, so the parser sees the 2.0.0 layout
        uint8_t* payload = plain + kBaseHeaderSize;
        std::memmove(payload + kBeaconDelayHighOffset + 1,
                     payload + kBeaconDelayHighOffset,
                     kBeaconWirePayloadSize - kBeaconDelayHighOffset);
        payload[kBeaconDelayHighOffset] = 0;
        plain[kPayloadSizeOffset] =
            static_cast<uint8_t>(kBeaconPlainPayloadSize);
    }

    // Every check passed. Commit the replay state
    HopReplayState* state = peers_.Touch(transmitter, info.epoch);
    if (state == nullptr) {
        // No room to remember the counter, so the frame cannot be accepted
        return OpenStatus::kReplay;
    }
    state->AcceptEpoch(info.epoch);
    if (is_beacon) {
        state->beacon.Commit(counter);
    } else {
        state->control.Commit(counter);
    }

    // A request heard from the joiner itself that names this node as next
    // hop or sponsor: keep the nonce for the final response. An overhearer
    // stores nothing
    if (type == MessageType::JOIN_REQUEST && transmitter == header.source &&
        header.payload_size == kJoinRequestFieldsSize + kJoinNonceSize) {
        const uint8_t* fields = plain + kBaseHeaderSize;
        const AddressType next_hop = ReadU16(fields + kJoinNextHopOffset);
        const AddressType sponsor = ReadU16(fields + kJoinSponsorOffset);
        if (next_hop == config_.node_address ||
            sponsor == config_.node_address) {
            JoinNonce join_nonce;
            std::memcpy(join_nonce.data(), fields + kJoinRequestFieldsSize,
                        join_nonce.size());
            sponsor_nonces_.Store(header.source, join_nonce, NowMs(),
                                  config_.join_nonce_lifetime_ms);
        }
    }

    out_len = plain_size;
    info.role = RxRole::kLocal;
    return OpenStatus::kOk;
}

OpenStatus FrameCodec::VerifyHopChecked(std::span<const uint8_t> frame,
                                        std::span<uint8_t> out, size_t& out_len,
                                        OpenInfo& info) {
    if (config_.mode != SecurityMode::kSecured) {
        return OpenStatus::kSecuredDropped;
    }
    if (frame.size() < kBaseHeaderSize) {
        return OpenStatus::kMalformed;
    }
    const Header header = Header::Parse(frame.data());
    if (!message_type::IsSecuredWireType(header.type)) {
        return OpenStatus::kPlainDropped;
    }
    const MessageType type = message_type::ToPlainType(header.type);
    const size_t extension_len = DataExtensionLen(type);
    const size_t body_end = kBaseHeaderSize + header.payload_size;
    info.plain_type = type;
    info.frame_class = ClassOf(type);
    if (extension_len == 0 || frame.size() != body_end + kDataTrailerSize ||
        header.payload_size < extension_len) {
        return OpenStatus::kMalformed;
    }
    const uint8_t* trailer = frame.data() + body_end;
    const uint32_t ctr_field = ReadU32(trailer);
    if ((ctr_field & kHopCounterFlag) != 0) {
        // Reserved for the per-transmitter hop counter
        return OpenStatus::kMalformed;
    }
    const NetworkTrafficKeys* keys = keys_.FindByParity(
        message_type::GetEpochParity(header.type), info.epoch);
    if (keys == nullptr) {
        return OpenStatus::kUnknownEpoch;
    }
    if (out.size() < body_end) {
        return OpenStatus::kBufferTooSmall;
    }

    // Unmask the extension with the mask of the received trailer
    AesBlock sample;
    std::memcpy(sample.data(), trailer, kDataTrailerSize);
    std::memcpy(sample.data() + kDataTrailerSize, frame.data() + 2, 2);
    std::memcpy(sample.data() + kDataTrailerSize + 2, frame.data(), 2);
    AesBlock mask;
    if (backend_.EncryptBlock(keys->hp, sample, mask) != CryptoStatus::kOk) {
        return OpenStatus::kCryptoError;
    }
    uint8_t extension[kGroupExtensionSize];
    for (size_t k = 0; k < extension_len; ++k) {
        extension[k] = frame[kBaseHeaderSize + k] ^ mask[k];
    }

    // Hop MIC over the frame as its transmitter built it
    const size_t body_offset = kBaseHeaderSize + extension_len;
    MacInput mac_input;
    mac_input.Append(kDomainHopMic);
    mac_input.Append(frame.data(), kBaseHeaderSize);
    mac_input.Append(extension, extension_len);
    mac_input.Append(frame.data() + body_offset,
                     body_end - body_offset + kCounterFieldSize + kMicSize);
    const CryptoStatus verdict = backend_.CmacVerify(
        keys->hm, mac_input.View(),
        std::span<const uint8_t>(trailer + kCounterFieldSize + kMicSize,
                                 kMicSize));
    if (verdict == CryptoStatus::kAuthFailed) {
        return OpenStatus::kAuthFailed;
    }
    if (verdict != CryptoStatus::kOk) {
        return OpenStatus::kCryptoError;
    }

    // Plain type, readable extension, body still as received
    uint8_t* opened = out.data();
    std::memcpy(opened, frame.data(), body_end);
    opened[kTypeOffset] = static_cast<uint8_t>(type);
    std::memcpy(opened + kBaseHeaderSize, extension, extension_len);
    out_len = body_end;

    info.counter = ctr_field & kDataCounterMask;
    info.end_to_end = (ctr_field & kEndToEndFlag) != 0;
    info.context.ctr_field = ctr_field;
    std::memcpy(info.context.tag.data(), trailer + kCounterFieldSize, kMicSize);
    info.context.epoch = info.epoch;
    info.context.body_state = BodyState::kCipher;
    return OpenStatus::kOk;
}

OpenStatus FrameCodec::OpenData(std::span<const uint8_t> frame,
                                std::span<uint8_t> out, size_t& out_len,
                                OpenInfo& info) {
    // Steps 1 and 2: length, H flag, epoch, mask, hop MIC
    const OpenStatus hop_status = VerifyHopChecked(frame, out, out_len, info);
    if (hop_status != OpenStatus::kOk) {
        return hop_status;
    }
    const Header header = Header::Parse(out.data());
    const MessageType type = info.plain_type;
    const AddressType origin = header.source;

    // Step 3: a frame this node originated
    if (origin == config_.node_address) {
        return OpenStatus::kOwnFrame;
    }
    // Step 4: a frame for another next hop is overheard. The same origin
    // counter will arrive again from the proper relay, so no state changes
    const AddressType next_hop = ReadU16(out.data() + kBaseHeaderSize);
    if (next_hop != config_.node_address && next_hop != kBroadcastAddress) {
        return OpenStatus::kOverheard;
    }
    if (IsKeyDataType(type) && !info.end_to_end) {
        return OpenStatus::kE2eRequired;
    }
    if (IsFloodType(type) && info.end_to_end) {
        return OpenStatus::kKeyUnavailable;
    }

    RxRole role = RxRole::kRelay;
    if (IsFloodType(type)) {
        role = RxRole::kFlood;
    } else if (header.destination == config_.node_address) {
        role = RxRole::kDestination;
    }

    // Step 5: hop replay check. E = 0 uses window G everywhere. E = 1 data
    // uses window R at a relay and no hop window at its destination. The
    // key types use no hop window at all. A frame in transit commits its
    // window here. A node that will verify the tag only checks window G
    // here and commits it in OpenFrameBody()
    if (info.counter == kDataCounterNone) {
        return OpenStatus::kReplay;
    }
    enum class Window : uint8_t { kNone, kGroup, kRelayed };
    Window window = Window::kNone;
    if (!info.end_to_end) {
        window = Window::kGroup;
    } else if (role == RxRole::kRelay && !IsKeyDataType(type)) {
        window = Window::kRelayed;
    }
    if (window != Window::kNone) {
        const uint32_t counter = info.counter;
        const bool use_group = window == Window::kGroup;
        const bool fresh =
            PeerAccepts(peers_.Find(origin), info.epoch,
                        [use_group, counter](const HopReplayState& state) {
                            return use_group ? state.group.Check(counter)
                                             : state.relayed.Check(counter);
                        });
        if (!fresh) {
            return OpenStatus::kReplay;
        }
        if (role == RxRole::kRelay) {
            HopReplayState* state = peers_.Touch(origin, info.epoch);
            if (state == nullptr) {
                return OpenStatus::kReplay;
            }
            state->AcceptEpoch(info.epoch);
            if (use_group) {
                state->group.Commit(counter);
            } else {
                state->relayed.Commit(counter);
            }
        }
    }
    info.role = role;
    return OpenStatus::kOk;
}

OpenStatus FrameCodec::OpenBodyChecked(std::span<const uint8_t> opened,
                                       const SecurityContext& context,
                                       std::span<uint8_t> out,
                                       size_t& out_len) {
    if (config_.mode != SecurityMode::kSecured ||
        context.body_state != BodyState::kCipher) {
        return OpenStatus::kInvalidState;
    }
    if (opened.size() < kBaseHeaderSize) {
        return OpenStatus::kMalformed;
    }
    const Header header = Header::Parse(opened.data());
    const MessageType type = static_cast<MessageType>(header.type);
    const size_t extension_len = DataExtensionLen(type);
    const size_t body_end = kBaseHeaderSize + header.payload_size;
    if (!IsPlainTypeByte(header.type) || extension_len == 0 ||
        opened.size() != body_end || header.payload_size < extension_len ||
        (context.ctr_field & kHopCounterFlag) != 0) {
        return OpenStatus::kMalformed;
    }
    const bool end_to_end = (context.ctr_field & kEndToEndFlag) != 0;
    if (IsKeyDataType(type) && !end_to_end) {
        return OpenStatus::kE2eRequired;
    }
    if (end_to_end) {
        // Bodies under a node key belong to a later step. Without a node
        // key for the origin the frame is dropped
        return OpenStatus::kKeyUnavailable;
    }
    const NetworkTrafficKeys* keys = keys_.FindByEpoch(context.epoch);
    if (keys == nullptr) {
        return OpenStatus::kUnknownEpoch;
    }
    if (out.size() < body_end) {
        return OpenStatus::kBufferTooSmall;
    }

    // Tag over the nonce block, the header as it was on the wire, the
    // extension bytes no relay changes, and the received ciphertext
    const size_t body_offset = kBaseHeaderSize + extension_len;
    const AesBlock nonce = NonceBlock(kDomainGroupBody, header.source,
                                      context.ctr_field & kDataCounterMask);
    uint8_t wire_header[kBaseHeaderSize];
    std::memcpy(wire_header, opened.data(), kBaseHeaderSize);
    wire_header[kTypeOffset] = message_type::ToWireType(type, context.epoch);
    MacInput mac_input;
    mac_input.Append(nonce.data(), nonce.size());
    mac_input.Append(wire_header, kBaseHeaderSize);
    const uint8_t zeros[kMutableExtensionSize] = {0, 0, 0};
    mac_input.Append(zeros, sizeof(zeros));
    mac_input.Append(opened.data() + kBaseHeaderSize + kMutableExtensionSize,
                     extension_len - kMutableExtensionSize);
    mac_input.Append(opened.data() + body_offset, body_end - body_offset);
    const CryptoStatus verdict = backend_.CmacVerify(
        keys->gm, mac_input.View(),
        std::span<const uint8_t>(context.tag.data(), context.tag.size()));
    if (verdict == CryptoStatus::kAuthFailed) {
        return OpenStatus::kTagFailed;
    }
    if (verdict != CryptoStatus::kOk) {
        return OpenStatus::kCryptoError;
    }

    // Step 6 for a node that verifies the tag: window G was only checked
    // when the frame was opened. Check it again, since another copy may
    // have been accepted meanwhile
    const uint32_t counter = context.ctr_field & kDataCounterMask;
    const bool commits_window =
        IsFloodType(type) || header.destination == config_.node_address;
    if (commits_window) {
        if (counter == kDataCounterNone ||
            !PeerAccepts(peers_.Find(header.source), context.epoch,
                         [counter](const HopReplayState& state) {
                             return state.group.Check(counter);
                         })) {
            return OpenStatus::kReplay;
        }
    }

    // Decrypt a copy. The input keeps the received ciphertext
    std::memcpy(out.data(), opened.data(), body_end);
    const CryptoStatus crypto = backend_.CtrCrypt(
        keys->ge, FirstKeyStreamBlock(nonce),
        std::span<uint8_t>(out.data() + body_offset, body_end - body_offset));
    if (crypto != CryptoStatus::kOk) {
        return OpenStatus::kCryptoError;
    }
    if (commits_window) {
        // Every check passed. Commit window G
        HopReplayState* state = peers_.Touch(header.source, context.epoch);
        if (state == nullptr) {
            return OpenStatus::kReplay;
        }
        state->AcceptEpoch(context.epoch);
        state->group.Commit(counter);
    }
    out_len = body_end;
    return OpenStatus::kOk;
}

// ---------------------------------------------------------------------------
// BaseMessage level
// ---------------------------------------------------------------------------

SealStatus FrameCodec::Seal(const BaseMessage& message, std::span<uint8_t> out,
                            size_t& out_len) {
    uint8_t plain[kMaxPlainFrameSize];
    const std::span<const uint8_t> payload = message.GetPayload();
    WriteU16(plain, message.GetDestination());
    WriteU16(plain + 2, message.GetSource());
    plain[kTypeOffset] = static_cast<uint8_t>(message.GetType());
    plain[kPayloadSizeOffset] = static_cast<uint8_t>(payload.size());
    if (!payload.empty()) {
        std::memcpy(plain + kBaseHeaderSize, payload.data(), payload.size());
    }
    return SealFrame(
        std::span<const uint8_t>(plain, kBaseHeaderSize + payload.size()),
        message.GetSecurityContext(), out, out_len);
}

std::optional<BaseMessage> FrameCodec::Open(std::span<const uint8_t> frame,
                                            OpenInfo* info,
                                            OpenStatus* status) {
    uint8_t plain[kMaxPlainFrameSize];
    size_t plain_len = 0;
    OpenInfo local_info;
    const OpenStatus open_status =
        OpenFrame(frame, plain, plain_len, local_info);
    if (info != nullptr) {
        *info = local_info;
    }
    if (status != nullptr) {
        *status = open_status;
    }
    if (open_status != OpenStatus::kOk) {
        return std::nullopt;
    }
    std::optional<BaseMessage> message(
        std::in_place, ReadU16(plain), ReadU16(plain + 2),
        static_cast<MessageType>(plain[kTypeOffset]),
        std::span<const uint8_t>(plain + kBaseHeaderSize,
                                 plain_len - kBaseHeaderSize));
    message->SetSecurityContext(local_info.context);
    return message;
}

std::optional<BaseMessage> FrameCodec::OpenBody(const BaseMessage& message,
                                                OpenStatus* status) {
    uint8_t opened[kMaxPlainFrameSize];
    uint8_t plain[kMaxPlainFrameSize];
    const std::span<const uint8_t> payload = message.GetPayload();
    WriteU16(opened, message.GetDestination());
    WriteU16(opened + 2, message.GetSource());
    opened[kTypeOffset] = static_cast<uint8_t>(message.GetType());
    opened[kPayloadSizeOffset] = static_cast<uint8_t>(payload.size());
    if (!payload.empty()) {
        std::memcpy(opened + kBaseHeaderSize, payload.data(), payload.size());
    }
    size_t plain_len = 0;
    const OpenStatus open_status = OpenFrameBody(
        std::span<const uint8_t>(opened, kBaseHeaderSize + payload.size()),
        message.GetSecurityContext(), plain, plain_len);
    if (status != nullptr) {
        *status = open_status;
    }
    if (open_status != OpenStatus::kOk) {
        return std::nullopt;
    }
    std::optional<BaseMessage> body(
        std::in_place, message.GetDestination(), message.GetSource(),
        message.GetType(),
        std::span<const uint8_t>(plain + kBaseHeaderSize,
                                 plain_len - kBaseHeaderSize));
    SecurityContext context = message.GetSecurityContext();
    context.body_state = BodyState::kPlain;
    body->SetSecurityContext(context);
    return body;
}

}  // namespace security
}  // namespace loramesher
