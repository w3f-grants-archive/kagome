/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef KAGOME_TEST_MOCK_CORE_CONSENSUS_GRANDPA_GRANDPA_MOCK_HPP
#define KAGOME_TEST_MOCK_CORE_CONSENSUS_GRANDPA_GRANDPA_MOCK_HPP

#include "consensus/grandpa/grandpa.hpp"
#include "consensus/grandpa/grandpa_observer.hpp"

#include <gmock/gmock.h>

namespace kagome::consensus::grandpa {

  class GrandpaMock : public Grandpa, public GrandpaObserver {
    using PeerId = libp2p::peer::PeerId;
    using CatchUpRequest = network::CatchUpRequest;
    using CatchUpResponse = network::CatchUpResponse;

   public:
    MOCK_METHOD(void,
                onNeighborMessage,
                (const libp2p::peer::PeerId &peer_id,
                 network::GrandpaNeighborMessage &&msg),
                (override));

    MOCK_METHOD(void,
                onVoteMessage,
                (std::optional<std::shared_ptr<GrandpaContext>> &&,
                 const PeerId &peer_id,
                 const VoteMessage &),
                (override));

    MOCK_METHOD(void,
                onCommitMessage,
                (std::optional<std::shared_ptr<GrandpaContext>> &&,
                 const PeerId &peer_id,
                 const network::FullCommitMessage &),
                (override));

    MOCK_METHOD(
        void,
        verifyJustification,
        (const GrandpaJustification &,
         const primitives::AuthoritySet &,
         std::shared_ptr<std::promise<outcome::result<void>>> promise_res),
        (override));

    MOCK_METHOD(void,
                applyJustification,
                (const GrandpaJustification &justification,
                 ApplyJustificationCb &&),
                (override));

    MOCK_METHOD(void, reload, (), (override));

    MOCK_METHOD(void,
                onCatchUpRequest,
                (const PeerId &peer_id, CatchUpRequest &&),
                (override));

    MOCK_METHOD(void,
                onCatchUpResponse,
                (std::optional<std::shared_ptr<GrandpaContext>> &&,
                 const PeerId &peer_id,
                 const CatchUpResponse &),
                (override));

    MOCK_METHOD(void,
                tryExecuteNextRound,
                (const std::shared_ptr<VotingRound> &),
                (override));

    MOCK_METHOD(void, updateNextRound, (RoundNumber round_number), (override));
  };

}  // namespace kagome::consensus::grandpa

#endif  // KAGOME_TEST_MOCK_CORE_CONSENSUS_GRANDPA_GRANDPA_MOCK_HPP
