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

#ifndef GRPC_TEST_CORE_FILTERS_FILTER_TEST_V2_H
#define GRPC_TEST_CORE_FILTERS_FILTER_TEST_V2_H

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <stddef.h>
#include <stdint.h>

#include <initializer_list>
#include <memory>
#include <optional>
#include <utility>

#include "src/core/call/message.h"
#include "src/core/call/metadata.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/filter/filter_args.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class FilterTestV2Base : public ::testing::Test {
 public:
  class Call;

  class Channel {
   private:
    struct Impl {
      Impl(std::unique_ptr<ChannelFilter> filter, FilterTestV2Base* test)
          : filter(std::move(filter)), test(test) {}
      RefCountedPtr<ArenaFactory> arena_factory = SimpleArenaAllocator();
      std::unique_ptr<ChannelFilter> filter;
      FilterTestV2Base* const test;
    };

   public:
    Call MakeCall();

   protected:
    explicit Channel(std::unique_ptr<ChannelFilter> filter,
                     FilterTestV2Base* test)
        : impl_(std::make_shared<Impl>(std::move(filter), test)) {}

    ChannelFilter* filter_ptr() { return impl_->filter.get(); }

   private:
    friend class FilterTestV2Base;
    friend class Call;

    std::shared_ptr<Impl> impl_;
  };

  // One "call" outstanding against this filter.
  // In reality - this filter is the only thing in the call.
  // Provides mocks to trap events that happen on the call.
  class Call {
   public:
    explicit Call(const Channel& channel);

    Call(const Call&) = delete;
    Call& operator=(const Call&) = delete;

    ~Call();

    // Construct client metadata in the arena of this call.
    // Optional argument is a list of key/value pairs to add to the metadata.
    ClientMetadataHandle NewClientMetadata(
        std::initializer_list<std::pair<absl::string_view, absl::string_view>>
            init = {});
    // Construct server metadata in the arena of this call.
    // Optional argument is a list of key/value pairs to add to the metadata.
    ServerMetadataHandle NewServerMetadata(
        std::initializer_list<std::pair<absl::string_view, absl::string_view>>
            init = {});
    // Construct a message in the arena of this call.
    MessageHandle NewMessage(absl::string_view payload = "",
                             uint32_t flags = 0);

    // Start the call.
    void Start(ClientMetadataHandle md);
    // Cancel the call.
    void Cancel();
    // Forward server initial metadata through this filter.
    void ForwardServerInitialMetadata(ServerMetadataHandle md);
    // Forward a message from client to server through this filter.
    void ForwardMessageClientToServer(MessageHandle msg);
    // Forward a message from server to client through this filter.
    void ForwardMessageServerToClient(MessageHandle msg);
    // Have the 'next' filter in the chain finish this call and return trailing
    // metadata.
    void FinishNextFilter(ServerMetadataHandle md);

    Arena* arena() const;

   private:
    friend class Channel;
    class ScopedContext;
    class Impl;

    std::shared_ptr<Impl> impl_;
  };

  struct Events {
    // Mock to trap starting the next filter in the chain.
    MOCK_METHOD(void, Started,
                (Call * call, const ClientMetadata& client_initial_metadata));
    // Mock to trap receiving server initial metadata in the next filter in the
    // chain.
    MOCK_METHOD(void, ForwardedServerInitialMetadata,
                (Call * call, const ServerMetadata& server_initial_metadata));
    // Mock to trap seeing a message forward from client to server.
    MOCK_METHOD(void, ForwardedMessageClientToServer,
                (Call * call, const Message& msg));
    // Mock to trap seeing a message forward from server to client.
    MOCK_METHOD(void, ForwardedMessageServerToClient,
                (Call * call, const Message& msg));
    // Mock to trap seeing a call finish in the next filter in the chain.
    MOCK_METHOD(void, Finished,
                (Call * call, const ServerMetadata& server_trailing_metadata));
  };

  ::testing::StrictMock<Events> events;

 protected:
  FilterTestV2Base();
  ~FilterTestV2Base() override;

  grpc_event_engine::experimental::EventEngine* event_engine() {
    return event_engine_.get();
  }

  void Step();

 private:
  std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
      event_engine_;
};

template <typename Filter>
class FilterTestV2 : public FilterTestV2Base {
 public:
  class Channel : public FilterTestV2Base::Channel {
   public:
    Filter* filter() { return static_cast<Filter*>(filter_ptr()); }

   private:
    friend class FilterTestV2<Filter>;
    using FilterTestV2Base::Channel::Channel;
  };

  absl::StatusOr<Channel> MakeChannel(
      const ChannelArgs& args,
      RefCountedPtr<const FilterConfig> config = nullptr) {
    auto filter = Filter::Create(
        args, ChannelFilter::Args(/*instance_id=*/0, std::move(config)));
    if (!filter.ok()) return filter.status();
    return Channel(std::move(*filter), this);
  }
};

}  // namespace grpc_core

// Expect one of the events corresponding to the methods in
// FilterTestV2::Events.
#define EXPECT_EVENT(event) EXPECT_CALL(events, event)

#endif  // GRPC_TEST_CORE_FILTERS_FILTER_TEST_V2_H
