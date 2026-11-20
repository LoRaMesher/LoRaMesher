/**
 * @file frame_codec.hpp
 * @brief Seals plain frames into secured on-air frames and opens them again
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "security/frame_counter.hpp"
#include "security/frame_format.hpp"
#include "security/i_crypto_backend.hpp"
#include "security/i_key_provider.hpp"
#include "security/join_nonce_table.hpp"
#include "security/key_ring.hpp"
#include "security/peer_replay_table.hpp"
#include "security/security_config.hpp"
#include "security/security_context.hpp"
#include "types/messages/base_message.hpp"
#include "utils/compat/span.hpp"

namespace loramesher {
namespace security {

/**
 * @brief Outcome of sealing a frame
 *
 * A plain enum keeps the path free of heap allocation.
 */
enum class SealStatus : uint8_t {
    kOk = 0,
    kUnsupportedType,     ///< Plain type has no class
    kForeignSource,       ///< Class B or C1 with a source other than this node
    kTooLarge,            ///< On-air length above max_packet_size, or above
                          ///< the output buffer
    kBeaconDelayRange,    ///< Beacon payload byte 11 is not zero
    kNoKey,               ///< No key for the epoch or the peer
    kCounterUnavailable,  ///< Saved limit reached, the frame stays queued
    kCounterExhausted,    ///< The counter space reached its ceiling
    kSealLocked,          ///< State captured for deep sleep
    kPlainForwardBody,    ///< Forwarded data frame without the received
                          ///< ciphertext
    // The values below are outside the table of the specification
    kMalformed,    ///< The plain frame does not parse
    kNotReady,     ///< Counters not started, or security disabled
    kCryptoError,  ///< The AES backend failed
};

/**
 * @brief Outcome of opening a received frame
 */
enum class OpenStatus : uint8_t {
    kOk = 0,
    kPlainDropped,    ///< S = 0 in secured mode
    kSecuredDropped,  ///< S = 1 while security is disabled
    kMalformed,       ///< Length rule, H flag, unknown or unsupported type
    kUnknownEpoch,    ///< No held epoch with the parity of the frame
    kAuthFailed,      ///< MIC or hop MIC mismatch
    kJoinUnverified,  ///< JOIN_RESPONSE for another node, MIC mismatch
    kOwnFrame,        ///< Sent or originated by this node
    kOverheard,       ///< Data frame for another next hop, state untouched
    kReplay,          ///< Counter replayed, all ones, or of an older epoch
    kE2eRequired,     ///< End-to-end protection required and missing
    kKeyUnavailable,  ///< No key for the body of the frame
    kTagFailed,       ///< Body tag mismatch
    kInvalidState,    ///< Body is not the received ciphertext
    kBufferTooSmall,  ///< Output buffer too small
    kCryptoError,     ///< The AES backend failed
};

/**
 * @brief What the node does with an opened frame
 */
enum class RxRole : uint8_t {
    kNone = 0,
    kLocal,        ///< Beacon or control frame: hand it to the parser
    kDestination,  ///< Unicast data for this node: open the body, deliver
    kRelay,        ///< Unicast data for another node: forward the bytes
    kFlood,        ///< Broadcast or group data: open the body for delivery,
                   ///< forward the received bytes
};

/**
 * @brief Facts about a frame, filled by OpenFrame() and VerifyHop()
 */
struct OpenInfo {
    MessageType plain_type = MessageType::ANY;
    FrameClass frame_class = FrameClass::kUnsupported;
    RxRole role = RxRole::kNone;
    /// Full key epoch the frame was sealed under
    uint8_t epoch = 0;
    /// True when transmitter is known. A data frame does not name it
    bool has_transmitter = false;
    /// Node whose radio sent the frame on this hop
    AddressType transmitter = 0;
    /// Counter value of the frame, without flags
    uint32_t counter = 0;
    /// Data frames: the E flag
    bool end_to_end = false;
    /// On-air length of the frame. Airtime is computed from this value
    size_t on_air_size = 0;
    /// Data frames: the fields a relay hands back to SealForwarded()
    SecurityContext context;
};

