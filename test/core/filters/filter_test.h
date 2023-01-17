// Copyright 2023 gRPC authors.
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

#ifndef TEST_CORE_FILTERS_FILTER_TEST_H
#define TEST_CORE_FILTERS_FILTER_TEST_H

#include <stddef.h>
#include <stdint.h>

#include <initializer_list>
#include <iosfwd>
#include <memory>
#include <ostream>
#include <string>
#include <utility>

#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"

#include <grpc/event_engine/memory_allocator.h>

#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "test/core/filters/filter_test.h"

MATCHER_P2(HasMetadataKeyValue, key, value, "") {
  std::string temp;
  auto r = arg.GetStringValue(key, &temp);
  return r == value;
}

MATCHER_P(HasMessageFlags, value, "") { return arg.flags() == value; }

MATCHER_P(HasMessagePayload, value, "") {
  return arg.payload()->JoinIntoString() == value;
}

namespace grpc_core {

inline std::ostream& operator<<(std::ostream& os,
                                const grpc_metadata_batch& md) {
  return os << md.DebugString();
}

inline std::ostream& operator<<(std::ostream& os, const Message& msg) {
  return os << "flags:" << msg.flags()
            << " payload:" << absl::CEscape(msg.payload()->JoinIntoString());
}

class FilterTest {
 private:
  struct Channel {
    explicit Channel(std::unique_ptr<ChannelFilter> filter)
        : filter(std::move(filter)) {}
    size_t initial_arena_size = 1024;
    MemoryAllocator memory_allocator =
        ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator("test");
    std::unique_ptr<ChannelFilter> filter;
  };

 public:
  class Call {
   public:
    explicit Call(const FilterTest& test);
    ~Call();

    Call(const Call&) = delete;
    Call& operator=(const Call&) = delete;

    ClientMetadataHandle NewClientMetadata(
        std::initializer_list<std::pair<absl::string_view, absl::string_view>>
            init = {});
    ServerMetadataHandle NewServerMetadata(
        std::initializer_list<std::pair<absl::string_view, absl::string_view>>
            init = {});
    MessageHandle NewMessage(absl::string_view payload = "",
                             uint32_t flags = 0);

    void Start(ClientMetadataHandle md);
    void Cancel();
    void ForwardServerInitialMetadata(ServerMetadataHandle md);
    void ForwardMessageClientToServer(MessageHandle msg);
    void ForwardMessageServerToClient(MessageHandle msg);
    void FinishNextFilter(ServerMetadataHandle md);

    void Step();

    MOCK_METHOD(void, Started, (const ClientMetadata& client_initial_metadata));
    MOCK_METHOD(void, ForwardedServerInitialMetadata,
                (const ServerMetadata& server_initial_metadata));
    MOCK_METHOD(void, ForwardedMessageClientToServer, (const Message& msg));
    MOCK_METHOD(void, ForwardedMessageServerToClient, (const Message& msg));
    MOCK_METHOD(void, Finished,
                (const ServerMetadata& server_trailing_metadata));

   private:
    class ScopedContext;
    class Impl;

    std::unique_ptr<Impl> impl_;
  };

  template <typename Filter>
  explicit FilterTest(Filter filter)
      : channel_(std::make_shared<Channel>(
            std::make_unique<Filter>(std::move(filter)))) {}

  void set_initial_arena_size(size_t size) {
    channel_->initial_arena_size = size;
  }

 private:
  std::shared_ptr<Channel> channel_;
};

}  // namespace grpc_core

#endif
