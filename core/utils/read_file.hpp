/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef KAGOME_UTILS_READ_FILE_HPP
#define KAGOME_UTILS_READ_FILE_HPP

#include <fstream>

#include "common/buffer.hpp"

namespace kagome {
  template <typename Out>
  bool readFile(Out &out, const std::string &path) {
    static_assert(sizeof(*out.data()) == 1);
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (not file.good()) {
      out.clear();
      return false;
    }
    out.resize(file.tellg());
    file.seekg(0);
    file.read(reinterpret_cast<char *>(out.data()), out.size());
    if (not file.good()) {
      out.clear();
      return false;
    }
    return true;
  }
}  // namespace kagome

#endif  // KAGOME_UTILS_READ_FILE_HPP
