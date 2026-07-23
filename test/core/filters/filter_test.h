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
#include <utility>

#include "src/core/call/call_spine.h"
#include "src/core/call/message.h"
#include "src/core/call/metadata.h"
#include "src/core/filter/filter_args.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/call/yodel/yodel_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

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
