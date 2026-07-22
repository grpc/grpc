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

#ifndef GRPC_TEST_CORE_FILTERS_FILTER_TEST_H
#define GRPC_TEST_CORE_FILTERS_FILTER_TEST_H

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <stddef.h>
#include <stdint.h>

#include <initializer_list>
#include <memory>
#include <optional>
#include <queue>
#include <utility>

#include "src/core/call/call_spine.h"
#include "src/core/call/interception_chain.h"
#include "src/core/call/message.h"
#include "src/core/call/metadata.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/filter/filter_args.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/promise/detail/promise_factory.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/call/yodel/yodel_test.h"
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

// FilterTest: the test harness for filters and interceptors written against
// the v3 filter API.
//
// A stack is built with `InterceptionChainBuilder` -- the same builder
// production code uses -- so the harness can host both v3 filters (a class with
// a nested `Call`) and v3 interceptors (a subclass of `Interceptor`). The chain
// bottoms out in a destination owned by the harness that queues up every call
// started through it, which is what lets us test interceptors that create more
// than one child call.
//
// Because it is built on the yodel harness, every test is also a fuzz target:
// the FuzzingEventEngine perturbs scheduling and event interleaving across
// runs.
//
// A test suite is a class deriving from FilterTest; individual tests are
// declared with FILTER_TEST(suite, name), which reads like TEST_F():
//
//   class MyFilterTest : public FilterTest {
//    protected:
//     using FilterTest::FilterTest;  // required: FILTER_TEST() constructs this
//
//     absl::Status Init() { return InitChannel<MyFilter>(MyArgs()); }
//   };
//
//   FILTER_TEST(MyFilterTest, UnaryRpc) {
//     ASSERT_TRUE(Init().ok());
//     auto [initiator, handler] = StartCallForFilter(NewClientMetadata());
//     PushClientMessage(NewMessage("hello"));
//     PushClientHalfClose();
//     EXPECT_THAT(PullClientMessage(handler).value(),
//                 HasMessagePayload("hello"));
//     PushServerTrailingMetadata(handler, ServerMetadataFromStatus(...));
//     EXPECT_THAT(**PullServerTrailingMetadata(), HasMetadataResult(...));
//     WaitForAllPendingWork();
//   }
class FilterTest : public YodelTest {
 public:
  using YodelTest::YodelTest;

  // The two ends of a call started through a filter. Note that `initiator` and
  // `handler` are structured bindings at the call site, so lambdas must capture
  // them by init-capture (`[initiator = initiator]`) rather than plain capture.
  struct StartedCall {
    CallInitiator initiator;
    CallHandler handler;
  };

 protected:
  // Building the stack under test

  // Build a stack containing exactly the one filter or interceptor under test.
  // Returns the build status so tests can assert on construction failures.
  // Must be called exactly once, before starting a call.
  template <typename T>
  absl::Status InitChannel(const ChannelArgs& args = ChannelArgs(),
                           RefCountedPtr<const FilterConfig> config = nullptr) {
    InterceptionChainBuilder builder(WithTestChannelArgs(args));
    builder.Add<T>(std::move(config));
    return FinishInitChannel(builder);
  }

  // Starting calls

  // Start a call through the stack under test and return the client end of it.
  // Only the initiator is returned: an interceptor may start any number of
  // child calls (or none), so handlers are collected separately by
  // GetHandler(). Must be called exactly once per test.
  CallInitiator StartCall(ClientMetadataHandle client_initial_metadata);

  // Return the handler for the next call started against the bottom of the
  // stack, ticking the event engine until one appears. Call once per child
  // call the filter or interceptor under test is expected to create.
  CallHandler GetHandler();

  // Convenience for the filter case, where a call always creates exactly one
  // child call: StartCall() followed by exactly one GetHandler().
  StartedCall StartCallForFilter(ClientMetadataHandle client_initial_metadata);

  // The initiator of the call started by StartCall(). All the client-side
  // Push/Pull helpers below act on it, since there is only ever one.
  CallInitiator initiator();

  // Driving the six call operations.
  //
  // Push*() is asynchronous and serializes operations onto the call's party in
  // FIFO order.
  //
  // Pull*() is synchronous and ticks the event engine until the value arrives.

