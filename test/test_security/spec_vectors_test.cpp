/**
 * @file spec_vectors_test.cpp
 * @brief Key derivation and the worked vectors of the wire security spec
 *
 * Reproduces the cryptographic part of every worked vector (sections S12.1
 * to S12.8 of the specification) from the primitives alone: nonce block,
 * key stream, ciphertext, MIC, tag, hop MIC and header mask. Building the
 * plain payload from message fields is the job of the frame codec.
 */

#include <gtest/gtest.h>

#include <initializer_list>
#include <vector>

#include "crypto_test_vectors.hpp"
#include "security/crypto_backend.hpp"
#include "security/kdf.hpp"

namespace loramesher {
namespace security {
namespace {

using test_vectors::ArrayFromHex;
using test_vectors::FromHex;
using Bytes = std::vector<uint8_t>;

constexpr uint8_t kEpoch = 1;
constexpr uint16_t kGateway = 0x0A01;
constexpr uint16_t kRelay = 0x0A02;
constexpr uint16_t kNode = 0x0A03;

#if LORAMESHER_SECURITY_USE_MBEDTLS
using BackendTypes =
    ::testing::Types<PortableCryptoBackend, MbedTlsCryptoBackend>;
#else
using BackendTypes = ::testing::Types<PortableCryptoBackend>;
#endif

Bytes Concat(std::initializer_list<Bytes> parts) {
    Bytes all;
    for (const Bytes& part : parts) {
        all.insert(all.end(), part.begin(), part.end());
    }
    return all;
}

Bytes ToBytes(const AesBlock& block) {
    return Bytes(block.begin(), block.end());
}

/// Nonce block: domain, sender (LE), counter (LE), one extra byte, zeros
AesBlock NonceBlock(uint8_t domain, uint16_t sender, uint32_t counter,
                    uint8_t byte7 = 0) {
    AesBlock block{};
    block[0] = domain;
    block[1] = static_cast<uint8_t>(sender);
    block[2] = static_cast<uint8_t>(sender >> 8);
    for (size_t i = 0; i < 4; ++i) {
        block[3 + i] = static_cast<uint8_t>(counter >> (8 * i));
    }
    block[7] = byte7;
    return block;
}

template <typename Backend>
class SpecVectorsTest : public ::testing::Test {
   protected:
    void SetUp() override {
        network_key_ =
            ArrayFromHex<Aes128Key>("000102030405060708090A0B0C0D0E0F");
        node_key_ = ArrayFromHex<Aes128Key>("6F77DB437E50C7A24B5446B6F480043C");
        ASSERT_EQ(CryptoStatus::kOk, DeriveNetworkTrafficKeys(
                                         backend_, network_key_, kEpoch, net_));
        ASSERT_EQ(CryptoStatus::kOk,
                  DeriveNodeTrafficKeys(backend_, node_key_, kEpoch, node_));
        ASSERT_EQ(CryptoStatus::kOk,
                  DeriveRecoveryKeys(backend_, node_key_, recovery_));
    }

    /// CTR of the specification: key stream block j is AES(K, A[0..13], j)
    /// with a 16-bit big-endian j that starts at 1
    Bytes Ctr(const Aes128Key& key, const AesBlock& nonce, Bytes data) {
        AesBlock counter = nonce;
        counter[14] = 0;
        counter[15] = 1;
        EXPECT_EQ(CryptoStatus::kOk, backend_.CtrCrypt(key, counter, data));
        return data;
    }

    Bytes Cmac(const Aes128Key& key, const Bytes& message) {
        AesBlock mac{};
        EXPECT_EQ(CryptoStatus::kOk, backend_.Cmac(key, message, mac));
        return ToBytes(mac);
    }

    /// Leading bytes of the CMAC, checked through the verify call as well
    Bytes Mic(const Aes128Key& key, const Bytes& message, size_t size) {
        Bytes mic(size);
        EXPECT_EQ(CryptoStatus::kOk, backend_.Cmac(key, message, mic));
        EXPECT_EQ(CryptoStatus::kOk, backend_.CmacVerify(key, message, mic));
        return mic;
    }

    Bytes Block(const Aes128Key& key, const Bytes& input) {
        AesBlock in{};
        std::copy(input.begin(), input.end(), in.begin());
        AesBlock out{};
        EXPECT_EQ(CryptoStatus::kOk, backend_.EncryptBlock(key, in, out));
        return ToBytes(out);
    }

