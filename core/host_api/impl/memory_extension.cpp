/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <exception>

#include <boost/assert.hpp>

#include "host_api/impl/memory_extension.hpp"
#include "runtime/memory_provider.hpp"

namespace kagome::host_api {
  MemoryExtension::MemoryExtension(
      std::shared_ptr<const runtime::MemoryProvider> memory_provider)
      : memory_provider_(std::move(memory_provider)),
        logger_{log::createLogger("MemoryExtension", "memory_extension")} {
    BOOST_ASSERT_MSG(memory_provider_ != nullptr, "memory provider is nullptr");
  }

  runtime::WasmPointer MemoryExtension::ext_allocator_malloc_version_1(
      runtime::WasmSize size) {
    return memory_provider_->getCurrentMemory()->get().allocate(size);
  }

  void MemoryExtension::ext_allocator_free_version_1(runtime::WasmPointer ptr) {
    auto opt_size = memory_provider_->getCurrentMemory()->get().deallocate(ptr);
    if (not opt_size) {
      logger_->warn(
          "Ptr {} does not point to any memory chunk in wasm memory. Nothing "
          "deallocated",
          ptr);
      return;
    }
  }
}  // namespace kagome::host_api