  // client -> server
  void PushClientMessage(MessageHandle message);
  void PushClientHalfClose();
  ValueOrFailure<ClientMetadataHandle> PullClientInitialMetadata(
      CallHandler handler);
  ClientToServerNextMessage PullClientMessage(CallHandler handler);
  // True iff the client->server stream ended cleanly (i.e. the client
  // half-closed rather than the call failing).
  bool PullClientHalfClose(CallHandler handler);

  // server -> client
  void PushServerInitialMetadata(CallHandler handler, ServerMetadataHandle md);
  void PushServerMessage(CallHandler handler, MessageHandle message);
  void PushServerTrailingMetadata(CallHandler handler, ServerMetadataHandle md);
  ValueOrFailure<std::optional<ServerMetadataHandle>>
  PullServerInitialMetadata();
  ServerToClientNextMessage PullServerMessage();
  ValueOrFailure<ServerMetadataHandle> PullServerTrailingMetadata();

  // Constructing the things that flow through a call

  ClientMetadataHandle NewClientMetadata(
      std::initializer_list<std::pair<absl::string_view, absl::string_view>>
          init = {});
  ServerMetadataHandle NewServerMetadata(
      std::initializer_list<std::pair<absl::string_view, absl::string_view>>
          init = {});
  MessageHandle NewMessage(absl::string_view payload = "", uint32_t flags = 0);

  // Called with the call's arena immediately after the call is created and
  // before it is started. Override to install call context objects (for
  // example ServiceConfigCallData) that the filter under test requires.
  virtual void InitCallArena(Arena* arena) {}

 private:
  // The bottom of the chain under test: stands in for the transport (or for
  // whatever the next interceptor down would be), and records the handler for
  // every call started through it so that GetHandler() can hand them out.
  //
  // This is the same construct other yodel-based fixtures already use to
  // collect started calls -- `ServerCallDestination` in
  // test/core/transport/test_suite/transport_test.h
  class TestCallDestination final : public UnstartedCallDestination {
   public:
    void StartCall(UnstartedCallHandler unstarted_call_handler) override;
    void Orphaned() override {}

    std::optional<CallHandler> PopHandler();

   private:
    std::queue<CallHandler> handlers_;
  };

  // Run `factory` to completion on `half`'s party, ticking the event engine
  // until it resolves, and return what it resolved to.
  template <typename CallHalf, typename PromiseFactory>
  auto BlockingRun(CallHalf half, absl::string_view name,
                   PromiseFactory factory) {
    using Result = typename promise_detail::OncePromiseFactory<
        void, PromiseFactory>::Promise::Result;
    std::shared_ptr<std::optional<Result>> result =
        std::make_shared<std::optional<Result>>();
    half.SpawnInfallible(name,
                         [factory = std::move(factory), result]() mutable {
                           return Map(factory(), [result](Result r) {
                             *result = std::move(r);
                             return Empty{};
                           });
                         });
    auto poll = [result]() -> Poll<Result> {
      if (!result->has_value()) return Pending{};
      return std::move(**result);
    };
    return TickUntil(absl::FunctionRef<Poll<Result>()>(poll));
  }

  ChannelArgs WithTestChannelArgs(const ChannelArgs& args);
  absl::Status FinishInitChannel(InterceptionChainBuilder& builder);

  void Shutdown() override;

  RefCountedPtr<TestCallDestination> destination_ =
      MakeRefCounted<TestCallDestination>();
  RefCountedPtr<UnstartedCallDestination> chain_;
  std::optional<CallInitiator> initiator_;
};

}  // namespace grpc_core

// Expect one of the events corresponding to the methods in
// FilterTestV2::Events.
#define EXPECT_EVENT(event) EXPECT_CALL(events, event)

// Declare one test in a FilterTest-derived suite. Reads like TEST_F(), and
// like all yodel tests each one doubles as a fuzz target.
#define FILTER_TEST(suite, name) YODEL_TEST(suite, name)

#endif  // GRPC_TEST_CORE_FILTERS_FILTER_TEST_H