    /// One control or beacon frame: C = CTR(K_he), MIC over A, H, C, extra
    void ExpectControlFrame(const char* name, uint8_t domain, uint16_t sender,
                            uint32_t counter, const char* header,
                            const char* plain, const char* nonce_block,
                            const char* cipher, const char* cmac,
                            const char* trailer, const char* frame,
                            const char* mic_extra = "") {
        SCOPED_TRACE(name);
        const AesBlock a = NonceBlock(domain, sender, counter);
        EXPECT_EQ(FromHex(nonce_block), ToBytes(a));
        const Bytes c = Ctr(net_.he, a, FromHex(plain));
        EXPECT_EQ(FromHex(cipher), c);
        const Bytes mic_input =
            Concat({ToBytes(a), FromHex(header), c, FromHex(mic_extra)});
        EXPECT_EQ(FromHex(cmac), Cmac(net_.hm, mic_input));
        const Bytes mic = Mic(net_.hm, mic_input, 4);

        // The trailer ends with the MIC and the frame is H, C, trailer
        const Bytes expected_trailer = FromHex(trailer);
        EXPECT_EQ(mic,
                  Bytes(expected_trailer.end() - 4, expected_trailer.end()));
        EXPECT_EQ(FromHex(frame),
                  Concat({FromHex(header), c, expected_trailer}));

        // Receiver side: MIC first, then the same CTR gives the plaintext
        EXPECT_EQ(FromHex(plain), Ctr(net_.he, a, c));
    }

    /// What one hop of a data frame adds: hop MIC, mask, masked extension
    void ExpectHop(const char* name, const char* header, const char* x,
                   const Bytes& cipher, const char* ctr, const Bytes& tag,
                   const char* hop_cmac, const char* hmic, const char* sample,
                   const char* mask, const char* x_wire, const char* frame) {
        SCOPED_TRACE(name);
        const Bytes h = FromHex(header);
        const Bytes hop_input =
            Concat({FromHex("06"), h, FromHex(x), cipher, FromHex(ctr), tag});
        if (hop_cmac[0] != '\0') {
            EXPECT_EQ(FromHex(hop_cmac), Cmac(net_.hm, hop_input));
        }
        const Bytes computed_hmic = Mic(net_.hm, hop_input, 4);
        EXPECT_EQ(FromHex(hmic), computed_hmic);

        // sample = ctr, tag, hmic, source, destination as on the wire
        const Bytes computed_sample =
            Concat({FromHex(ctr), tag, computed_hmic,
                    Bytes(h.begin() + 2, h.begin() + 4),
                    Bytes(h.begin(), h.begin() + 2)});
        EXPECT_EQ(FromHex(sample), computed_sample);
        const Bytes computed_mask = Block(net_.hp, computed_sample);
        if (mask[0] != '\0') {
            EXPECT_EQ(FromHex(mask), computed_mask);
        }
        Bytes masked = FromHex(x);
        for (size_t i = 0; i < masked.size(); ++i) {
            masked[i] ^= computed_mask[i];
        }
        EXPECT_EQ(FromHex(x_wire), masked);
        EXPECT_EQ(FromHex(frame), Concat({h, masked, cipher, FromHex(ctr), tag,
                                          computed_hmic}));
    }

