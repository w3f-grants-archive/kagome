/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef KAGOME_CRYPTO_EXTENSION_HPP
#define KAGOME_CRYPTO_EXTENSION_HPP

#include <future>
#include <optional>
#include <queue>

#include "crypto/crypto_store.hpp"
#include "log/logger.hpp"
#include "runtime/memory_provider.hpp"
#include "runtime/types.hpp"

namespace kagome::crypto {
  class Sr25519Provider;
  class EcdsaProvider;
  class Ed25519Provider;
  class Secp256k1Provider;
  class Hasher;
  class CryptoStore;
}  // namespace kagome::crypto

namespace kagome::host_api {
  /**
   * Implements extension functions related to cryptography
   */
  class CryptoExtension {
   public:
    static constexpr uint32_t kVerifyBatchSuccess = 1;
    static constexpr uint32_t kVerifyBatchFail = 0;
    static constexpr uint32_t kVerifySuccess = 1;
    static constexpr uint32_t kVerifyFail = 0;

    CryptoExtension(
        std::shared_ptr<const runtime::MemoryProvider> memory_provider,
        std::shared_ptr<const crypto::Sr25519Provider> sr25519_provider,
        std::shared_ptr<const crypto::EcdsaProvider> ecdsa_provider,
        std::shared_ptr<const crypto::Ed25519Provider> ed25519_provider,
        std::shared_ptr<const crypto::Secp256k1Provider> secp256k1_provider,
        std::shared_ptr<const crypto::Hasher> hasher,
        std::shared_ptr<crypto::CryptoStore> crypto_store);

    // -------------------- hashing methods v1 --------------------

    /**
     * @see HostApi::ext_hashing_keccak_256_version_1
     */
    runtime::WasmPointer ext_hashing_keccak_256_version_1(
        runtime::WasmSpan data);

    /**
     * @see HostApi::ext_hashing_sha2_256_version_1
     */
    runtime::WasmPointer ext_hashing_sha2_256_version_1(runtime::WasmSpan data);

    /**
     * @see HostApi::ext_hashing_blake2_128_version_1
     */
    runtime::WasmPointer ext_hashing_blake2_128_version_1(
        runtime::WasmSpan data);

    /**
     * @see HostApi::ext_hashing_blake2_256_version_1
     */
    runtime::WasmPointer ext_hashing_blake2_256_version_1(
        runtime::WasmSpan data);

    /**
     * @see HostApi::ext_hashing_twox_64_version_1
     */
    runtime::WasmPointer ext_hashing_twox_64_version_1(runtime::WasmSpan data);

    /**
     * @see HostApi::ext_hashing_twox_128_version_1
     */
    runtime::WasmPointer ext_hashing_twox_128_version_1(runtime::WasmSpan data);

    /**
     * @see HostApi::ext_hashing_twox_256_version_1
     */
    runtime::WasmPointer ext_hashing_twox_256_version_1(runtime::WasmSpan data);

    // -------------------- crypto methods v1 --------------------

    void ext_crypto_start_batch_verify_version_1();

    [[nodiscard]] runtime::WasmSize ext_crypto_finish_batch_verify_version_1();

    /**
     * @see HostApi::ext_crypto_ed25519_public_keys
     */
    runtime::WasmSpan ext_crypto_ed25519_public_keys_version_1(
        runtime::WasmSize key_type);

    /**
     *@see HostApi::ext_crypto_ed25519_generate
     */
    runtime::WasmPointer ext_crypto_ed25519_generate_version_1(
        runtime::WasmSize key_type, runtime::WasmSpan seed);

    /**
     * @see HostApi::ext_crypto_ed25519_sign
     */
    runtime::WasmSpan ext_crypto_ed25519_sign_version_1(
        runtime::WasmSize key_type,
        runtime::WasmPointer key,
        runtime::WasmSpan msg);

    /**
     * @see HostApi::ext_crypto_ed25519_verify
     */
    runtime::WasmSize ext_crypto_ed25519_verify_version_1(
        runtime::WasmPointer sig,
        runtime::WasmSpan msg,
        runtime::WasmPointer pubkey_data);

