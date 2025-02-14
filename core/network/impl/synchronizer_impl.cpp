/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "network/impl/synchronizer_impl.hpp"

#include <random>

#include "application/app_configuration.hpp"
#include "blockchain/block_tree_error.hpp"
#include "consensus/babe/impl/babe_digests_util.hpp"
#include "consensus/grandpa/environment.hpp"
#include "consensus/grandpa/has_authority_set_change.hpp"
#include "network/helpers/peer_id_formatter.hpp"
#include "network/types/block_attributes.hpp"
#include "primitives/common.hpp"
#include "storage/predefined_keys.hpp"
#include "storage/trie/serialization/trie_serializer.hpp"
#include "storage/trie/trie_batches.hpp"
#include "storage/trie/trie_storage.hpp"

OUTCOME_CPP_DEFINE_CATEGORY(kagome::network, SynchronizerImpl::Error, e) {
  using E = kagome::network::SynchronizerImpl::Error;
  switch (e) {
    case E::SHUTTING_DOWN:
      return "Node is shutting down";
    case E::EMPTY_RESPONSE:
      return "Response is empty";
    case E::RESPONSE_WITHOUT_BLOCK_HEADER:
      return "Response does not contain header of some block";
    case E::RESPONSE_WITHOUT_BLOCK_BODY:
      return "Response does not contain body of some block";
    case E::DISCARDED_BLOCK:
      return "Block is discarded";
    case E::WRONG_ORDER:
      return "Wrong order of blocks/headers in response";
    case E::INVALID_HASH:
      return "Hash does not match";
    case E::ALREADY_IN_QUEUE:
      return "Block is already enqueued";
    case E::PEER_BUSY:
      return "Peer is busy";
    case E::ARRIVED_TOO_EARLY:
      return "Block is arrived too early. Try to process it late";
    case E::DUPLICATE_REQUEST:
      return "Duplicate of recent request has been detected";
  }
  return "unknown error";
}

namespace {
  constexpr const char *kImportQueueLength =
      "kagome_import_queue_blocks_submitted";
  constexpr uint32_t kBabeDigestBatch = 100;

  kagome::network::BlockAttributes attributesForSync(
      kagome::application::AppConfiguration::SyncMethod method) {
    using SM = kagome::application::AppConfiguration::SyncMethod;
    switch (method) {
      case SM::Full:
        return kagome::network::BlocksRequest::kBasicAttributes;
      case SM::Fast:
      case SM::FastWithoutState:
      case SM::Warp:
        return kagome::network::BlockAttribute::HEADER
             | kagome::network::BlockAttribute::JUSTIFICATION;
      case SM::Auto:
        UNREACHABLE;
    }
    return kagome::network::BlocksRequest::kBasicAttributes;
  }
}  // namespace

namespace kagome::network {

  SynchronizerImpl::SynchronizerImpl(
      const application::AppConfiguration &app_config,
      std::shared_ptr<application::AppStateManager> app_state_manager,
      std::shared_ptr<blockchain::BlockTree> block_tree,
      std::shared_ptr<blockchain::BlockStorage> block_storage,
      std::shared_ptr<consensus::babe::BlockHeaderAppender> block_appender,
      std::shared_ptr<consensus::babe::BlockExecutor> block_executor,
      std::shared_ptr<storage::trie::TrieSerializer> serializer,
      std::shared_ptr<storage::trie::TrieStorage> storage,
      std::shared_ptr<network::Router> router,
      std::shared_ptr<libp2p::basic::Scheduler> scheduler,
      std::shared_ptr<crypto::Hasher> hasher,
      std::shared_ptr<runtime::ModuleFactory> module_factory,
      std::shared_ptr<runtime::Core> core_api,
      primitives::events::ChainSubscriptionEnginePtr chain_sub_engine,
      std::shared_ptr<consensus::grandpa::Environment> grandpa_environment)
      : app_state_manager_(std::move(app_state_manager)),
        block_tree_(std::move(block_tree)),
        block_storage_{std::move(block_storage)},
        block_appender_(std::move(block_appender)),
        block_executor_(std::move(block_executor)),
        serializer_(std::move(serializer)),
        storage_(std::move(storage)),
        router_(std::move(router)),
        scheduler_(std::move(scheduler)),
        hasher_(std::move(hasher)),
        module_factory_(std::move(module_factory)),
        core_api_(std::move(core_api)),
        grandpa_environment_{std::move(grandpa_environment)},
        chain_sub_engine_(std::move(chain_sub_engine)) {
    BOOST_ASSERT(app_state_manager_);
    BOOST_ASSERT(block_tree_);
    BOOST_ASSERT(block_executor_);
    BOOST_ASSERT(serializer_);
    BOOST_ASSERT(storage_);
    BOOST_ASSERT(router_);
    BOOST_ASSERT(scheduler_);
    BOOST_ASSERT(hasher_);
    BOOST_ASSERT(module_factory_);
    BOOST_ASSERT(core_api_);
    BOOST_ASSERT(grandpa_environment_);
    BOOST_ASSERT(chain_sub_engine_);

    sync_method_ = app_config.syncMethod();

    // Register metrics
    metrics_registry_->registerGaugeFamily(
        kImportQueueLength, "Number of blocks submitted to the import queue");
    metric_import_queue_length_ =
        metrics_registry_->registerGaugeMetric(kImportQueueLength);
    metric_import_queue_length_->set(0);

    app_state_manager_->takeControl(*this);
  }

  /** @see AppStateManager::takeControl */
  void SynchronizerImpl::stop() {
    node_is_shutting_down_ = true;
  }

  bool SynchronizerImpl::subscribeToBlock(
      const primitives::BlockInfo &block_info, SyncResultHandler &&handler) {
    // Check if block is already in tree
    auto has = block_tree_->hasBlockHeader(block_info.hash);
    if (has and has.value()) {
      scheduler_->schedule(
          [handler = std::move(handler), block_info] { handler(block_info); });
      return false;
    }

    auto last_finalized_block = block_tree_->getLastFinalized();
    // Check if block from discarded side-chain
    if (last_finalized_block.number >= block_info.number) {
      scheduler_->schedule(
          [handler = std::move(handler)] { handler(Error::DISCARDED_BLOCK); });
      return false;
    }

    // Check if block has arrived too early
    auto best_block_res =
        block_tree_->getBestContaining(last_finalized_block.hash, std::nullopt);
    BOOST_ASSERT(best_block_res.has_value());
    const auto &best_block = best_block_res.value();
    if (best_block.number + kMaxDistanceToBlockForSubscription
        < block_info.number) {
      scheduler_->schedule([handler = std::move(handler)] {
        handler(Error::ARRIVED_TOO_EARLY);
      });
      return false;
    }

    subscriptions_.emplace(block_info, std::move(handler));
    return true;
  }

