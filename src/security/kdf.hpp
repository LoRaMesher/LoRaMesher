/**
 * @file kdf.hpp
 * @brief Derivation of the traffic keys from a network key or a node key
 */

#pragma once

#include "security/i_crypto_backend.hpp"

namespace loramesher {
namespace security {

/// Keys derived from the network key of one epoch
struct NetworkTrafficKeys {
    Aes128Key he;  ///< K_he, label LMHE: encryption of control frames
    Aes128Key hm;  ///< K_hm, label LMHM: MIC of control frames, hop MIC
    Aes128Key hp;  ///< K_hp, label LMHP: header mask of data frames
    Aes128Key ge;  ///< K_ge, label LMGE: encryption of group data bodies
    Aes128Key gm;  ///< K_gm, label LMGM: tag of group data bodies
};

/// Keys derived from a node key for one epoch
struct NodeTrafficKeys {
    Aes128Key ee;  ///< K_ee, label LMEE: encryption of end-to-end bodies
    Aes128Key em;  ///< K_em, label LMEM: tag of end-to-end bodies
    Aes128Key ke;  ///< K_ke, label LMKE: encryption of key message bodies
    Aes128Key km;  ///< K_km, label LMKM: tag and check value of the same
};

/// Keys derived from a node key that do not depend on the epoch
struct RecoveryKeys {
    Aes128Key re;  ///< K_re, label LMRE: encryption of the recovery grant
    Aes128Key rm;  ///< K_rm, label LMRM: tag of recovery request and grant
};

/**
 * @brief KDF(K, label, x) = AES-CMAC(K, label followed by x), full 16-byte output
 *
 * @param backend Crypto backend
 * @param parent Parent key K
 * @param label Four ASCII bytes, no terminator
 * @param x One byte, the epoch for traffic keys and 0 for recovery keys
 * @param key Receives the derived key
 * @return kOk, or kBackendError with key zeroed
 */
CryptoStatus DeriveKey(const ICryptoBackend& backend, const Aes128Key& parent,
                       const char label[4], uint8_t x, Aes128Key& key);

/**
 * @brief Derive K_he, K_hm, K_hp, K_ge and K_gm from K_net of an epoch
 */
CryptoStatus DeriveNetworkTrafficKeys(const ICryptoBackend& backend,
                                      const Aes128Key& network_key,
                                      uint8_t epoch, NetworkTrafficKeys& keys);

/**
 * @brief Derive K_ee, K_em, K_ke and K_km from K_node for an epoch
 */
CryptoStatus DeriveNodeTrafficKeys(const ICryptoBackend& backend,
                                   const Aes128Key& node_key, uint8_t epoch,
                                   NodeTrafficKeys& keys);

/**
 * @brief Derive K_re and K_rm from K_node
 */
CryptoStatus DeriveRecoveryKeys(const ICryptoBackend& backend,
                                const Aes128Key& node_key, RecoveryKeys& keys);

}  // namespace security
}  // namespace loramesher