    Backend backend_;
    Aes128Key network_key_{};
    Aes128Key node_key_{};
    NetworkTrafficKeys net_{};
    NodeTrafficKeys node_{};
    RecoveryKeys recovery_{};
};

TYPED_TEST_SUITE(SpecVectorsTest, BackendTypes);

TYPED_TEST(SpecVectorsTest, DerivedKeysMatchS12_1) {
    const auto key = [](const char* hex) {
        return ArrayFromHex<Aes128Key>(hex);
    };
    EXPECT_EQ(key("3F190153DF3104694764132CA0361034"), this->net_.he);
    EXPECT_EQ(key("8D5A63B2B6C12FA7238DA6FE941039E0"), this->net_.hm);
    EXPECT_EQ(key("2977A7AE47612A1374D011B64BADF207"), this->net_.hp);
    EXPECT_EQ(key("C2CCEC823A94FA9804AB593CB957157A"), this->net_.ge);
    EXPECT_EQ(key("DC623F94FDCB6D1C4F18915D080E235B"), this->net_.gm);
    EXPECT_EQ(key("B3DE076E7DC79D6E8C0218737EC1271D"), this->node_.ee);
    EXPECT_EQ(key("1E98DEB578EDE6937E90FB6234BFF9AF"), this->node_.em);
    EXPECT_EQ(key("7AE7E29E33AC0FAC8F36B34B39E0C13C"), this->node_.ke);
    EXPECT_EQ(key("46CB3EA2F926EAF374D610071870ADFD"), this->node_.km);
    EXPECT_EQ(key("30E06F4E6E136E109E51029C8EDEE484"), this->recovery_.re);
    EXPECT_EQ(key("140EA8FFE794FEE93CBE35BF73FE178D"), this->recovery_.rm);
}

TYPED_TEST(SpecVectorsTest, DeriveKeyIsCmacOfLabelAndByte) {
    Aes128Key derived{};
    ASSERT_EQ(CryptoStatus::kOk, DeriveKey(this->backend_, this->network_key_,
                                           "LMHE", kEpoch, derived));
    EXPECT_EQ(this->net_.he, derived);
    EXPECT_EQ(ToBytes(derived),
              this->Cmac(this->network_key_, FromHex("4C4D484501")));

    // Another epoch or another label gives another key
    Aes128Key other{};
    ASSERT_EQ(CryptoStatus::kOk,
              DeriveKey(this->backend_, this->network_key_, "LMHE", 2, other));
    EXPECT_NE(derived, other);
    ASSERT_EQ(CryptoStatus::kOk, DeriveKey(this->backend_, this->network_key_,
                                           "LMHM", kEpoch, other));
    EXPECT_NE(derived, other);
}

TYPED_TEST(SpecVectorsTest, V1SyncBeacon) {
    this->ExpectControlFrame(
        "V1", 0x02, kRelay, 0x000123, "FFFF020ACE0D",
        "EFBE28B603010A02D20400050C", "02020A23010000000000000000000000",
        "D42A669BCCD9CDAFE4FB2AFE88", "86E3AF6E1845FE2A004798193176B609",
        "23010086E3AF6E",
        "FFFF020ACE0DD42A669BCCD9CDAFE4FB2AFE8823010086E3AF6E");
}

TYPED_TEST(SpecVectorsTest, V2RoutingTableFragment) {
    this->ExpectControlFrame(
        "V2", 0x01, kRelay, 0x00000457, "FFFF020ABA1B",
        "010A0702000201010A01E6020100DC010A030A01C8010002BE030A",
        "01020A57040000000000000000000000",
        "D2FDA910C18F3255A05278A8B3F3C6BA949FBCD5619BA24EC88CF9",
        "74482AA8649807BB058F3C6CF3204441", "5704000074482AA8",
        "FFFF020ABA1BD2FDA910C18F3255A05278A8B3F3C6BA949FBCD5619BA24EC88CF9"
        "5704000074482AA8");

    // The two key stream blocks listed with the vector
    const Bytes stream =
        this->Ctr(this->net_.he, NonceBlock(0x01, kRelay, 0x457), Bytes(32, 0));
    EXPECT_EQ(FromHex("D3F7AE12C18D3354AA539EAAB2F31ABB"
                      "9E9CB6D4A99AA24C768FF37C772A807F"),
              stream);
}

TYPED_TEST(SpecVectorsTest, V3EndToEndDataOverOneRelay) {
    const Bytes h = FromHex("010A030A990E");
    const Bytes body = FromHex("74656D703D32312E3543");
    const AesBlock a_p = NonceBlock(0x04, kNode, 0x29);
    EXPECT_EQ(FromHex("04030A29000000000000000000000000"), ToBytes(a_p));
    EXPECT_EQ(FromHex("542B0AC1A3C5CC864F799AADC8936948"),
              this->Ctr(this->node_.ee, a_p, Bytes(16, 0)));
    const Bytes c = this->Ctr(this->node_.ee, a_p, body);
    EXPECT_EQ(FromHex("204E67B19EF7FDA87A3A"), c);

    const Bytes tag_input = Concat({ToBytes(a_p), h, FromHex("00000037"), c});
    EXPECT_EQ(36u, tag_input.size());
    EXPECT_EQ(FromHex("5E7BC82C6564F2F1523B48B7E1CF596B"),
              this->Cmac(this->node_.em, tag_input));
    const Bytes tag = this->Mic(this->node_.em, tag_input, 4);
    EXPECT_EQ(FromHex("5E7BC82C"), tag);

    this->ExpectHop(
        "hop 1", "010A030A990E", "020A0A37", c, "29000080", tag,
        "CF6288E43BC349279B5CB92D6676D6F2", "CF6288E4",
        "290000805E7BC82CCF6288E4030A010A", "216CE481375A3298D78913B1D61825C0",
        "2366EEB6",
        "010A030A990E2366EEB6204E67B19EF7FDA87A3A290000805E7BC82CCF6288E4");
    this->ExpectHop(
        "hop 2", "010A030A990E", "010A0937", c, "29000080", tag,
        "7F2F90E26B4F8364977282ABF07CCD01", "7F2F90E2",
        "290000805E7BC82C7F2F90E2030A010A", "59B05E3BA57C37FDA5149B79F62428F5",
        "58BA570C",
        "010A030A990E58BA570C204E67B19EF7FDA87A3A290000805E7BC82C7F2F90E2");

    // The destination decrypts the body with the same key stream
    EXPECT_EQ(body, this->Ctr(this->node_.ee, a_p, c));
}

TYPED_TEST(SpecVectorsTest, V4DataBroadcast) {
    const Bytes h = FromHex("FFFF030A9A0E");
    const Bytes body = {'h', 'e', 'l', 'l', 'o', ' ', 'm', 'e', 's', 'h'};
    const AesBlock a_p = NonceBlock(0x03, kNode, 0x2A);
    EXPECT_EQ(FromHex("03030A2A000000000000000000000000"), ToBytes(a_p));
    const Bytes c = this->Ctr(this->net_.ge, a_p, body);
    EXPECT_EQ(FromHex("E1BFFE005EF6AF1B37A1"), c);

    const Bytes tag_input = Concat({ToBytes(a_p), h, FromHex("00000038"), c});
    EXPECT_EQ(FromHex("4F6D5274611A3904CC8DC4BA806A4FBB"),
              this->Cmac(this->net_.gm, tag_input));
    const Bytes tag = this->Mic(this->net_.gm, tag_input, 4);

    this->ExpectHop(
        "hop", "FFFF030A9A0E", "FFFF0A38", c, "2A000000", tag,
        "BFD0F5924CC96460B5CE747DE718CBA4", "BFD0F592",
        "2A0000004F6D5274BFD0F592030AFFFF", "25C3618D804C365119B868C49538F7A2",
        "DA3C6BB5",
        "FFFF030A9A0EDA3C6BB5E1BFFE005EF6AF1B37A12A0000004F6D5274BFD0F592");
}

TYPED_TEST(SpecVectorsTest, V5JoinWithNonce) {
    this->ExpectControlFrame(
        "V5a", 0x01, kNode, 0, "010A030ACA0A", "01020A020A00C0FFEE42",
        "01030A00000000000000000000000000", "566DDDC4E6B7A23E0525",
        "241A769CC1245EC1651572634D96DE15", "030A00000000241A769C",
        "010A030ACA0A566DDDC4E6B7A23E0525030A00000000241A769C");
    this->ExpectControlFrame("V5b", 0x01, kRelay, 2, "010A030ACA06",
                             "01010A020A01", "01020A02000000000000000000000000",
                             "0173303374AC", "789342C95787843B6FC8C078563ADA1E",
                             "020A02000000789342C9",
                             "010A030ACA060173303374AC020A02000000789342C9");
    // The join nonce enters the MIC input only
    this->ExpectControlFrame(
        "V5c", 0x01, kRelay, 3, "030A010ACB09", "EFBE0100030A000002",
        "01020A03000000000000000000000000", "E8BDEC55C0D6D23B05",
        "C3D5D4EB8B14586379617A8CBDD2A8B6", "020A03000000C3D5D4EB",
        "030A010ACB09E8BDEC55C0D6D23B05020A03000000C3D5D4EB", "C0FFEE42");

    // Without the nonce the MIC of V5c differs, so a replay to a joiner fails
    const AesBlock a = NonceBlock(0x01, kRelay, 3);
    const Bytes without_nonce = Concat(
        {ToBytes(a), FromHex("030A010ACB09"), FromHex("E8BDEC55C0D6D23B05")});
    EXPECT_EQ(CryptoStatus::kAuthFailed,
              this->backend_.CmacVerify(this->net_.hm, without_nonce,
                                        FromHex("C3D5D4EB")));
}

TYPED_TEST(SpecVectorsTest, V6KeyUpdate) {
    const Bytes next_key = FromHex("A0A1A2A3A4A5A6A7A8A9AAABACADAEAF");
    const Bytes kcv =
        this->Mic(this->node_.km, Concat({FromHex("08030A0200"), next_key}), 8);
    EXPECT_EQ(FromHex("DC1E63C2EA1226D4"), kcv);
    const Bytes body = Concat({FromHex("0200"), next_key, kcv});
    EXPECT_EQ(FromHex("0200A0A1A2A3A4A5A6A7A8A9AAABACADAEAFDC1E63C2EA1226D4"),
              body);

    const Bytes h = FromHex("030A010AD91E");
    const AesBlock a_p = NonceBlock(0x05, kGateway, 0x100);
    EXPECT_EQ(FromHex("05010A00010000000000000000000000"), ToBytes(a_p));
    const Bytes c = this->Ctr(this->node_.ke, a_p, body);
    EXPECT_EQ(FromHex("892C29831AFBF2DD1512B9FA14EEB8B1852CD4578B2822E8BA5C"),
              c);
    const Bytes tag_input = Concat({ToBytes(a_p), h, FromHex("00000005"), c});
    EXPECT_EQ(FromHex("8CC2C9BC07A3E979FD5B8D62250B4FFA"),
              this->Cmac(this->node_.km, tag_input));
    const Bytes tag = this->Mic(this->node_.km, tag_input, 4);

    // The vector lists neither the full hop CMAC nor the mask
    this->ExpectHop(
        "hop", "030A010AD91E", "020A0A05", c, "00010080", tag, "", "FC9B89C8",
        "000100808CC2C9BCFC9B89C8010A030A", "", "4246256A",
        "030A010AD91E4246256A892C29831AFBF2DD1512B9FA14EEB8B1852CD4578B2822E8"
        "BA5C000100808CC2C9BCFC9B89C8");
}

TYPED_TEST(SpecVectorsTest, V7Recovery) {
    // Request tag over the nonce block and the held epoch
    const AesBlock a_r = NonceBlock(0x09, kNode, 7);
    EXPECT_EQ(FromHex("09030A07000000000000000000000000"), ToBytes(a_r));
    const Bytes rtag =
        this->Mic(this->recovery_.rm, Concat({ToBytes(a_r), FromHex("01")}), 8);
    EXPECT_EQ(FromHex("261F520C68E88AFD"), rtag);
    EXPECT_EQ(FromHex("020A030AD30D0107000000261F520C68E88AFD"),
              Concat({FromHex("020A030AD30D0107000000"), rtag}));

    // Grant: the granted epoch sits in byte 7 of the nonce block
    const AesBlock a_g = NonceBlock(0x0A, kNode, 7, 3);
    EXPECT_EQ(FromHex("0A030A07000000030000000000000000"), ToBytes(a_g));
    const Bytes granted_key = FromHex("B0B1B2B3B4B5B6B7B8B9BABBBCBDBEBF");
    const Bytes c_g = this->Ctr(this->recovery_.re, a_g, granted_key);
    EXPECT_EQ(FromHex("DAC4A8CE528B55C9F0BB2FEB5697BD6B"), c_g);
    const Bytes gtag =
        this->Mic(this->recovery_.rm, Concat({ToBytes(a_g), c_g}), 8);
    EXPECT_EQ(FromHex("BAC34B148A020B19"), gtag);
    EXPECT_EQ(FromHex("030A020AD41903DAC4A8CE528B55C9F0BB2FEB5697BD6BBAC34B148A"
                      "020B19"),
              Concat({FromHex("030A020AD41903"), c_g, gtag}));

    // The orphan recovers the key with the same key stream
    EXPECT_EQ(granted_key, this->Ctr(this->recovery_.re, a_g, c_g));
}

}  // namespace
}  // namespace security
}  // namespace loramesher
