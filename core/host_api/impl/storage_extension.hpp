/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef KAGOME_HOST_API_STORAGE_EXTENSION_HPP
#define KAGOME_HOST_API_STORAGE_EXTENSION_HPP

#include <cstdint>

#include "log/logger.hpp"
#include "runtime/types.hpp"
#include "storage/changes_trie/changes_tracker.hpp"

namespace kagome::runtime {
  class MemoryProvider;
  class TrieStorageProvider;
}  // namespace kagome::runtime

namespace kagome::host_api {
  /**
   * Implements HostApi functions related to storage
   */
  class StorageExtension {
   public:
    StorageExtension(
        std::shared_ptr<runtime::TrieStorageProvider> storage_provider,
        std::shared_ptr<const runtime::MemoryProvider> memory_provider,
        std::shared_ptr<storage::changes_trie::ChangesTracker> changes_tracker);

    void reset();

    // -------------------------Trie operations--------------------------

    /**
     * @see HostApi::ext_storage_read_version_1
     */
    runtime::WasmSpan ext_storage_read_version_1(runtime::WasmSpan key,
                                                 runtime::WasmSpan value_out,
                                                 runtime::WasmOffset offset);

    // ------------------------ VERSION 1 ------------------------

    /**
     * @see HostApi::ext_storage_set_version_1
     */
    void ext_storage_set_version_1(runtime::WasmSpan key,
                                   runtime::WasmSpan value);

    /**
     * @see HostApi::ext_storage_get_version_1
     */
    runtime::WasmSpan ext_storage_get_version_1(runtime::WasmSpan key);

    /**
     * @see HostApi::ext_storage_clear_version_1
     */
    void ext_storage_clear_version_1(runtime::WasmSpan key_data);

    /**
     * @see HostApi::ext_storage_exists_version_1
     */
    runtime::WasmSize ext_storage_exists_version_1(
        runtime::WasmSpan key_data) const;

    /**
     * @see HostApi::ext_storage_clear_prefix_version_1
     */
    void ext_storage_clear_prefix_version_1(runtime::WasmSpan prefix);

    /**
     * @see HostApi::ext_storage_clear_prefix_version_2
     */
    runtime::WasmSpan ext_storage_clear_prefix_version_2(
        runtime::WasmSpan prefix, runtime::WasmSpan limit);

    /**
     * @see HostApi::ext_storage_root_version_1
     */
    runtime::WasmSpan ext_storage_root_version_1() const;

    /**
     * @see HostApi::ext_storage_changes_root_version_1
     */
    runtime::WasmSpan ext_storage_changes_root_version_1(
        runtime::WasmSpan parent_hash);

    /**
     * @see HostApi::ext_storage_next_key
     */
    runtime::WasmSpan ext_storage_next_key_version_1(
        runtime::WasmSpan key) const;

    /**
     * @see HostApi::ext_storage_append_version_1
     */
    void ext_storage_append_version_1(runtime::WasmSpan key,
                                      runtime::WasmSpan value) const;

    /**
     * @see HostApi::ext_storage_start_transaction_version_1
     */
    void ext_storage_start_transaction_version_1();

    /**
     * @see HostApi::ext_storage_commit_transaction_version_1
     */
    void ext_storage_commit_transaction_version_1();

    /**
     * @see HostApi::ext_storage_rollback_transaction_version_1
     */
    void ext_storage_rollback_transaction_version_1();

    /**
     * @see HostApi::ext_trie_blake2_256_root_version_1
     */
    runtime::WasmPointer ext_trie_blake2_256_root_version_1(
        runtime::WasmSpan values_data);

    /**
     * @see HostApi::ext_trie_blake2_256_ordered_root_version_1
     */
    runtime::WasmPointer ext_trie_blake2_256_ordered_root_version_1(
        runtime::WasmSpan values_data);

   private:
    /**
     * Find the value by given key and the return the part of it starting from
     * given offset
     *
     * @param key Buffer representation of the key
     * @return result containing Buffer with the value
     */
    outcome::result<common::Buffer> get(const common::Buffer &key) const;

    /**
     * @return error if any, a key if the next key exists
     * none otherwise
     */
    outcome::result<std::optional<common::Buffer>> getStorageNextKey(
        const common::Buffer &key) const;

    std::optional<common::Buffer> calcStorageChangesRoot(
        common::Hash256 parent) const;

    runtime::WasmSpan clearPrefix(const common::Buffer &prefix,
                                  std::optional<uint32_t> limit);

    std::shared_ptr<runtime::TrieStorageProvider> storage_provider_;
    std::shared_ptr<const runtime::MemoryProvider> memory_provider_;
    std::shared_ptr<storage::changes_trie::ChangesTracker> changes_tracker_;
    log::Logger logger_;

    constexpr static auto kDefaultLoggerTag = "WASM Runtime [StorageExtension]";
  };

}  // namespace kagome::host_api

#endif  // KAGOME_STORAGE_HostApiS_HostApi_HPP