/**
 * @brief One implementation of the secured frame format for every path
 *
 * The codec works on bytes. Seal takes a plain frame exactly as
 * BaseMessage::Serialize() produces it (6-byte header, then payload) and
 * writes the on-air bytes. Open takes on-air bytes and writes a plain frame
 * that BaseMessage::CreateFromSerialized() and the typed parsers accept.
 * The radio transmit path, the radio receive path and MockRadio call the
 * same two functions.
 *
 * Frame classes:
 * - Sync beacon: encrypted and authenticated under the network key with
 *   the beacon counter of the transmitter. The wire payload drops the top
 *   byte of the propagation delay.
 * - Control frames: encrypted and authenticated with the control counter
 *   of the transmitter. Join frames carry the transmitter in the trailer.
 *   A forwarder opens the frame and seals the frame it builds as new.
 * - Data frames: the origin encrypts and tags the body once. Every
 *   transmitter adds a hop MIC over the whole frame and masks the routing
 *   bytes. A relay forwards the received body, counter field and tag byte
 *   for byte and can only recompute the hop MIC and the mask.
 *
 * Receive order: length and type checks, epoch selection, MIC, then the
 * replay check. No state changes before every check the node can make has
 * passed. The replay state is committed last. An overheard data frame
 * changes nothing.
 *
 * This step covers bodies under the group keys (E = 0). A frame with E = 1
 * is verified and forwarded by a relay. Sealing or opening an E = 1 body
 * needs the node keys of a later step and returns kKeyUnavailable.
 *
 * Sealing and opening allocate nothing. The peer table is allocated once
 * in the constructor.
 *
 * The class is not thread safe. Its owner serializes the calls from the
 * transmit path and the receive path.
 */
class FrameCodec {
   public:
    /**
     * @param config Mode, node address, size limit and peer capacity
     * @param backend AES primitives. Must outlive the codec
     * @param key_provider Source of the network keys. May be null when the
     *        mode is kDisabled
     * @param counters Transmit counters of the node. Must outlive the
     *        codec. The owner starts them and runs their saves
     */
    FrameCodec(const FrameCodecConfig& config, const ICryptoBackend& backend,
               std::shared_ptr<IKeyProvider> key_provider,
               FrameCounters& counters);

    /**
     * @brief Read the network keys from the provider and derive their keys
     *
     * Call at start and after the provider stored a new key.
     *
     * @return Number of epochs held afterwards
     */
    size_t LoadKeys();

    /// Erase every derived key
    void InvalidateKeys() { keys_.Clear(); }

    // ------------------------------------------------------------------
    // Sizes
    // ------------------------------------------------------------------

    /// Trailer length of a type in the mode of this codec, 0 when disabled
    size_t TrailerLen(MessageType plain_type) const {
        return security::TrailerLen(config_.mode, plain_type);
    }

    /// On-air length of a frame from the size of its plain form
    size_t OnAirSize(MessageType plain_type, size_t plain_total_size) const {
        return security::OnAirSize(config_.mode, plain_type, plain_total_size);
    }

    /// Largest application payload of a send type with the current settings
    size_t MaxPayloadSize(MessageType type, bool group_acks = false) const {
        return MaxApplicationPayload(config_.mode, type, group_acks,
                                     config_.max_packet_size);
    }

    // ------------------------------------------------------------------
    // Byte level entry points
    // ------------------------------------------------------------------

    /**
     * @brief Turn a plain frame into its on-air bytes
     *
     * Takes the next counter of the matching space for a frame this node
     * transmits as new: every beacon, every control frame, and every data
     * frame whose source is this node. A data frame with another source is
     * a forwarded frame and goes through SealForwarded().
     *
     * Every check runs before a counter is taken, so a refused frame
     * consumes nothing.
     *
     * @param plain Header and payload as BaseMessage serializes them
     * @param context Security fields of the message. Only a forwarded data
     *        frame uses them
     * @param out Receives the on-air bytes
     * @param out_len Receives their length, 0 on failure
     */
    SealStatus SealFrame(std::span<const uint8_t> plain,
                         const SecurityContext& context, std::span<uint8_t> out,
                         size_t& out_len);