  void SynchronizerImpl::notifySubscribers(const primitives::BlockInfo &block,
                                           outcome::result<void> res) {
    auto range = subscriptions_.equal_range(block);
    for (auto it = range.first; it != range.second;) {
      auto cit = it++;
      if (auto node = subscriptions_.extract(cit)) {
        if (res.has_error()) {
          auto error = res.as_failure();
          scheduler_->schedule(
              [handler = std::move(node.mapped()), error] { handler(error); });
        } else {
          scheduler_->schedule(
              [handler = std::move(node.mapped()), block] { handler(block); });
        }
      }
    }
  }

  bool SynchronizerImpl::syncByBlockInfo(
      const primitives::BlockInfo &block_info,
      const libp2p::peer::PeerId &peer_id,
      Synchronizer::SyncResultHandler &&handler,
      bool subscribe_to_block) {
    // Subscribe on demand
    if (subscribe_to_block) {
      subscribeToBlock(block_info, std::move(handler));
    }

    // If provided block is already enqueued, just remember peer
    if (auto it = known_blocks_.find(block_info.hash);
        it != known_blocks_.end()) {
      auto &block_in_queue = it->second;
      block_in_queue.peers.emplace(peer_id);
      if (handler) {
        handler(block_info);
      }
      return false;
    }

    // We are communicating with one peer only for one issue.
    // If peer is already in use, don't start an additional issue.
    auto peer_is_busy = not busy_peers_.emplace(peer_id).second;
    if (peer_is_busy) {
      SL_TRACE(
          log_,
          "Can't syncByBlockHeader block {} is received from {}: Peer busy",
          block_info,
          peer_id);
      return false;
    }
    SL_TRACE(log_, "Peer {} marked as busy", peer_id);

    const auto &last_finalized_block = block_tree_->getLastFinalized();

    auto best_block_res =
        block_tree_->getBestContaining(last_finalized_block.hash, std::nullopt);
    BOOST_ASSERT(best_block_res.has_value());
    const auto &best_block = best_block_res.value();

    // Provided block is equal our best one. Nothing needs to do.
    if (block_info == best_block) {
      if (handler) {
        handler(block_info);
      }
      return false;
    }

    // First we need to find the best common block to avoid manipulations with
    // blocks what already exists on node.
    //
    // Find will be doing in interval between definitely known common block and
    // potentially unknown.
    //
    // Best candidate for lower bound is last finalized (it must be known for
    // all synchronized nodes).
    const auto lower = last_finalized_block.number;

    // Best candidate for upper bound is next potentially known block (next for
    // min of provided and our best)
    const auto upper = std::min(block_info.number, best_block.number) + 1;

    // Search starts with potentially known block (min of provided and our best)
    const auto hint = std::min(block_info.number, best_block.number);

    BOOST_ASSERT(lower < upper);

    // Callback what will be called at the end of finding the best common block
    auto find_handler =
        [wp = weak_from_this(), peer_id, handler = std::move(handler)](
            outcome::result<primitives::BlockInfo> res) mutable {
          if (auto self = wp.lock()) {
            // Remove peer from list of busy peers
            if (self->busy_peers_.erase(peer_id) > 0) {
              SL_TRACE(self->log_, "Peer {} unmarked as busy", peer_id);
            }

            // Finding the best common block was failed
            if (not res.has_value()) {
              if (handler) {
                handler(res.as_failure());
              }
              return;
            }

            // If provided block is already enqueued, just remember peer
            auto &block_info = res.value();
            if (auto it = self->known_blocks_.find(block_info.hash);
                it != self->known_blocks_.end()) {
              auto &block_in_queue = it->second;
              block_in_queue.peers.emplace(peer_id);
              if (handler) {
                handler(std::move(block_info));
              }
              return;
            }

            // Start to load blocks since found
            SL_DEBUG(self->log_,
                     "Start to load blocks from {} since block {}",
                     peer_id,
                     block_info);
            self->loadBlocks(peer_id, block_info, std::move(handler));
          }
        };

    // Find the best common block
    SL_DEBUG(log_,
             "Start to find common block with {} in #{}..#{} to catch up",
             peer_id,
             lower,
             upper);
    findCommonBlock(peer_id, lower, upper, hint, std::move(find_handler));
    return true;
  }

  bool SynchronizerImpl::syncByBlockHeader(
      const primitives::BlockHeader &header,
      const libp2p::peer::PeerId &peer_id,
      Synchronizer::SyncResultHandler &&handler) {
    auto block_hash = hasher_->blake2b_256(scale::encode(header).value());
    const primitives::BlockInfo block_info(header.number, block_hash);

    // Block was applied before
    if (block_tree_->getBlockHeader(block_hash).has_value()) {
      return false;
    }

    // Block is already enqueued
    if (auto it = known_blocks_.find(block_info.hash);
        it != known_blocks_.end()) {
      auto &block_in_queue = it->second;
      block_in_queue.peers.emplace(peer_id);
      return false;
    }

    // Number of provided block header greater currently watched.
    // Reset watched blocks list and start to watch the block with new number
    if (watched_blocks_number_ < header.number) {
      watched_blocks_number_ = header.number;
      watched_blocks_.clear();
    }
    // If number of provided block header is the same of watched, add handler
    // for this block
    if (watched_blocks_number_ == header.number) {
      watched_blocks_.emplace(block_hash, std::move(handler));
    }

    // If parent of provided block is in chain, start to load it immediately
    bool parent_is_known =
        known_blocks_.find(header.parent_hash) != known_blocks_.end()
        or block_tree_->getBlockHeader(header.parent_hash).has_value();

    if (parent_is_known) {
      loadBlocks(peer_id, block_info, [wp = weak_from_this()](auto res) {
        if (auto self = wp.lock()) {
          SL_TRACE(self->log_, "Block(s) enqueued to apply by announce");
        }
      });
      return true;
    }

    // Otherwise, is using base way to enqueue
    return syncByBlockInfo(
        block_info,
        peer_id,
        [wp = weak_from_this()](auto res) {
          if (auto self = wp.lock()) {
            SL_TRACE(self->log_, "Block(s) enqueued to load by announce");
          }
        },
        false);
  }

