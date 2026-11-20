/**
 * @file frame_codec_test_support.hpp
 * @brief Test nodes and an independent reference sealer for the frame codec
 *
 * The reference functions build secured frames from the AES primitives
 * alone, following the pseudo code of the wire security specification.
 * They take the counter as a parameter, so a test can also build frames no
 * honest node sends: a replayed counter, the all-ones counter, a set H flag
 * or a body under a node key.
 */

#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <vector>

#include "crypto_test_vectors.hpp"
#include "security/crypto_backend.hpp"
#include "security/frame_codec.hpp"
#include "security/kdf.hpp"
#include "security/memory_counter_store.hpp"

namespace loramesher {
namespace security {
namespace codec_test {

using Bytes = std::vector<uint8_t>;
using test_vectors::ArrayFromHex;
using test_vectors::FromHex;

constexpr AddressType kGateway = 0x0A01;
constexpr AddressType kRelay = 0x0A02;
constexpr AddressType kNode = 0x0A03;
constexpr AddressType kOther = 0x0A04;
constexpr AddressType kGroup = 0x8001;

/// K_net[1] of the specification vectors
inline Key128 NetKey1() {
    return ArrayFromHex<Key128>("000102030405060708090A0B0C0D0E0F");
}

/// K_net[2] of vector V6
inline Key128 NetKey2() {
    return ArrayFromHex<Key128>("A0A1A2A3A4A5A6A7A8A9AAABACADAEAF");
}

/// K_node[0x0A03] of the specification vectors
inline Key128 NodeKey0A03() {
    return ArrayFromHex<Key128>("6F77DB437E50C7A24B5446B6F480043C");
}

inline Bytes Concat(std::initializer_list<Bytes> parts) {
    Bytes all;
    for (const Bytes& part : parts) {
        all.insert(all.end(), part.begin(), part.end());
    }
    return all;
}

inline Bytes Le16(uint16_t value) {
    return {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
}

inline Bytes Le32(uint32_t value) {
    return {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
            static_cast<uint8_t>(value >> 16),
            static_cast<uint8_t>(value >> 24)};
}

/// Plain frame as BaseMessage serializes it: header, then payload
inline Bytes PlainFrame(AddressType destination, AddressType source,
                        MessageType type, const Bytes& payload) {
    return Concat(
        {Le16(destination),
         Le16(source),
         {static_cast<uint8_t>(type), static_cast<uint8_t>(payload.size())},
         payload});
}

/// Plain data frame: header, extension (next_hop, ttl, seq_num), body
inline Bytes PlainData(AddressType destination, AddressType source,
                       MessageType type, AddressType next_hop, uint8_t ttl,
                       uint8_t seq_num, const Bytes& body) {
    return PlainFrame(destination, source, type,
                      Concat({Le16(next_hop), {ttl, seq_num}, body}));
}

/// Plain group frame: the extension has a flags byte before seq_num
inline Bytes PlainGroup(AddressType group, AddressType source, uint8_t ttl,
                        uint8_t flags, uint8_t seq_num, const Bytes& body) {
    return PlainFrame(group, source, MessageType::DATA_GROUP,
                      Concat({Le16(0xFFFF), {ttl, flags, seq_num}, body}));
}

/// Key provider with a fixed list of network keys
class StaticKeyProvider : public IKeyProvider {
   public:
    explicit StaticKeyProvider(std::vector<NetworkKey> keys)
        : keys_(std::move(keys)) {}

    size_t GetNetworkKeys(std::array<NetworkKey, 2>& out) override {
        size_t count = 0;
        for (const NetworkKey& key : keys_) {
            if (count < out.size()) {
                out[count++] = key;
            }
        }
        return count;
    }

    std::optional<Key128> GetNodeKey(AddressType /*peer*/) override {
        ++node_key_calls;
        return std::nullopt;
    }

    AddressType GetGatewayAddress() override { return kGateway; }

    void SetKeys(std::vector<NetworkKey> keys) { keys_ = std::move(keys); }

    int node_key_calls = 0;

   private:
    std::vector<NetworkKey> keys_;
};

/// Where the counters of a test node start
struct CounterStart {
    uint32_t beacon = 0;
    uint32_t control = 0;
    uint32_t data = 0;
};

/**
 * @brief One node with its own keys, counters, clock and codec
 */
class TestNode {
   public:
    explicit TestNode(AddressType address, uint8_t tx_epoch = 1,
                      CounterStart start = {},
                      std::vector<NetworkKey> keys = {{1, NetKey1()}},
                      size_t max_packet_size = kDefaultMaxPacketSize,
                      SecurityMode mode = SecurityMode::kSecured)
        : provider(std::make_shared<StaticKeyProvider>(std::move(keys))),
          store(std::make_shared<MemoryCounterStore>(
              MakeRecord(tx_epoch, start))),
          counters(store),
          codec(MakeConfig(address, max_packet_size, mode), backend, provider,
                counters) {
        counters.Start();
        codec.LoadKeys();
        Refill();
    }

    /// Keep every space supplied, as the protocol task does between slots
    void Refill() {
        counters.RunPendingSave();
        counters.EnsureReserved(CounterSpace::kBeacon);
        counters.EnsureReserved(CounterSpace::kControl);
        counters.EnsureReserved(CounterSpace::kData);
    }

    /// Seal a plain frame, expecting success
    Bytes Seal(const Bytes& plain, const SecurityContext& context = {}) {
        Refill();
        Bytes out(kMaxSealedFrameSize);
        size_t out_len = 0;
        last_seal = codec.SealFrame(plain, context, out, out_len);
        out.resize(out_len);
        return out;
    }

    /// Open a frame. Returns the plain frame, empty when it was dropped
    Bytes Open(const Bytes& frame) {
        Bytes out(kMaxPlainFrameSize);
        size_t out_len = 0;
        last_open = codec.OpenFrame(frame, out, out_len, info);
        out.resize(out_len);
        return out;
    }

    /// Open the body of the frame opened last
    Bytes OpenBody(const Bytes& opened) {
        Bytes out(kMaxPlainFrameSize);
        size_t out_len = 0;
        last_open = codec.OpenFrameBody(opened, info.context, out, out_len);
        out.resize(out_len);
        return out;
    }

    /// Forward the frame opened last to a next hop
    Bytes Forward(Bytes opened, AddressType next_hop) {
        if (!FrameCodec::RewriteForForward(opened, next_hop)) {
            last_seal = SealStatus::kInvalidParameter;
            return {};
        }
        Bytes out(kMaxSealedFrameSize);
        size_t out_len = 0;
        last_seal = codec.SealForwarded(opened, info.context, out, out_len);
        out.resize(out_len);
        return out;
    }

    DefaultCryptoBackend backend;
    std::shared_ptr<StaticKeyProvider> provider;
    std::shared_ptr<MemoryCounterStore> store;
    FrameCounters counters;
    uint32_t now_ms = 0;
    FrameCodec codec;
    OpenInfo info;
    SealStatus last_seal = SealStatus::kOk;
    OpenStatus last_open = OpenStatus::kOk;

   private:
    static CounterRecord MakeRecord(uint8_t tx_epoch, const CounterStart& s) {
        CounterRecord record;
        record.tx_epoch = tx_epoch;
        record.limit[0] = s.beacon;
        record.limit[1] = s.control;
        record.limit[2] = s.data;
        return record;
    }

    FrameCodecConfig MakeConfig(AddressType address, size_t max_packet_size,
                                SecurityMode mode) {
        FrameCodecConfig config;
        config.mode = mode;
        config.node_address = address;
        config.max_packet_size = max_packet_size;
        config.max_peers = 8;
        config.join_nonce_lifetime_ms = 30000;
        config.clock_ms = [this]() {
            return now_ms;
        };
        return config;
    }
};

// ---------------------------------------------------------------------------
// Reference sealer, independent of the codec
// ---------------------------------------------------------------------------

inline AesBlock RefNonce(uint8_t domain, AddressType sender, uint32_t counter) {
    AesBlock block{};
    block[0] = domain;
    block[1] = static_cast<uint8_t>(sender);
    block[2] = static_cast<uint8_t>(sender >> 8);
    for (size_t i = 0; i < 4; ++i) {
        block[3 + i] = static_cast<uint8_t>(counter >> (8 * i));
    }
    return block;
}

/// CTR(K, A, M): key stream block j is AES(K, A[0..13] with j in 14..15)
inline Bytes RefCtr(const Aes128Key& key, const AesBlock& nonce,
                    const Bytes& data) {
    DefaultCryptoBackend backend;
    Bytes out = data;
    for (size_t offset = 0, j = 1; offset < out.size(); offset += 16, ++j) {
        AesBlock counter_block = nonce;
        counter_block[14] = static_cast<uint8_t>(j >> 8);
        counter_block[15] = static_cast<uint8_t>(j);
        AesBlock key_stream;
        backend.EncryptBlock(key, counter_block, key_stream);
        for (size_t i = 0; i < 16 && offset + i < out.size(); ++i) {
            out[offset + i] ^= key_stream[i];
        }
    }
    return out;
}

inline Bytes RefMic(const Aes128Key& key, const Bytes& message,
                    size_t size = 4) {
    DefaultCryptoBackend backend;
    AesBlock mac;
    backend.Cmac(key, message, mac);
    return Bytes(mac.begin(), mac.begin() + size);
}

inline NetworkTrafficKeys RefNetKeys(const Key128& network_key, uint8_t epoch) {
    DefaultCryptoBackend backend;
    NetworkTrafficKeys keys;
    DeriveNetworkTrafficKeys(backend, network_key, epoch, keys);
    return keys;
}

inline Bytes WithWireType(Bytes frame, uint8_t epoch) {
    frame[4] = static_cast<uint8_t>(0x80 | ((epoch & 1) << 3) | frame[4]);
    return frame;
}

/// Class C1 or C2 frame. `bound` enters the MIC input only
inline Bytes RefControl(const NetworkTrafficKeys& keys, uint8_t epoch,
                        const Bytes& plain, AddressType transmitter,
                        uint32_t counter, bool join, const Bytes& bound = {}) {
    const Bytes header =
        WithWireType(Bytes(plain.begin(), plain.begin() + 6), epoch);
    const AesBlock nonce = RefNonce(0x01, transmitter, counter);
    const Bytes cipher =
        RefCtr(keys.he, nonce, Bytes(plain.begin() + 6, plain.end()));
    const Bytes mic = RefMic(keys.hm, Concat({Bytes(nonce.begin(), nonce.end()),
                                              header, cipher, bound}));
    return Concat({header, cipher, join ? Le16(transmitter) : Bytes{},
                   Le32(counter), mic});
}

/// Class B frame from the 14-byte plain payload
inline Bytes RefBeacon(const NetworkTrafficKeys& keys, uint8_t epoch,
                       const Bytes& plain, uint32_t counter) {
    Bytes header = WithWireType(Bytes(plain.begin(), plain.begin() + 6), epoch);
    header[5] = 13;
    Bytes payload(plain.begin() + 6, plain.end());
    payload.erase(payload.begin() + 11);
    const AddressType source =
        static_cast<AddressType>(plain[2] | (plain[3] << 8));
    const AesBlock nonce = RefNonce(0x02, source, counter);
    const Bytes cipher = RefCtr(keys.he, nonce, payload);
    const Bytes mic = RefMic(
        keys.hm, Concat({Bytes(nonce.begin(), nonce.end()), header, cipher}));
    Bytes ctr = Le32(counter);
    ctr.pop_back();
    return Concat({header, cipher, ctr, mic});
}

/// Hop layer of a class D frame: hop MIC and header mask
inline Bytes RefHopLayer(const NetworkTrafficKeys& keys, const Bytes& header,
                         const Bytes& extension, const Bytes& cipher,
                         const Bytes& ctr_field, const Bytes& tag) {
    const Bytes hmic = RefMic(
        keys.hm, Concat({{0x06}, header, extension, cipher, ctr_field, tag}));
    const Bytes sample = Concat({ctr_field, tag, hmic,
                                 Bytes(header.begin() + 2, header.begin() + 4),
                                 Bytes(header.begin(), header.begin() + 2)});
    DefaultCryptoBackend backend;
    AesBlock sample_block;
    std::copy(sample.begin(), sample.end(), sample_block.begin());
    AesBlock mask;
    backend.EncryptBlock(keys.hp, sample_block, mask);
    Bytes masked = extension;
    for (size_t k = 0; k < masked.size(); ++k) {
        masked[k] ^= mask[k];
    }
    return Concat({header, masked, cipher, ctr_field, tag, hmic});
}

/**
 * @brief Class D frame with explicit body keys, domain and counter field
 *
 * @param plain Header, extension and plain body
 * @param ctr_field Counter with the E and H flags as they go on the wire
 */
inline Bytes RefData(const NetworkTrafficKeys& keys, uint8_t epoch,
                     const Bytes& plain, uint32_t ctr_field, uint8_t domain,
                     const Aes128Key& body_enc, const Aes128Key& body_mac) {
    const Bytes header =
        WithWireType(Bytes(plain.begin(), plain.begin() + 6), epoch);
    const size_t extension_len = plain[4] == 0x14 ? 5 : 4;
    const Bytes extension(plain.begin() + 6, plain.begin() + 6 + extension_len);
    const Bytes body(plain.begin() + 6 + extension_len, plain.end());
    const AddressType origin =
        static_cast<AddressType>(plain[2] | (plain[3] << 8));
    const AesBlock nonce = RefNonce(domain, origin, ctr_field & 0x3FFFFFFFu);
    const Bytes cipher = RefCtr(body_enc, nonce, body);
    Bytes immutable = extension;
    immutable[0] = immutable[1] = immutable[2] = 0;
    const Bytes tag = RefMic(
        body_mac,
        Concat({Bytes(nonce.begin(), nonce.end()), header, immutable, cipher}));
    return RefHopLayer(keys, header, extension, cipher, Le32(ctr_field), tag);
}

/// Class D frame under the group keys (E = 0)
inline Bytes RefGroupData(const NetworkTrafficKeys& keys, uint8_t epoch,
                          const Bytes& plain, uint32_t counter) {
    return RefData(keys, epoch, plain, counter, 0x03, keys.ge, keys.gm);
}

}  // namespace codec_test
}  // namespace security
}  // namespace loramesher