    /**
     * @see HostApi::ext_crypto_sr25519_public_keys
     */
    runtime::WasmSpan ext_crypto_sr25519_public_keys_version_1(
        runtime::WasmSize key_type);

    /**
     *@see HostApi::ext_crypto_sr25519_generate
     */
    runtime::WasmPointer ext_crypto_sr25519_generate_version_1(
        runtime::WasmSize key_type, runtime::WasmSpan seed);

    /**
     * @see HostApi::ext_crypto_sr25519_sign
     */
    runtime::WasmSpan ext_crypto_sr25519_sign_version_1(
        runtime::WasmSize key_type,
        runtime::WasmPointer key,
        runtime::WasmSpan msg);

    /**
     * @see HostApi::ext_crypto_sr25519_verify
     */
    int32_t ext_crypto_sr25519_verify_version_1(
        runtime::WasmPointer sig,
        runtime::WasmSpan msg,
        runtime::WasmPointer pubkey_data);

    int32_t ext_crypto_sr25519_verify_version_2(
        runtime::WasmPointer sig,
        runtime::WasmSpan msg,
        runtime::WasmPointer pubkey_data);

    /**
     * @see HostApi::ext_crypto_secp256k1_ecdsa_recover_version_1
     */
    runtime::WasmSpan ext_crypto_secp256k1_ecdsa_recover_version_1(
        runtime::WasmPointer sig, runtime::WasmPointer msg);

    /**
     * @see HostApi::ext_crypto_secp256k1_ecdsa_recover_compressed_version_1
     */
    runtime::WasmSpan ext_crypto_secp256k1_ecdsa_recover_compressed_version_1(
        runtime::WasmPointer sig, runtime::WasmPointer msg);

    /**
     * @see HostApi::ext_crypto_ecdsa_public_keys_version_1
     */
    runtime::WasmSpan ext_crypto_ecdsa_public_keys_version_1(
        runtime::WasmSize key_type);

    /**
     * @see HostApi::ext_crypto_ecdsa_sign_version_1
     */
    runtime::WasmSpan ext_crypto_ecdsa_sign_version_1(
        runtime::WasmSize key_type,
        runtime::WasmPointer key,
        runtime::WasmSpan msg);

    /**
     * @see HostApi::ext_crypto_ecdsa_sign_prehashed_version_1
     */
    runtime::WasmSpan ext_crypto_ecdsa_sign_prehashed_version_1(
        runtime::WasmSize key_type,
        runtime::WasmPointer key,
        runtime::WasmSpan msg);

    /**
     * @see HostApi::ext_crypto_ecdsa_generate
     */
    runtime::WasmPointer ext_crypto_ecdsa_generate_version_1(
        runtime::WasmSize key_type_id, runtime::WasmSpan seed) const;

    /**
     * @see HostApi::ext_crypto_ecdsa_verify
     */
    int32_t ext_crypto_ecdsa_verify_version_1(runtime::WasmPointer sig,
                                              runtime::WasmSpan msg,
                                              runtime::WasmPointer key) const;

    /**
     * @see HostApi::ext_crypto_ecdsa_verify_prehashed_version_1
     */
    int32_t ext_crypto_ecdsa_verify_prehashed_version_1(
        runtime::WasmPointer sig,
        runtime::WasmSpan msg,
        runtime::WasmPointer key) const;

   private:
    runtime::Memory &getMemory() const {
      return memory_provider_->getCurrentMemory()->get();
    }

    std::shared_ptr<const runtime::MemoryProvider> memory_provider_;
    std::shared_ptr<const crypto::Sr25519Provider> sr25519_provider_;
    std::shared_ptr<const crypto::EcdsaProvider> ecdsa_provider_;
    std::shared_ptr<const crypto::Ed25519Provider> ed25519_provider_;
    std::shared_ptr<const crypto::Secp256k1Provider> secp256k1_provider_;
    std::shared_ptr<const crypto::Hasher> hasher_;
    std::shared_ptr<crypto::CryptoStore> crypto_store_;
    log::Logger logger_;
  };
}  // namespace kagome::host_api

#endif  // KAGOME_CRYPTO_EXTENSION_HPP