  void SynchronizerImpl::syncMissingJustifications(
      const PeerId &peer_id,
      primitives::BlockInfo target_block,
      std::optional<uint32_t> limit,
      Synchronizer::SyncResultHandler &&handler) {
    if (busy_peers_.find(peer_id) != busy_peers_.end()) {
      SL_DEBUG(
          log_,
          "Justifications load since block {} was rescheduled, peer {} is busy",
          target_block,
          peer_id);
      scheduler_->schedule([wp = weak_from_this(),
                            peer_id,
                            block = std::move(target_block),
                            limit = std::move(limit),
                            handler = std::move(handler)]() mutable {
        auto self = wp.lock();
        if (not self) {
          return;
        }
        self->syncMissingJustifications(
            peer_id, std::move(block), std::move(limit), std::move(handler));
      });
      return;
    }

    loadJustifications(
        peer_id, std::move(target_block), std::move(limit), std::move(handler));
  }

  void SynchronizerImpl::findCommonBlock(
      const libp2p::peer::PeerId &peer_id,
      primitives::BlockNumber lower,
      primitives::BlockNumber upper,
      primitives::BlockNumber hint,
      SyncResultHandler &&handler,
      std::map<primitives::BlockNumber, primitives::BlockHash> &&observed) {
    // Interrupts process if node is shutting down
    if (node_is_shutting_down_) {
      handler(Error::SHUTTING_DOWN);
      return;
    }

    network::BlocksRequest request{network::BlockAttribute::HEADER,
                                   hint,
                                   network::Direction::ASCENDING,
                                   1};

    auto request_fingerprint = request.fingerprint();

    if (auto r = recent_requests_.emplace(
            std::make_tuple(peer_id, request_fingerprint), "find common block");
        not r.second) {
      SL_VERBOSE(log_,
                 "Can't check if block #{} in #{}..#{} is common with {}: {}",
                 hint,
                 lower,
                 upper - 1,
                 peer_id,
                 r.first->second);
      handler(Error::DUPLICATE_REQUEST);
      return;
    }

    scheduleRecentRequestRemoval(peer_id, request_fingerprint);

    auto response_handler = [wp = weak_from_this(),
                             lower,
                             upper,
                             target = hint,
                             peer_id,
                             handler = std::move(handler),
                             observed = std::move(observed),
                             request_fingerprint](auto &&response_res) mutable {
      auto self = wp.lock();
      if (not self) {
        return;
      }

      // Any error interrupts finding common block
      if (response_res.has_error()) {
        SL_VERBOSE(self->log_,
                   "Can't check if block #{} in #{}..#{} is common with {}: {}",
                   target,
                   lower,
                   upper - 1,
                   peer_id,
                   response_res.error());
        handler(response_res.as_failure());
        return;
      }
      auto &blocks = response_res.value().blocks;

      // No block in response is abnormal situation. Requested block must be
      // existed because finding in interval of numbers of blocks that must
      // exist
      if (blocks.empty()) {
        SL_VERBOSE(self->log_,
                   "Can't check if block #{} in #{}..#{} is common with {}: "
                   "Response does not have any blocks",
                   target,
                   lower,
                   upper - 1,
                   peer_id);
        handler(Error::EMPTY_RESPONSE);
        self->recent_requests_.erase(std::tuple(peer_id, request_fingerprint));
        return;
      }

      auto hash = blocks.front().hash;

      observed.emplace(target, hash);

      for (;;) {
        // Check if block is known (is already enqueued or is in block tree)
        bool block_is_known =
            self->known_blocks_.find(hash) != self->known_blocks_.end()
            or self->block_tree_->getBlockHeader(hash).has_value();

        // Interval of finding is totally narrowed. Common block should be found
        if (target == lower) {
          if (block_is_known) {
            // Common block is found
            SL_DEBUG(self->log_,
                     "Found best common block with {}: {}",
                     peer_id,
                     BlockInfo(target, hash));
            handler(BlockInfo(target, hash));
            return;
          }

          // Common block is not found. It is abnormal situation. Requested
          // block must be existed because finding in interval of numbers of
          // blocks that must exist
          SL_WARN(self->log_, "Not found any common block with {}", peer_id);
          handler(Error::EMPTY_RESPONSE);
          return;
        }

        primitives::BlockNumber hint;

        // Narrowing interval for next iteration
        if (block_is_known) {
          SL_TRACE(self->log_,
                   "Block {} of {} is found locally",
                   BlockInfo(target, hash),
                   peer_id);

          // Narrowing interval to continue above
          lower = target;
          hint = lower + (upper - lower) / 2;
        } else {
          SL_TRACE(self->log_,
                   "Block {} of {} is not found locally",
                   BlockInfo(target, hash),
                   peer_id,
                   lower,
                   upper - 1);

          // Step for next iteration
          auto step = upper - target;

          // Narrowing interval to continue below
          upper = target;
          hint = upper - std::min(step, (upper - lower) / 2);
        }
        hint = lower + (upper - lower) / 2;

        // Try again with narrowed interval

        auto it = observed.find(hint);

        // This block number was observed early
        if (it != observed.end()) {
          target = hint;
          hash = it->second;

          SL_TRACE(
              self->log_,
              "Block {} of {} is already observed. Continue without request",
              BlockInfo(target, hash),
              peer_id);
          continue;
        }

        // This block number has not observed yet
        self->findCommonBlock(peer_id,
                              lower,
                              upper,
                              hint,
                              std::move(handler),
                              std::move(observed));
        break;
      }
    };

    SL_TRACE(log_,
             "Check if block #{} in #{}..#{} is common with {}",
             hint,
             lower,
             upper - 1,
             peer_id);

    auto protocol = router_->getSyncProtocol();
    BOOST_ASSERT_MSG(protocol, "Router did not provide sync protocol");
    protocol->request(peer_id, std::move(request), std::move(response_handler));
  }

