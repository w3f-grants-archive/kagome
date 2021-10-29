/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef KAGOME_BLOCKS_REQUEST_HPP
#define KAGOME_BLOCKS_REQUEST_HPP

#include <cstdint>

#include <gsl/span>
#include <optional>
#include "network/types/block_attributes.hpp"
#include "network/types/block_direction.hpp"
#include "primitives/block_id.hpp"
#include "primitives/common.hpp"

namespace kagome::network {
  /**
   * Request for blocks to another peer
   */
  struct BlocksRequest {
    /// unique request id
    primitives::BlocksRequestId id;
    /// bits, showing, which parts of BlockData to return
    BlockAttributes fields{};
    /// start from this block
    primitives::BlockId from{};
    /// end at this block; an implementation defined maximum is used when
    /// unspecified
    std::optional<primitives::BlockHash> to{};
    /// sequence direction
    Direction direction{};
    /// maximum number of blocks to return; an implementation defined maximum is
    /// used when unspecified
    std::optional<uint32_t> max{};

    /// includes HEADER, BODY and JUSTIFICATION
    static constexpr BlockAttributes kBasicAttributes =
        BlockAttribute::HEADER | BlockAttribute::BODY
        | BlockAttribute::JUSTIFICATION;

    bool attributeIsSet(const BlockAttribute &attribute) const {
      return fields & attribute;
    }
  };
}  // namespace kagome::network

#endif  // KAGOME_BLOCKS_REQUEST_HPP
