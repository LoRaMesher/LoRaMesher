/**
 * @file kdf.cpp
 * @brief Derivation of the traffic keys from a network key or a node key
 */

#include "security/kdf.hpp"

namespace loramesher {
namespace security {

namespace {

/// One derived key: where it goes, its label and its x byte
struct Derivation {
    Aes128Key* key;
    const char* label;
    uint8_t x;
};

template <size_t N>
CryptoStatus DeriveAll(const ICryptoBackend& backend, const Aes128Key& parent,
                       const Derivation (&derivations)[N]) {
    for (const Derivation& derivation : derivations) {
        const CryptoStatus status = DeriveKey(backend, parent, derivation.label,
                                              derivation.x, *derivation.key);
        if (status != CryptoStatus::kOk) {
            return status;
        }
    }
    return CryptoStatus::kOk;
}

}  // namespace

CryptoStatus DeriveKey(const ICryptoBackend& backend, const Aes128Key& parent,
                       const char label[4], uint8_t x, Aes128Key& key) {
    const uint8_t input[5] = {
        static_cast<uint8_t>(label[0]), static_cast<uint8_t>(label[1]),
        static_cast<uint8_t>(label[2]), static_cast<uint8_t>(label[3]), x};
    return backend.Cmac(parent, input, key);
}

CryptoStatus DeriveNetworkTrafficKeys(const ICryptoBackend& backend,
                                      const Aes128Key& network_key,
                                      uint8_t epoch, NetworkTrafficKeys& keys) {
    const Derivation derivations[] = {{&keys.he, "LMHE", epoch},
                                      {&keys.hm, "LMHM", epoch},
                                      {&keys.hp, "LMHP", epoch},
                                      {&keys.ge, "LMGE", epoch},
                                      {&keys.gm, "LMGM", epoch}};
    return DeriveAll(backend, network_key, derivations);
}

CryptoStatus DeriveNodeTrafficKeys(const ICryptoBackend& backend,
                                   const Aes128Key& node_key, uint8_t epoch,
                                   NodeTrafficKeys& keys) {
    const Derivation derivations[] = {{&keys.ee, "LMEE", epoch},
                                      {&keys.em, "LMEM", epoch},
                                      {&keys.ke, "LMKE", epoch},
                                      {&keys.km, "LMKM", epoch}};
    return DeriveAll(backend, node_key, derivations);
}

CryptoStatus DeriveRecoveryKeys(const ICryptoBackend& backend,
                                const Aes128Key& node_key, RecoveryKeys& keys) {
    const Derivation derivations[] = {{&keys.re, "LMRE", 0x00},
                                      {&keys.rm, "LMRM", 0x00}};
    return DeriveAll(backend, node_key, derivations);
}

}  // namespace security
}  // namespace loramesher