  void SynchronizerImpl::loadBlocks(const libp2p::peer::PeerId &peer_id,
                                    primitives::BlockInfo from,
                                    SyncResultHandler &&handler) {
    // Interrupts process if node is shutting down
    if (node_is_shutting_down_) {
      if (handler) {
        handler(Error::SHUTTING_DOWN);
      }
      return;
    }

    network::BlocksRequest request{attributesForSync(sync_method_),
                                   from.hash,
                                   network::Direction::ASCENDING,
                                   std::nullopt};

    auto request_fingerprint = request.fingerprint();

    if (auto r = recent_requests_.emplace(
            std::make_tuple(peer_id, request_fingerprint), "load blocks");
        not r.second) {
      SL_ERROR(log_,
               "Can't load blocks from {} beginning block {}: {}",
               peer_id,
               from,
               r.first->second);
      if (handler) {
        handler(Error::DUPLICATE_REQUEST);
      }
      return;
    }

    scheduleRecentRequestRemoval(peer_id, request_fingerprint);

    auto response_handler = [wp = weak_from_this(),
                             from,
                             peer_id,
                             handler = std::move(handler),
                             parent_hash = primitives::BlockHash{}](
                                auto &&response_res) mutable {
      auto self = wp.lock();
      if (not self) {
        return;
      }

      // Any error interrupts loading of blocks
      if (response_res.has_error()) {
        SL_ERROR(self->log_,
                 "Can't load blocks from {} beginning block {}: {}",
                 peer_id,
                 from,
                 response_res.error());
        if (handler) {
          handler(response_res.as_failure());
        }
        return;
      }
      auto &blocks = response_res.value().blocks;

      // No block in response is abnormal situation.
      // At least one starting block should be returned as existing
      if (blocks.empty()) {
        SL_ERROR(self->log_,
                 "Can't load blocks from {} beginning block {}: "
                 "Response does not have any blocks",
                 peer_id,
                 from);
        if (handler) {
          handler(Error::EMPTY_RESPONSE);
        }
        return;
      }

      SL_TRACE(self->log_,
               "{} blocks are loaded from {} beginning block {}",
               blocks.size(),
               peer_id,
               from);

      bool some_blocks_added = false;
      primitives::BlockInfo last_loaded_block;

      for (auto &block : blocks) {
        // Check if header is provided
        if (not block.header.has_value()) {
          SL_ERROR(self->log_,
                   "Can't load blocks from {} starting from block {}: "
                   "Received block without header",
                   peer_id,
                   from);
          if (handler) {
            handler(Error::RESPONSE_WITHOUT_BLOCK_HEADER);
          }
          return;
        }
        // Check if body is provided
        if (not block.header.has_value()) {
          SL_ERROR(self->log_,
                   "Can't load blocks from {} starting from block {}: "
                   "Received block without body",
                   peer_id,
                   from);
          if (handler) {
            handler(Error::RESPONSE_WITHOUT_BLOCK_BODY);
          }
          return;
        }
        auto &header = block.header.value();

        const auto &last_finalized_block =
            self->block_tree_->getLastFinalized();

        // Check by number if block is not finalized yet
        if (last_finalized_block.number >= header.number) {
          if (last_finalized_block.number == header.number) {
            if (last_finalized_block.hash != block.hash) {
              SL_ERROR(self->log_,
                       "Can't load blocks from {} starting from block {}: "
                       "Received discarded block {}",
                       peer_id,
                       from,
                       BlockInfo(header.number, block.hash));
              if (handler) {
                handler(Error::DISCARDED_BLOCK);
              }
              return;
            }

            SL_TRACE(self->log_,
                     "Skip block {} received from {}: "
                     "it is finalized with block #{}",
                     BlockInfo(header.number, block.hash),
                     peer_id,
                     last_finalized_block.number);
            continue;
          }

          SL_TRACE(self->log_,
                   "Skip block {} received from {}: "
                   "it is below the last finalized block #{}",
                   BlockInfo(header.number, block.hash),
                   peer_id,
                   last_finalized_block.number);
          continue;
        }

        // Check if block is not discarded
        if (last_finalized_block.number + 1 == header.number) {
          if (last_finalized_block.hash != header.parent_hash) {
            SL_ERROR(self->log_,
                     "Can't complete blocks loading from {} starting from "
                     "block {}: Received discarded block {}",
                     peer_id,
                     from,
                     BlockInfo(header.number, header.parent_hash));
            if (handler) {
              handler(Error::DISCARDED_BLOCK);
            }
            return;
          }

          // Start to check parents
          parent_hash = header.parent_hash;
        }

        // Check if block is in chain
        static const primitives::BlockHash zero_hash;
        if (parent_hash != header.parent_hash && parent_hash != zero_hash) {
          SL_ERROR(self->log_,
                   "Can't complete blocks loading from {} starting from "
                   "block {}: Received block is not descendant of previous",
                   peer_id,
                   from);
          if (handler) {
            handler(Error::WRONG_ORDER);
          }
          return;
        }

        // Check if hash is valid
        auto calculated_hash =
            self->hasher_->blake2b_256(scale::encode(header).value());
        if (block.hash != calculated_hash) {
          SL_ERROR(self->log_,
                   "Can't complete blocks loading from {} starting from "
                   "block {}: "
                   "Received block whose hash does not match the header",
                   peer_id,
                   from);
          if (handler) {
            handler(Error::INVALID_HASH);
          }
          return;
        }

        last_loaded_block = {header.number, block.hash};

        parent_hash = block.hash;

        // Add block in queue and save peer or just add peer for existing record
        auto it = self->known_blocks_.find(block.hash);
        if (it == self->known_blocks_.end()) {
          self->known_blocks_.emplace(block.hash, KnownBlock{block, {peer_id}});
          self->metric_import_queue_length_->set(self->known_blocks_.size());
        } else {
          it->second.peers.emplace(peer_id);
          SL_TRACE(self->log_,
                   "Skip block {} received from {}: already enqueued",
                   BlockInfo(header.number, block.hash),
                   peer_id);
          continue;
        }

        SL_TRACE(self->log_,
                 "Enqueue block {} received from {}",
                 BlockInfo(header.number, block.hash),
                 peer_id);

        self->generations_.emplace(header.number, block.hash);
        self->ancestry_.emplace(header.parent_hash, block.hash);

        some_blocks_added = true;
      }

      SL_TRACE(self->log_, "Block loading is finished");
      if (handler) {
        handler(last_loaded_block);
      }

      if (some_blocks_added) {
        SL_TRACE(self->log_, "Enqueued some new blocks: schedule applying");
        self->scheduler_->schedule([wp] {
          if (auto self = wp.lock()) {
            self->applyNextBlock();
          }
        });
      }
    };

    auto protocol = router_->getSyncProtocol();
    BOOST_ASSERT_MSG(protocol, "Router did not provide sync protocol");
    protocol->request(peer_id, std::move(request), std::move(response_handler));
  }

