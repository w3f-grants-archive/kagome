/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef KAGOME_CORE_STORAGE_TRIE_IMPL_POLKADOT_TRIE_FACTORY_IMPL
#define KAGOME_CORE_STORAGE_TRIE_IMPL_POLKADOT_TRIE_FACTORY_IMPL

#include "storage/trie/polkadot_trie/polkadot_trie_factory.hpp"
#include "storage/trie/polkadot_trie/polkadot_trie_impl.hpp"

namespace kagome::storage::trie {

  class PolkadotTrieFactoryImpl : public PolkadotTrieFactory {
   public:
    std::unique_ptr<PolkadotTrie> createEmpty(
        PolkadotTrie::NodeRetrieveFunctor f) const override;
    std::shared_ptr<PolkadotTrie> createFromRoot(
        PolkadotTrie::NodePtr root,
        PolkadotTrie::NodeRetrieveFunctor f) const override;
  };

}  // namespace kagome::storage::trie

#endif  // KAGOME_CORE_STORAGE_TRIE_IMPL_POLKADOT_TRIE_FACTORY_IMPL