    /**
     * @brief Check a received frame and turn it into its plain form
     *
     * Runs the whole receive decision. For a beacon or a control frame the
     * output is the decrypted plain frame. For a data frame the output has
     * the plain type, the readable routing bytes and the body still as the
     * received ciphertext. info.role then says what to do:
     * - kDestination: OpenFrameBody(), deliver the result
     * - kFlood: OpenFrameBody() for delivery, forward the output of this
     *   call through RewriteForForward() and SealForwarded()
     * - kRelay: forward in the same way, the body stays closed
     *
     * State changes happen only after every check this node can make has
     * passed: the replay state of the peer, and the join nonce of a request
     * heard from the joiner. For a frame in transit (kRelay) the hop window
     * is committed here. For kDestination and kFlood the hop window is only
     * checked here and OpenFrameBody() commits it after the tag verified.
     *
     * @param frame On-air bytes
     * @param out Receives the plain frame. kMaxPlainFrameSize always fits
     * @param out_len Receives its length, 0 when the frame is dropped
     * @param info Receives the facts about the frame
     */
    OpenStatus OpenFrame(std::span<const uint8_t> frame, std::span<uint8_t> out,
                         size_t& out_len, OpenInfo& info);

    /**
     * @brief Verify the body tag of an opened data frame and decrypt a copy
     *
     * The input stays untouched, so it can still be forwarded. When this
     * node is the destination or the type is a flood type, the call checks
     * hop window G again and commits it after the tag verified. A second
     * call for the same frame therefore returns kReplay, and a frame with
     * a bad tag leaves the window untouched.
     *
     * @param opened Output of OpenFrame() for a data frame
     * @param context info.context of the same call
     * @param out Receives the plain frame with the decrypted body
     * @param out_len Receives its length, 0 on failure
     */
    OpenStatus OpenFrameBody(std::span<const uint8_t> opened,
                             const SecurityContext& context,
                             std::span<uint8_t> out, size_t& out_len);

    // ------------------------------------------------------------------
    // Relay operations for data frames
    // ------------------------------------------------------------------

    /**
     * @brief Unmask a data frame and verify its hop MIC
     *
     * The first part of OpenFrame() for a data frame: length rule, H flag,
     * epoch selection, header mask and hop MIC. It reads no replay state
     * and changes nothing.
     */
    OpenStatus VerifyHop(std::span<const uint8_t> frame, std::span<uint8_t> out,
                         size_t& out_len, OpenInfo& info);

    /**
     * @brief Update the fields a relay may change in an opened data frame
     *
     * Decrements ttl. Writes the next hop of a unicast frame. A flood frame
     * keeps its next hop of 0xFFFF. Nothing else is touched.
     *
     * @param opened Output of OpenFrame() for a data frame, changed in place
     * @param next_hop Next hop of a unicast frame
     * @return False when the frame is no data frame or its ttl is 1 or 0,
     *         which means it must not be forwarded
     */
    static bool RewriteForForward(std::span<uint8_t> opened,
                                  AddressType next_hop);

    /**
     * @brief Seal a data frame this node forwards
     *
     * Copies the header, the body, the counter field and the tag exactly as
     * received and computes the hop MIC and the header mask for the routing
     * bytes as they are now. Uses the epoch the frame arrived under. Takes
     * no counter and runs no encryption of the body.
     *
     * @return kPlainForwardBody when the context does not mark the body as
     *         received ciphertext, kMalformed when the source is this node
     */
    SealStatus SealForwarded(std::span<const uint8_t> opened,
                             const SecurityContext& context,
                             std::span<uint8_t> out, size_t& out_len);

    // ------------------------------------------------------------------
    // BaseMessage level entry points
    // ------------------------------------------------------------------

    /// SealFrame() on a message. Uses the security context of the message
    SealStatus Seal(const BaseMessage& message, std::span<uint8_t> out,
                    size_t& out_len);