  void SynchronizerImpl::loadJustifications(const libp2p::peer::PeerId &peer_id,
                                            primitives::BlockInfo target_block,
                                            std::optional<uint32_t> limit,
                                            SyncResultHandler &&handler) {
    if (node_is_shutting_down_) {
      if (handler) {
        handler(Error::SHUTTING_DOWN);
      }
      return;
    }

    busy_peers_.insert(peer_id);
    auto cleanup = gsl::finally([this, peer_id] {
      auto peer = busy_peers_.find(peer_id);
      if (peer != busy_peers_.end()) {
        busy_peers_.erase(peer);
      }
    });

    BlocksRequest request{
        BlockAttribute::HEADER | BlockAttribute::JUSTIFICATION,
        target_block.hash,
        Direction::ASCENDING,
        limit};

    auto request_fingerprint = request.fingerprint();
    if (auto r = recent_requests_.emplace(
            std::make_tuple(peer_id, request_fingerprint),
            "load justifications");
        not r.second) {
      SL_ERROR(log_,
               "Can't load justification from {} for block {}: Duplicate '{}' "
               "request",
               peer_id,
               target_block,
               r.first->second);
      if (handler) {
        handler(Error::DUPLICATE_REQUEST);
      }
      return;
    }

    scheduleRecentRequestRemoval(peer_id, request_fingerprint);

    auto response_handler = [wp = weak_from_this(),
                             peer_id,
                             target_block,
                             limit,
                             handler = std::move(handler)](
                                auto &&response_res) mutable {
      auto self = wp.lock();
      if (not self) {
        return;
      }

      if (response_res.has_error()) {
        SL_ERROR(self->log_,
                 "Can't load justification from {} for block {}: {}",
                 peer_id,
                 target_block,
                 response_res.error());
        if (handler) {
          handler(response_res.as_failure());
        }
        return;
      }

      auto &blocks = response_res.value().blocks;

      if (blocks.empty()) {
        SL_ERROR(self->log_,
                 "Can't load block justification from {} for block {}: "
                 "Response does not have any contents",
                 peer_id,
                 target_block);
        if (handler) {
          handler(Error::EMPTY_RESPONSE);
        }
        return;
      }

      // Use decreasing limit,
      // to avoid race between block and justification requests
      if (limit.has_value()) {
        if (blocks.size() >= limit.value()) {
          limit = 0;
        } else {
          limit.value() -= (blocks.size() - 1);
        }
      }

      bool justification_received = false;
      BlockInfo last_justified_block;
      BlockInfo last_observed_block;
      for (auto &block : blocks) {
        if (not block.header) {
          SL_ERROR(self->log_,
                   "No header was provided from {} for block {} while "
                   "requesting justifications",
                   peer_id,
                   target_block);
          if (handler) {
            handler(Error::RESPONSE_WITHOUT_BLOCK_HEADER);
          }
          return;
        }
        last_observed_block =
            primitives::BlockInfo{block.header->number, block.hash};
        if (block.justification) {
          justification_received = true;
          last_justified_block = last_observed_block;
          {
            std::lock_guard lock(self->justifications_mutex_);
            self->justifications_.emplace(last_justified_block,
                                          *block.justification);
          }
        }
      }

      if (justification_received) {
        SL_TRACE(self->log_, "Enqueued new justifications: schedule applying");
        self->scheduler_->schedule([wp] {
          if (auto self = wp.lock()) {
            self->applyNextJustification();
          }
        });
      }

      // Continue justifications requesting till limit is non-zero and last
      // observed block is not target (no block anymore)
      if ((not limit.has_value() or limit.value() > 0)
          and last_observed_block != target_block) {
        SL_TRACE(self->log_, "Request next block pack");
        self->scheduler_->schedule([wp,
                                    peer_id,
                                    target_block = last_observed_block,
                                    limit,
                                    handler = std::move(handler)]() mutable {
          if (auto self = wp.lock()) {
            self->loadJustifications(
                peer_id, target_block, limit, std::move(handler));
          }
        });
        return;
      }

      if (handler) {
        handler(last_justified_block);
      }
    };

    auto protocol = router_->getSyncProtocol();
    BOOST_ASSERT_MSG(protocol, "Router did not provide sync protocol");
    protocol->request(peer_id, std::move(request), std::move(response_handler));
  }

