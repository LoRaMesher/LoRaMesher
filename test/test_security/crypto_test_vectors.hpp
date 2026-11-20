/**
 * @file crypto_test_vectors.hpp
 * @brief Published known-answer vectors for AES-128, CTR, CCM and CMAC
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "security/i_crypto_backend.hpp"

namespace loramesher {
namespace security {
namespace test_vectors {

/// Decode a string of hex digits
inline std::vector<uint8_t> FromHex(const char* hex) {
    const auto nibble = [](char digit) -> uint8_t {
        if (digit >= '0' && digit <= '9') {
            return static_cast<uint8_t>(digit - '0');
        }
        return static_cast<uint8_t>((digit | 0x20) - 'a' + 10);
    };
    std::vector<uint8_t> bytes;
    for (size_t i = 0; hex[i] != '\0' && hex[i + 1] != '\0'; i += 2) {
        bytes.push_back(
            static_cast<uint8_t>((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
    }
    return bytes;
}

/// Decode hex digits into a fixed-size array such as a key or a nonce
template <typename Array>
Array ArrayFromHex(const char* hex) {
    const std::vector<uint8_t> bytes = FromHex(hex);
    Array array{};
    if (bytes.size() == array.size()) {
        std::memcpy(array.data(), bytes.data(), array.size());
    }
    return array;
}

/// One CCM known answer, every field in hex
struct CcmVector {
    const char* name;
    const char* key;
    const char* nonce;
    const char* associated_data;
    const char* plaintext;
    const char* ciphertext;
    const char* tag;
};

/// RFC 3610 section 8 packet vectors with an 8-byte tag. Every vector of
/// the RFC uses a 13-byte nonce. The other twelve use a 10-byte tag.
constexpr CcmVector kRfc3610Vectors[] = {
    {"RFC 3610 #1", "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf",
     "00000003020100a0a1a2a3a4a5", "0001020304050607",
     "08090a0b0c0d0e0f101112131415161718191a1b1c1d1e",
     "588c979a61c663d2f066d0c2c0f989806d5f6b61dac384", "17e8d12cfdf926e0"},
    {"RFC 3610 #2", "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf",
     "00000004030201a0a1a2a3a4a5", "0001020304050607",
     "08090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
     "72c91a36e135f8cf291ca894085c87e3cc15c439c9e43a3b", "a091d56e10400916"},
    {"RFC 3610 #3", "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf",
     "00000005040302a0a1a2a3a4a5", "0001020304050607",
     "08090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20",
     "51b1e5f44a197d1da46b0f8e2d282ae871e838bb64da859657", "4adaa76fbd9fb0c5"},
    {"RFC 3610 #4", "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf",
     "00000006050403a0a1a2a3a4a5", "000102030405060708090a0b",
     "0c0d0e0f101112131415161718191a1b1c1d1e",
     "a28c6865939a9a79faaa5c4c2a9d4a91cdac8c", "96c861b9c9e61ef1"},
    {"RFC 3610 #5", "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf",
     "00000007060504a0a1a2a3a4a5", "000102030405060708090a0b",
     "0c0d0e0f101112131415161718191a1b1c1d1e1f",
     "dcf1fb7b5d9e23fb9d4e131253658ad86ebdca3e", "51e83f077d9c2d93"},
    {"RFC 3610 #6", "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf",
     "00000008070605a0a1a2a3a4a5", "000102030405060708090a0b",
     "0c0d0e0f101112131415161718191a1b1c1d1e1f20",
     "6fc1b011f006568b5171a42d953d469b2570a4bd87", "405a0443ac91cb94"},
    {"RFC 3610 #13", "d7828d13b2b0bdc325a76236df93cc6b",
     "00412b4ea9cdbe3c9696766cfa", "0be1a88bace018b1",
     "08e8cf97d820ea258460e96ad9cf5289054d895ceac47c",
     "4cb97f86a2a4689a877947ab8091ef5386a6ffbdd080f8", "e78cf7cb0cddd7b3"},
    {"RFC 3610 #14", "d7828d13b2b0bdc325a76236df93cc6b",
     "0033568ef7b2633c9696766cfa", "63018f76dc8a1bcb",
     "9020ea6f91bdd85afa0039ba4baff9bfb79c7028949cd0ec",
     "4ccb1e7ca981befaa0726c55d378061298c85c92814abc33", "c52ee81d7d77c08a"},
    {"RFC 3610 #15", "d7828d13b2b0bdc325a76236df93cc6b",
     "00103fe41336713c9696766cfa", "aa6cfa36cae86b40",
     "b916e0eacc1c00d7dcec68ec0b3bbb1a02de8a2d1aa346132e",
     "b1d23a2220ddc0ac900d9aa03c61fcf4a559a4417767089708", "a776796edb723506"},
    {"RFC 3610 #16", "d7828d13b2b0bdc325a76236df93cc6b",
     "00764c63b8058e3c9696766cfa", "d0d0735c531e1becf049c244",
     "12daac5630efa5396f770ce1a66b21f7b2101c",
     "14d253c3967b70609b7cbb7c49916028324526", "9a6f49975bcadeaf"},
    {"RFC 3610 #17", "d7828d13b2b0bdc325a76236df93cc6b",
     "00f8b678094e3b3c9696766cfa", "77b60f011c03e1525899bcae",
     "e88b6a46c78d63e52eb8c546efb5de6f75e9cc0d",
     "5545ff1a085ee2efbf52b2e04bee1e2336c73e3f", "762c0c7744fe7e3c"},
    {"RFC 3610 #18", "d7828d13b2b0bdc325a76236df93cc6b",
     "00d560912d3f703c9696766cfa", "cd9044d2b71fdb8120ea60c0",
     "6435acbafb11a82e2f071d7ca4a5ebd93a803ba87f",
     "009769ecabdf48625594c59251e6035722675e04c8", "47099e5ae0704551"},
};

/// NIST CAVP CCM vectors (ccmtestvectors, VTT128.rsp): 13-byte nonce,
/// 32 bytes of associated data, 24 bytes of payload, 4 and 8-byte tags
constexpr CcmVector kCavpVectors[] = {
    {"CAVP VTT128 Tlen=4 Count=0", "43b1a6bc8d0d22d6d1ca95c18593cca5",
     "9882578e750b9682c6ca7f8f86",
     "2084f3861c9ad0ccee7c63a7e05aece5db8b34bd8724cc06b4ca99a7f9c4914f",
     "a2b381c7d1545c408fe29817a21dc435a154c87256346b05",
     "cc69ed76985e0ed4c8365a72775e5a19bfccc71aeb116c85", "a8c74677"},
    {"CAVP VTT128 Tlen=4 Count=1", "43b1a6bc8d0d22d6d1ca95c18593cca5",
     "9882578e750b9682c6ca7f8f86",
     "79db716e6b0b1627890d378c4560eba7871883d94527be3454dc3c257ea93556",
     "47f4cdd574264f48716d02d616cf27c759fdf787cdcd43b1",
     "292ea1643d2c1ddc36b9c0b3c38cb9eb4765f8ef70e84431", "676e2df1"},
    {"CAVP VTT128 Tlen=4 Count=2", "43b1a6bc8d0d22d6d1ca95c18593cca5",
     "9882578e750b9682c6ca7f8f86",
     "0d02778f90a164a4f9ada9dc7fd24eeb941069621418ef32c3f9ca6bf6fb2c4a",
     "5eadeaec29561244ede706b6eb30a1c371d74450a105c3f9",
     "3077865d605c40d0aa33c4d33e733fef6f4f4b381c20c479", "eb1321a1"},
    {"CAVP VTT128 Tlen=4 Count=3", "43b1a6bc8d0d22d6d1ca95c18593cca5",
     "9882578e750b9682c6ca7f8f86",
     "02e5a1306f612bdec098458cff3e691d93f050ba11ba627355dc7029d2cea5ab",
     "aac9fb69fed114c62db65090947096a2f5c85c271c6a6d53",
     "c41397d8b7db46526a6292f54133088eeb50534fa14f6ad3", "dd8cb4ca"},
    {"CAVP VTT128 Tlen=4 Count=4", "43b1a6bc8d0d22d6d1ca95c18593cca5",
     "9882578e750b9682c6ca7f8f86",
     "25144e807e389bb0e45b6dc25558caf61a2263869c4d0e4079d07674d7091110",
     "fb6e8d38ce38a8c1e710f3a33c682e6dabf055fb33fe75f8",
     "95b4e1898732fa55a0c431c6e92bb041b5685a938edb7278", "b659a844"},
    {"CAVP VTT128 Tlen=8 Count=20", "368f35a1f80eaaacd6bb136609389727",
     "842a8445847502ea77363a16b6",
     "34396dfcfa6f742aea7040976bd596497a7a6fa4fb85ee8e4ca394d02095b7bf",
     "1cccd55825316a94c5979e049310d1d717cdfb7624289dac",
     "1a58094f0e8c6035a5584bfa8d1009c5f78fd2ca487ff222", "f6d1d897d6051618"},
    {"CAVP VTT128 Tlen=8 Count=21", "368f35a1f80eaaacd6bb136609389727",
     "842a8445847502ea77363a16b6",
     "25865c1b89f1973bfa680d8458df35a56993a7e81e407e061794004068e481ab",
     "36004342dd74e7966692a848b2c11e1fc311eac9d9cef616",
     "30949f55f6c9ed37065d7db6acc1c60d2353c375b5999998", "ceca422687f41550"},
    {"CAVP VTT128 Tlen=8 Count=22", "368f35a1f80eaaacd6bb136609389727",
     "842a8445847502ea77363a16b6",
     "e6209480da9e49172ba58a9048f2f1b0349030e8e7a79dcdf295eecd613f401a",
     "e81f4fb360bcae372d8be3f32655a29bc10a2f31876173cc",
     "ee8b93a44b01a4964d44360d38557a892148068deb361c42", "d2b981fc741f2591"},
    {"CAVP VTT128 Tlen=8 Count=23", "368f35a1f80eaaacd6bb136609389727",
     "842a8445847502ea77363a16b6",
     "112c969882e685b4ae1ee6b67f680e6a1d9d840e627d12118f991c1a3d71314c",
     "27d6443e729d35d7a0690fcb7fe0b20892875f60b5d8763a",
     "2142982959203f76c0a6da3561e06a1a72c576dcd98f19b4", "a1fd47cd41fcf013"},
    {"CAVP VTT128 Tlen=8 Count=24", "368f35a1f80eaaacd6bb136609389727",
     "842a8445847502ea77363a16b6",
     "73ef62870c50faca5d4e6c6ec45fa7b54bf79ed229fcf1fc8c79c9c09596039b",
     "6c17ad5496dfccde8b877630e1e582dab52aaabe385a321f",
     "6a837143bd62c67feb48a3ceffe55ac855688302540d5d91", "43eb86ffa6958d71"},
};

/// NIST SP 800-38C appendix C examples 1 to 3. Their nonces are 7, 8 and 12
/// bytes long and their tags 4, 6 and 8 bytes.
constexpr CcmVector kSp80038cVectors[] = {
    {"SP 800-38C example 1", "404142434445464748494a4b4c4d4e4f",
     "10111213141516", "0001020304050607", "20212223", "7162015b", "4dac255d"},
    {"SP 800-38C example 2", "404142434445464748494a4b4c4d4e4f",
     "1011121314151617", "000102030405060708090a0b0c0d0e0f",
     "202122232425262728292a2b2c2d2e2f", "d2a1f0e051ea5f62081a7792073d593d",
     "1fc64fbfaccd"},
    {"SP 800-38C example 3", "404142434445464748494a4b4c4d4e4f",
     "101112131415161718191a1b", "000102030405060708090a0b0c0d0e0f10111213",
     "202122232425262728292a2b2c2d2e2f3031323334353637",
     "e3b201a9f5b71a7a9b1ceaeccd97e70b6176aad9a4428aa5", "484392fbc1b09951"},
};

/// One CMAC known answer
struct CmacVector {
    size_t message_size;  ///< Leading bytes of kRfc4493Message
    const char* mac;
};

/// RFC 4493 section 4 key, message and expected MACs
constexpr char kRfc4493Key[] = "2b7e151628aed2a6abf7158809cf4f3c";
constexpr char kRfc4493Message[] =
    "6bc1bee22e409f96e93d7e117393172a"
    "ae2d8a571e03ac9c9eb76fac45af8e51"
    "30c81c46a35ce411e5fbc1191a0a52ef"
    "f69f2445df4f9b17ad2b417be66c3710";
constexpr CmacVector kRfc4493Vectors[] = {
    {0, "bb1d6929e95937287fa37d129b756746"},
    {16, "070a16b46b4d4144f79bdd9dd04a287c"},
    {40, "dfa66747de9ae63030ca32611497c827"},
    {64, "51f0bebf7e3b9d92fc49741779363cfe"},
};

/// NIST SP 800-38A F.5.1 CTR-AES128. The key and the plaintext are those of
/// RFC 4493. F.1.1 gives the ECB encryption of the first plaintext block.
constexpr char kSp80038aCounter[] = "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff";
constexpr char kSp80038aCtrCiphertext[] =
    "874d6191b620e3261bef6864990db6ce"
    "9806f66b7970fdff8617187bb9fffdff"
    "5ae4df3edbd5d35e5b4f09020db03eab"
    "1e031dda2fbe03d1792170a0f3009cee";
constexpr char kSp80038aEcbFirstBlock[] = "3ad77bb40d7a3660a89ecaf32466ef97";

/// FIPS 197 appendix C.1 AES-128 block example
constexpr char kFips197Key[] = "000102030405060708090a0b0c0d0e0f";
constexpr char kFips197Plaintext[] = "00112233445566778899aabbccddeeff";
constexpr char kFips197Ciphertext[] = "69c4e0d86a7b0430d8cdb78070b4c55a";

}  // namespace test_vectors
}  // namespace security
}  // namespace loramesher