    /**
     * @brief OpenFrame() into a message
     *
     * For a data frame the message carries the security context and its
     * body is the received ciphertext.
     *
     * @param frame On-air bytes
     * @param info Optional, receives the facts about the frame
     * @param status Optional, receives the reason when nothing is returned
     */
    std::optional<BaseMessage> Open(std::span<const uint8_t> frame,
                                    OpenInfo* info = nullptr,
                                    OpenStatus* status = nullptr);

    /**
     * @brief OpenFrameBody() on a message
     *
     * @return A copy with the decrypted body. The input is left untouched
     */
    std::optional<BaseMessage> OpenBody(const BaseMessage& message,
                                        OpenStatus* status = nullptr);

    // ------------------------------------------------------------------
    // State
    // ------------------------------------------------------------------

    /// Forget the join nonces of this node. Call when the join completed
    /// or was abandoned. Until then the node counts as joining
    void ClearJoinNonces() { own_join_nonces_.Clear(); }

    /// Number of own join nonces kept, 0 when the node is not joining
    size_t GetOwnJoinNonceCount() const { return own_join_nonces_.Count(); }

    /// Receive state of the peers, for the routing hook and the snapshot
    PeerReplayTable& GetPeers() { return peers_; }

    const PeerReplayTable& GetPeers() const { return peers_; }

    /// Counters of sealed, opened and dropped frames
    SecurityStats GetStats() const;

    /// Source address of the last frame that failed a MIC or a tag
    AddressType GetLastFailedSource() const { return last_failed_source_; }

    const FrameCodecConfig& GetConfig() const { return config_; }

    /// Epoch this node seals under, the epoch of the counter record
    uint8_t GetTxEpoch() const { return counters_.GetEpoch(); }

   private:
    struct Header;

    SealStatus SealChecked(std::span<const uint8_t> plain,
                           const SecurityContext& context,
                           std::span<uint8_t> out, size_t& out_len);
    SealStatus SealBeacon(const Header& header, std::span<const uint8_t> plain,
                          std::span<uint8_t> out, size_t& out_len);
    SealStatus SealControl(const Header& header, std::span<const uint8_t> plain,
                           std::span<uint8_t> out, size_t& out_len);
    SealStatus SealOriginData(const Header& header,
                              std::span<const uint8_t> plain,
                              std::span<uint8_t> out, size_t& out_len);
    SealStatus SealForwardedChecked(std::span<const uint8_t> opened,
                                    const SecurityContext& context,
                                    std::span<uint8_t> out, size_t& out_len);
    SealStatus FinishHopLayer(const NetworkTrafficKeys& keys,
                              std::span<uint8_t> frame, size_t extension_len);
    SealStatus TakeCounter(CounterSpace space, uint32_t& value);

    OpenStatus OpenChecked(std::span<const uint8_t> frame,
                           std::span<uint8_t> out, size_t& out_len,
                           OpenInfo& info);
    OpenStatus OpenControl(const Header& header, std::span<const uint8_t> frame,
                           const NetworkTrafficKeys& keys,
                           std::span<uint8_t> out, size_t& out_len,
                           OpenInfo& info);
    OpenStatus OpenData(std::span<const uint8_t> frame, std::span<uint8_t> out,
                        size_t& out_len, OpenInfo& info);
    OpenStatus VerifyHopChecked(std::span<const uint8_t> frame,
                                std::span<uint8_t> out, size_t& out_len,
                                OpenInfo& info);
    OpenStatus OpenBodyChecked(std::span<const uint8_t> opened,
                               const SecurityContext& context,
                               std::span<uint8_t> out, size_t& out_len);
    OpenStatus Count(OpenStatus status, AddressType source);

    uint32_t NowMs() const { return config_.clock_ms ? config_.clock_ms() : 0; }

    FrameCodecConfig config_;
    const ICryptoBackend& backend_;
    std::shared_ptr<IKeyProvider> key_provider_;
    FrameCounters& counters_;
    KeyRing keys_;
    PeerReplayTable peers_;
    SponsorNonceTable sponsor_nonces_;
    OwnJoinNonces own_join_nonces_;
    SecurityStats stats_;
    AddressType last_failed_source_ = 0;
};

}  // namespace security
}  // namespace loramesher