  void SynchronizerImpl::syncBabeDigest(const libp2p::peer::PeerId &peer_id,
                                        const primitives::BlockInfo &_block,
                                        CbResultVoid &&cb) {
    auto block = _block;
    // BabeConfigRepositoryImpl first block slot
    if (auto hash = block_tree_->getBlockHash(1);
        not hash or not block_tree_->getBlockHeader(hash.value())) {
      auto cb2 = [=, cb{std::move(cb)}, weak{weak_from_this()}](
                     outcome::result<BlocksResponse> _res) mutable {
        auto self = weak.lock();
        if (not self) {
          return;
        }
        if (not _res) {
          cb(_res.error());
          return;
        }
        auto &res = _res.value();
        if (res.blocks.empty()) {
          cb(Error::EMPTY_RESPONSE);
          return;
        }
        auto &header = res.blocks[0].header;
        if (not header) {
          cb(Error::RESPONSE_WITHOUT_BLOCK_HEADER);
          return;
        }
        if (header->number != 1) {
          cb(Error::INVALID_HASH);
          return;
        }
        if (header->parent_hash != block_tree_->getGenesisBlockHash()) {
          cb(Error::INVALID_HASH);
          return;
        }
        auto hash = self->block_storage_->putBlockHeader(*header).value();
        if (header->number < self->block_tree_->getLastFinalized().number) {
          self->block_storage_->assignNumberToHash({header->number, hash})
              .value();
        }
        self->syncBabeDigest(peer_id, block, std::move(cb));
      };
      router_->getSyncProtocol()->request(
          peer_id,
          {BlockAttribute::HEADER, 1, Direction::DESCENDING, 1},
          std::move(cb2));
      return;
    }
    // BabeConfigRepositoryImpl NextEpoch
    while (block.number != 0) {
      if (auto _header = block_tree_->getBlockHeader(block.hash)) {
        auto &header = _header.value();
        if (consensus::babe::getNextEpochDigest(header)) {
          break;
        }
        block = {header.number - 1, header.parent_hash};
        continue;
      }
      auto cb2 = [=, weak{weak_from_this()}, cb{std::move(cb)}](
                     outcome::result<BlocksResponse> _res) mutable {
        auto self = weak.lock();
        if (not self) {
          return;
        }
        if (not _res) {
          cb(_res.error());
          return;
        }
        auto &res = _res.value();
        if (res.blocks.empty()) {
          cb(Error::EMPTY_RESPONSE);
          return;
        }
        for (auto &item : res.blocks) {
          auto &header = item.header;
          if (not header) {
            cb(Error::RESPONSE_WITHOUT_BLOCK_HEADER);
            return;
          }
          primitives::BlockInfo info{
              header->number,
              self->hasher_->blake2b_256(scale::encode(*header).value())};
          if (info != block) {
            cb(Error::INVALID_HASH);
            return;
          }
          self->block_storage_->putBlockHeader(*header).value();
          if (block.number < self->block_tree_->getLastFinalized().number) {
            self->block_storage_->assignNumberToHash(block).value();
          }
          if (consensus::babe::getNextEpochDigest(*header)) {
            cb(outcome::success());
            return;
          }
          if (block.number != 0) {
            block = {header->number - 1, header->parent_hash};
          }
        }
        self->syncBabeDigest(peer_id, block, std::move(cb));
      };
      router_->getSyncProtocol()->request(peer_id,
                                          {
                                              BlockAttribute::HEADER,
                                              block.hash,
                                              Direction::DESCENDING,
                                              kBabeDigestBatch,
                                          },
                                          std::move(cb2));
      return;
    }
    cb(outcome::success());
  }

  void SynchronizerImpl::syncState(const libp2p::peer::PeerId &peer_id,
                                   const primitives::BlockInfo &block,
                                   SyncResultHandler &&handler) {
    std::unique_lock lock{state_sync_mutex_};
    if (state_sync_) {
      SL_TRACE(log_,
               "State sync request was not sent to {} for block {}: "
               "previous request in progress",
               peer_id,
               block);
      return;
    }
    auto _header = block_tree_->getBlockHeader(block.hash);
    if (not _header) {
      handler(_header.error());
      return;
    }
    auto &header = _header.value();
    if (storage_->getEphemeralBatchAt(header.state_root)) {
      handler(block);
      return;
    }
    if (not state_sync_flow_ or state_sync_flow_->blockInfo() != block) {
      state_sync_flow_.emplace(block, header);
    }
    state_sync_.emplace(StateSync{
        peer_id,
        std::move(handler),
    });
    entries_ = 0;
    SL_INFO(log_, "Sync of state for block {} has started", block);
    syncState();
  }

  void SynchronizerImpl::syncState() {
    SL_TRACE(log_,
             "State sync request has sent to {} for block {}",
             state_sync_->peer,
             state_sync_flow_->blockInfo());

    auto request = state_sync_flow_->nextRequest();

    auto protocol = router_->getStateProtocol();
    BOOST_ASSERT_MSG(protocol, "Router did not provide state protocol");

    auto response_handler = [wp = weak_from_this()](auto &&_res) mutable {
      auto self = wp.lock();
      if (not self) {
        return;
      }
      std::unique_lock lock{self->state_sync_mutex_};
      auto ok = self->syncState(lock, std::move(_res));
      if (not ok) {
        auto cb = std::move(self->state_sync_->cb);
        SL_WARN(self->log_, "State syncing failed with error: {}", ok.error());
        self->state_sync_.reset();
        lock.unlock();
        cb(ok.error());
      }
    };

    protocol->request(
        state_sync_->peer, std::move(request), std::move(response_handler));
  }

  outcome::result<void> SynchronizerImpl::syncState(
      std::unique_lock<std::mutex> &lock,
      outcome::result<StateResponse> &&_res) {
    OUTCOME_TRY(res, _res);
    OUTCOME_TRY(state_sync_flow_->onResponse(res));
    entries_ += res.entries[0].entries.size();
    if (not state_sync_flow_->complete()) {
      SL_TRACE(log_, "State syncing continues. {} entries loaded", entries_);
      syncState();
      return outcome::success();
    }
    OUTCOME_TRY(
        state_sync_flow_->commit(*module_factory_, *core_api_, *serializer_));
    auto block = state_sync_flow_->blockInfo();
    state_sync_flow_.reset();
    SL_INFO(log_, "State syncing block {} has finished.", block);
    chain_sub_engine_->notify(primitives::events::ChainEventType::kNewRuntime,
                              block.hash);

    auto cb = std::move(state_sync_->cb);
    state_sync_.reset();

    // State syncing has completed; Switch to the full syncing
    sync_method_ = application::AppConfiguration::SyncMethod::Full;
    lock.unlock();
    cb(block);
    return outcome::success();
  }

