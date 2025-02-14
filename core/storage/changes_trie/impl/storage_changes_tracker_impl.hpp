#ifndef KAGOME_STORAGE_CHANGES_TRIE_STORAGE_CHANGES_TRACKER_IMPL
#define KAGOME_STORAGE_CHANGES_TRIE_STORAGE_CHANGES_TRACKER_IMPL

#include "storage/changes_trie/changes_tracker.hpp"

#include <set>

#include "log/logger.hpp"
#include "primitives/event_types.hpp"

namespace kagome::storage::changes_trie {

  class StorageChangesTrackerImpl : public ChangesTracker {
   public:
    void onBlockAdded(
        const primitives::BlockHash &hash,
        const primitives::events::StorageSubscriptionEnginePtr
            &storage_sub_engine,
        const primitives::events::ChainSubscriptionEnginePtr &chain_sub_engine);

    void onPut(const common::BufferView &key,
               const common::BufferView &value,
               bool new_entry) override;
    void onRemove(const common::BufferView &key) override;

   private:
    std::set<common::Buffer, std::less<>>
        new_entries_;  // entries that do not yet exist in
                       // the underlying storage
    std::map<common::Buffer, std::optional<common::Buffer>, std::less<>>
        actual_val_;

    log::Logger logger_ =
        log::createLogger("Storage Changes Tracker", "changes_trie");
  };

}  // namespace kagome::storage::changes_trie

#endif  // KAGOME_STORAGE_CHANGES_TRIE_STORAGE_CHANGES_TRACKER_IMPL
