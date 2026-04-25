//
// Copyright 2026 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef GRPC_TEST_CORE_TEST_UTIL_TEST_MEMORY_ALLOCATOR_H
#define GRPC_TEST_CORE_TEST_UTIL_TEST_MEMORY_ALLOCATOR_H

#include <grpc/event_engine/internal/memory_allocator_impl.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/slice.h>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {

class TestMemoryAllocatorImpl
    : public grpc_event_engine::experimental::internal::MemoryAllocatorImpl {
 public:
  TestMemoryAllocatorImpl() = default;

  size_t Reserve(
      grpc_event_engine::experimental::MemoryRequest request) override {
    size_t reserved = request.max();
    total_bytes_reserved_ += reserved;
    return reserved;
  }

  grpc_slice MakeSlice(
      grpc_event_engine::experimental::MemoryRequest request) override {
    size_t reserved = request.max();
    total_bytes_reserved_ += reserved;
    return grpc_slice_malloc(reserved);
  }

  void Release(size_t /*n*/) override {}

  void Shutdown() override {}

  size_t total_bytes_reserved() const { return total_bytes_reserved_; }

 private:
  size_t total_bytes_reserved_ = 0;
};

class TestMemoryAllocatorFactory
    : public grpc_event_engine::experimental::MemoryAllocatorFactory {
 public:
  struct RawPointerChannelArgTag {};

  TestMemoryAllocatorFactory() = default;

  grpc_event_engine::experimental::MemoryAllocator CreateMemoryAllocator(
      absl::string_view name) override {
    create_called_ = true;
    last_requested_name_ = std::string(name);

    auto test_impl = std::make_shared<TestMemoryAllocatorImpl>();
    last_created_impl_ = test_impl;
    return grpc_event_engine::experimental::MemoryAllocator(
        std::move(test_impl));
  }

  bool create_called() const { return create_called_; }
  const std::string& last_requested_name() const {
    return last_requested_name_;
  }
  std::shared_ptr<TestMemoryAllocatorImpl> last_created_impl() const {
    return last_created_impl_;
  }

 private:
  bool create_called_ = false;
  std::string last_requested_name_;
  std::shared_ptr<TestMemoryAllocatorImpl> last_created_impl_;
};

}  // namespace testing
}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_TEST_UTIL_TEST_MEMORY_ALLOCATOR_H