  void SynchronizerImpl::applyNextBlock() {
    if (generations_.empty()) {
      SL_TRACE(log_, "No block for applying");
      return;
    }

    bool false_val = false;
    if (not applying_in_progress_.compare_exchange_strong(false_val, true)) {
      SL_TRACE(log_, "Applying in progress");
      return;
    }
    SL_TRACE(log_, "Begin applying");
    auto cleanup = gsl::finally([this] {
      SL_TRACE(log_, "End applying");
      applying_in_progress_ = false;
    });

    primitives::BlockHash hash;

    while (true) {
      auto generation_node = generations_.extract(generations_.begin());
      if (generation_node) {
        hash = generation_node.mapped();
        break;
      }
      if (generations_.empty()) {
        SL_TRACE(log_, "No block for applying");
        return;
      }
    }

    if (auto it = known_blocks_.find(hash); it != known_blocks_.end()) {
      auto &block_data = it->second.data;
      BOOST_ASSERT(block_data.header.has_value());
      const BlockInfo block_info(block_data.header->number, block_data.hash);

      const auto &last_finalized_block = block_tree_->getLastFinalized();

      SyncResultHandler handler;

      if (watched_blocks_number_ == block_data.header->number) {
        if (auto wbn_node = watched_blocks_.extract(hash)) {
          handler = std::move(wbn_node.mapped());
        }
      }

      // Skip applied and finalized blocks and
      //  discard side-chain below last finalized
      if (block_data.header->number <= last_finalized_block.number) {
        auto header_res = block_tree_->getBlockHeader(hash);
        if (not header_res.has_value()) {
          auto n = discardBlock(block_data.hash);
          SL_WARN(
              log_,
              "Block {} {} not applied as discarded",
              block_info,
              n ? fmt::format("and {} others have", n) : fmt::format("has"));
          if (handler) {
            handler(Error::DISCARDED_BLOCK);
          }
        }

      } else {
        auto callback =
            [wself{weak_from_this()}, hash, handler{std::move(handler)}](
                auto &&block_addition_result) mutable {
              if (auto self = wself.lock()) {
                self->processBlockAdditionResult(
                    std::move(block_addition_result), hash, std::move(handler));
                self->postApplyBlock(hash);
              }
            };

        if (sync_method_ == application::AppConfiguration::SyncMethod::Full) {
          // Regular syncing
          primitives::Block block{
              .header = std::move(block_data.header.value()),
              .body = std::move(block_data.body.value()),
          };
          block_executor_->applyBlock(
              std::move(block), block_data.justification, std::move(callback));

        } else {
          // Fast syncing
          if (not state_sync_) {
            // Headers loading
            block_appender_->appendHeader(std::move(block_data.header.value()),
                                          block_data.justification,
                                          std::move(callback));

          } else {
            // State syncing in progress; Temporary discard all new blocks
            auto n = discardBlock(block_data.hash);
            SL_WARN(
                log_,
                "Block {} {} not applied as discarded: "
                "state syncing on block in progress",
                block_info,
                n ? fmt::format("and {} others have", n) : fmt::format("has"));
            if (handler) {
              handler(Error::DISCARDED_BLOCK);
            }
            return;
          }
        }
        return;
      }
    }
    postApplyBlock(hash);
  }

  void SynchronizerImpl::processBlockAdditionResult(
      outcome::result<void> &&block_addition_result,
      const primitives::BlockHash &hash,
      SyncResultHandler &&handler) {
    auto node = known_blocks_.extract(hash);
    if (node) {
      auto &block_data = node.mapped().data;
      auto &peers = node.mapped().peers;
      BOOST_ASSERT(block_data.header.has_value());
      const BlockInfo block_info(block_data.header->number, block_data.hash);

      notifySubscribers(block_info, block_addition_result);

      if (not block_addition_result.has_value()) {
        if (block_addition_result
            != outcome::failure(blockchain::BlockTreeError::BLOCK_EXISTS)) {
          auto n = discardBlock(block_data.hash);
          SL_WARN(log_,
                  "Block {} {} been discarded: {}",
                  block_info,
                  n ? fmt::format("and {} others have", n) : fmt::format("has"),
                  block_addition_result.error());
          if (handler) {
            handler(Error::DISCARDED_BLOCK);
          }
        } else {
          SL_DEBUG(log_, "Block {} is skipped as existing", block_info);
          if (handler) {
            handler(block_info);
          }
        }
      } else {
        telemetry_->notifyBlockImported(
            block_info, telemetry::BlockOrigin::kNetworkInitialSync);
        if (handler) {
          handler(block_info);
        }

        // Check if finality lag greater than justification saving interval
        static const BlockNumber kJustificationInterval = 512;
        static const BlockNumber kMaxJustificationLag = 5;
        auto last_finalized = block_tree_->getLastFinalized();
        if (consensus::grandpa::HasAuthoritySetChange{*block_data.header}
                .scheduled
            or (block_info.number - kMaxJustificationLag)
                       / kJustificationInterval
                   > last_finalized.number / kJustificationInterval) {
          //  Trying to substitute with justifications' request only
          for (const auto &peer_id : peers) {
            syncMissingJustifications(
                peer_id,
                last_finalized,
                kJustificationInterval * 2,
                [wp = weak_from_this(), last_finalized, block_info](auto res) {
                  if (auto self = wp.lock()) {
                    if (res.has_value()) {
                      SL_DEBUG(
                          self->log_,
                          "Loaded justifications for blocks in range {} - {}",
                          last_finalized,
                          res.value());
                      return;
                    }

                    SL_WARN(self->log_,
                            "Missing justifications between blocks {} and "
                            "{} was not loaded: {}",
                            last_finalized,
                            block_info.number,
                            res.error());
                  }
                });
          }
        }
      }
    }
  }

  void SynchronizerImpl::postApplyBlock(const primitives::BlockHash &hash) {
    ancestry_.erase(hash);

    auto minPreloadedBlockAmount =
        sync_method_ == application::AppConfiguration::SyncMethod::Full
            ? kMinPreloadedBlockAmount
            : kMinPreloadedBlockAmountForFastSyncing;

    if (known_blocks_.size() < minPreloadedBlockAmount) {
      SL_TRACE(log_,
               "{} blocks in queue: ask next portion of block",
               known_blocks_.size());
      askNextPortionOfBlocks();
    } else {
      SL_TRACE(log_, "{} blocks in queue", known_blocks_.size());
    }
    metric_import_queue_length_->set(known_blocks_.size());
    scheduler_->schedule([wp = weak_from_this()] {
      if (auto self = wp.lock()) {
        self->applyNextBlock();
      }
    });
  }

  void SynchronizerImpl::applyNextJustification() {
    // Operate over the same lock as for the whole blocks application
    bool false_val = false;
    if (not applying_in_progress_.compare_exchange_strong(false_val, true)) {
      SL_TRACE(log_, "Applying justification in progress");
      return;
    }
    SL_TRACE(log_, "Begin justification applying");
    auto cleanup = gsl::finally([this] {
      SL_TRACE(log_, "End justification applying");
      applying_in_progress_ = false;
    });

    std::queue<JustificationPair> justifications;
    {
      std::lock_guard lock(justifications_mutex_);
      justifications.swap(justifications_);
    }

    while (not justifications.empty()) {
      auto [block_info, justification] = std::move(justifications.front());
      const auto &block = block_info;  // SL_WARN compilation WA
      justifications.pop();
      grandpa_environment_->applyJustification(
          block_info, justification, [block, log{log_}](auto &&res) mutable {
            if (res.has_error()) {
              SL_WARN(log,
                      "Justification for block {} was not applied: {}",
                      block,
                      res.error());
            } else {
              SL_TRACE(log, "Applied justification for block {}", block);
            }
          });
    }
  }

