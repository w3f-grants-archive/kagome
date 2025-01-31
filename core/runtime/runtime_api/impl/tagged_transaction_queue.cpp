/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "runtime/runtime_api/impl/tagged_transaction_queue.hpp"

#include "blockchain/block_tree.hpp"
#include "runtime/common/executor.hpp"

namespace kagome::runtime {

  TaggedTransactionQueueImpl::TaggedTransactionQueueImpl(
      std::shared_ptr<Executor> executor,
      LazySPtr<blockchain::BlockTree> block_tree)
      : executor_{std::move(executor)},
        block_tree_(std::move(block_tree)),
        logger_{log::createLogger("TaggedTransactionQueue", "runtime")} {
    BOOST_ASSERT(executor_);
  }

  outcome::result<TaggedTransactionQueue::TransactionValidityAt>
  TaggedTransactionQueueImpl::validate_transaction(
      primitives::TransactionSource source, const primitives::Extrinsic &ext) {
    auto block = block_tree_.get()->bestLeaf();
    SL_TRACE(logger_, "Validate transaction called at block {}", block);
    OUTCOME_TRY(result,
                executor_->callAt<primitives::TransactionValidity>(
                    block.hash,
                    "TaggedTransactionQueue_validate_transaction",
                    source,
                    ext,
                    block.hash));
    return TransactionValidityAt{block, std::move(result)};
  }

}  // namespace kagome::runtime
