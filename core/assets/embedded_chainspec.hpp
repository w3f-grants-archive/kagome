/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef KAGOME_ASSETS_EMBEDDED_CHAINSPEC_HPP
#define KAGOME_ASSETS_EMBEDDED_CHAINSPEC_HPP

#include <optional>
#include <string_view>

#include "assets/assets.hpp"

namespace kagome::assets {
  inline std::optional<std::string_view> getEmbeddedChainspec(
      std::string_view name) {
    const char *res = nullptr;
    if (name == "polkadot") {
      res = embedded_chainspec_polkadot;
    } else if (name == "kusama") {
      res = embedded_chainspec_kusama;
    } else if (name == "rococo") {
      res = embedded_chainspec_rococo;
    } else if (name == "westend") {
      res = embedded_chainspec_westend;
    }
    if (res == nullptr) {
      return std::nullopt;
    }
    return res;
  }
}  // namespace kagome::assets

#endif  // KAGOME_ASSETS_EMBEDDED_CHAINSPEC_HPP
