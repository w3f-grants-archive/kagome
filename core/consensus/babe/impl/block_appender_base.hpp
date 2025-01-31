/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef KAGOME_BLOCK_APPENDER_BASE_HPP
#define KAGOME_BLOCK_APPENDER_BASE_HPP

#include "consensus/grandpa/environment.hpp"
#include "log/logger.hpp"
#include "outcome/outcome.hpp"
#include "primitives/block_data.hpp"

namespace kagome::blockchain {
  class BlockTree;
  class DigestTracker;
}  // namespace kagome::blockchain

namespace kagome::crypto {
  class Hasher;
}

namespace kagome::consensus::babe {

  class BlockValidator;
  class BabeConfigRepository;
  class ConsistencyKeeper;
  class ConsistencyGuard;
  class BabeUtil;

  /**
   * Common logic for adding a new block to the blockchain
   */
  class BlockAppenderBase {
   public:
    using ApplyJustificationCb = grandpa::Environment::ApplyJustificationCb;
    BlockAppenderBase(std::shared_ptr<ConsistencyKeeper> consistency_keeper,
                      std::shared_ptr<blockchain::BlockTree> block_tree,
                      std::shared_ptr<blockchain::DigestTracker> digest_tracker,
                      std::shared_ptr<BabeConfigRepository> babe_config_repo,
                      std::shared_ptr<BlockValidator> block_validator,
                      std::shared_ptr<grandpa::Environment> grandpa_environment,
                      std::shared_ptr<BabeUtil> babe_util,
                      std::shared_ptr<crypto::Hasher> hasher);

    primitives::BlockContext makeBlockContext(
        const primitives::BlockHeader &header) const;

    void applyJustifications(
        const primitives::BlockInfo &block_info,
        const std::optional<primitives::Justification> &new_justification,
        ApplyJustificationCb &&callback);

    outcome::result<ConsistencyGuard> observeDigestsAndValidateHeader(
        const primitives::Block &block,
        const primitives::BlockContext &context);

    struct SlotInfo {
      BabeTimePoint start;
      BabeDuration duration;
    };

    outcome::result<SlotInfo> getSlotInfo(
        const primitives::BlockHeader &header) const;

   private:
    log::Logger logger_ = log::createLogger("BlockAppender", "babe");

    // Justifications stored for future application (because a justification may
    // contain votes for higher blocks, which we have not received yet)
    using PostponedJustifications =
        std::map<primitives::BlockInfo, primitives::Justification>;
    std::shared_ptr<PostponedJustifications> postponed_justifications_;

    std::shared_ptr<ConsistencyKeeper> consistency_keeper_;
    std::shared_ptr<blockchain::BlockTree> block_tree_;
    std::shared_ptr<blockchain::DigestTracker> digest_tracker_;
    std::shared_ptr<BabeConfigRepository> babe_config_repo_;
    std::shared_ptr<BlockValidator> block_validator_;
    std::shared_ptr<grandpa::Environment> grandpa_environment_;
    std::shared_ptr<BabeUtil> babe_util_;
    std::shared_ptr<crypto::Hasher> hasher_;
  };

}  // namespace kagome::consensus::babe

#endif  // KAGOME_BLOCK_APPENDER_BASE_HPP