  size_t SynchronizerImpl::discardBlock(
      const primitives::BlockHash &hash_of_discarding_block) {
    std::queue<primitives::BlockHash> queue;
    queue.emplace(hash_of_discarding_block);

    size_t affected = 0;
    while (not queue.empty()) {
      const auto &hash = queue.front();

      if (auto it = known_blocks_.find(hash); it != known_blocks_.end()) {
        auto number = it->second.data.header->number;
        notifySubscribers({number, hash}, Error::DISCARDED_BLOCK);

        known_blocks_.erase(it);
        affected++;
      }

      auto range = ancestry_.equal_range(hash);
      for (auto it = range.first; it != range.second; ++it) {
        queue.emplace(it->second);
      }
      ancestry_.erase(range.first, range.second);

      queue.pop();
    }

    metric_import_queue_length_->set(known_blocks_.size());
    return affected;
  }

  void SynchronizerImpl::prune(const primitives::BlockInfo &finalized_block) {
    // Remove blocks whose numbers less finalized one
    while (not generations_.empty()) {
      auto generation_node = generations_.extract(generations_.begin());
      if (generation_node) {
        const auto &number = generation_node.key();
        if (number >= finalized_block.number) {
          break;
        }
        const auto &hash = generation_node.mapped();
        notifySubscribers({number, hash}, Error::DISCARDED_BLOCK);

        known_blocks_.erase(hash);
        ancestry_.erase(hash);
      }
    }

    // Remove blocks whose numbers equal finalized one, excluding finalized
    // one
    auto range = generations_.equal_range(finalized_block.number);
    for (auto it = range.first; it != range.second;) {
      auto cit = it++;
      const auto &hash = cit->second;
      if (hash != finalized_block.hash) {
        discardBlock(hash);
      }
    }

    metric_import_queue_length_->set(known_blocks_.size());
  }

  void SynchronizerImpl::scheduleRecentRequestRemoval(
      const libp2p::peer::PeerId &peer_id,
      const BlocksRequest::Fingerprint &fingerprint) {
    scheduler_->schedule(
        [wp = weak_from_this(), peer_id, fingerprint] {
          if (auto self = wp.lock()) {
            self->recent_requests_.erase(std::tuple(peer_id, fingerprint));
          }
        },
        kRecentnessDuration);
  }

  void SynchronizerImpl::askNextPortionOfBlocks() {
    bool false_val = false;
    if (not asking_blocks_portion_in_progress_.compare_exchange_strong(
            false_val, true)) {
      SL_TRACE(log_, "Asking portion of blocks in progress");
      return;
    }
    SL_TRACE(log_, "Begin asking portion of blocks");

    for (auto g_it = generations_.rbegin(); g_it != generations_.rend();
         ++g_it) {
      const auto &hash = g_it->second;

      auto b_it = known_blocks_.find(hash);
      if (b_it == known_blocks_.end()) {
        SL_TRACE(log_,
                 "Block {} is unknown. Go to next one",
                 primitives::BlockInfo(g_it->first, hash));
        continue;
      }

      primitives::BlockInfo block_info(g_it->first, hash);

      auto &peers = b_it->second.peers;
      if (peers.empty()) {
        SL_TRACE(
            log_, "Block {} don't have any peer. Go to next one", block_info);
        continue;
      }

      for (auto p_it = peers.begin(); p_it != peers.end();) {
        auto cp_it = p_it++;

        auto peer_id = *cp_it;

        if (busy_peers_.find(peer_id) != busy_peers_.end()) {
          SL_TRACE(log_,
                   "Peer {} for block {} is busy",
                   peer_id,
                   primitives::BlockInfo(g_it->first, hash));
          continue;
        }

        busy_peers_.insert(peers.extract(cp_it));
        SL_TRACE(log_, "Peer {} marked as busy", peer_id);

        auto handler = [wp = weak_from_this(), peer_id](const auto &res) {
          if (auto self = wp.lock()) {
            if (self->busy_peers_.erase(peer_id) > 0) {
              SL_TRACE(self->log_, "Peer {} unmarked as busy", peer_id);
            }
            SL_TRACE(self->log_, "End asking portion of blocks");
            self->asking_blocks_portion_in_progress_ = false;
            if (not res.has_value()) {
              SL_DEBUG(self->log_,
                       "Loading next portion of blocks from {} is failed: {}",
                       peer_id,
                       res.error());
              return;
            }
            SL_DEBUG(self->log_,
                     "Portion of blocks from {} is loaded till {}",
                     peer_id,
                     res.value());
            if (self->known_blocks_.empty()) {
              self->askNextPortionOfBlocks();
            }
          }
        };

        if (sync_method_ == application::AppConfiguration::SyncMethod::Full) {
          auto lower = generations_.begin()->first;
          auto upper = generations_.rbegin()->first + 1;
          auto hint = generations_.rbegin()->first;

          SL_DEBUG(
              log_,
              "Start to find common block with {} in #{}..#{} to fill queue",
              peer_id,
              generations_.begin()->first,
              generations_.rbegin()->first);
          findCommonBlock(
              peer_id,
              lower,
              upper,
              hint,
              [wp = weak_from_this(), peer_id, handler = std::move(handler)](
                  outcome::result<primitives::BlockInfo> res) {
                if (auto self = wp.lock()) {
                  if (not res.has_value()) {
                    SL_DEBUG(self->log_,
                             "Can't load next portion of blocks from {}: {}",
                             peer_id,
                             res.error());
                    handler(res);
                    return;
                  }
                  auto &common_block_info = res.value();
                  SL_DEBUG(self->log_,
                           "Start to load next portion of blocks from {} "
                           "since block {}",
                           peer_id,
                           common_block_info);
                  self->loadBlocks(
                      peer_id, common_block_info, std::move(handler));
                }
              });
        } else {
          SL_DEBUG(log_,
                   "Start to load next portion of blocks from {} "
                   "since block {}",
                   peer_id,
                   block_info);
          loadBlocks(peer_id, block_info, std::move(handler));
        }
        return;
      }

      SL_TRACE(log_,
               "Block {} doesn't have appropriate peer. Go to next one",
               primitives::BlockInfo(g_it->first, hash));
    }

    SL_TRACE(log_, "End asking portion of blocks: none");
    asking_blocks_portion_in_progress_ = false;
  }

}  // namespace kagome::network
