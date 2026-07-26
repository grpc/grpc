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

#include "src/core/call/call_destination.h"
#include "src/core/call/call_spine.h"
#include "src/core/call/interception_chain.h"
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

namespace grpc_core {

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
//     absl::Status Init() { return CreateFilterChain<MyFilter>(MyArgs()); }
//   };
//
//   FILTER_TEST(MyFilterTest, UnaryRpc) {
//     ASSERT_TRUE(Init().ok());
//     StartCallForFilter(NewClientMetadata());
//     PushClientMessage(NewMessage("hello"));
//     PushClientHalfClose();
//     EXPECT_THAT(PullClientMessage().value(), HasMessagePayload("hello"));
//     PushServerTrailingMetadata(ServerMetadataFromStatus(...));
//     EXPECT_THAT(**PullServerTrailingMetadata(), HasMetadataResult(...));
//     WaitForAllPendingWork();
//   }
//
// Interceptors may create more than one child call: use StartCall() plus
// GetNextHandler() and pass each handler to the explicit-handler overloads.
class FilterTest : public YodelTest {
 protected:
  using YodelTest::YodelTest;

  // Building the stack under test

  // Build a stack containing exactly the one filter or interceptor under test.
  // Returns the build status so tests can assert on construction failures.
  // Must be called exactly once, before starting a call.
  template <typename Filter>
  absl::Status CreateFilterChain(
      const ChannelArgs& args = ChannelArgs(),
      RefCountedPtr<const FilterConfig> config = nullptr) {
    InterceptionChainBuilder builder(WithTestChannelArgs(args));
    builder.Add<Filter>(std::move(config));
    return FinishInitChannel(builder);
  }

  // Starting calls

  // Start a call through the stack under test and return the client end of it.
  // Only the initiator is returned: an interceptor may start any number of
  // child calls (or none), so handlers are collected separately by
  // GetNextHandler(). Also becomes the implicit initiator.
  CallInitiator StartCall(ClientMetadataHandle client_initial_metadata);

  // Return the handler for the next call started against the bottom of the
  // stack, ticking the event engine until one appears. Call once per child call
  // the filter or interceptor is expected to create. Also becomes the implicit
  // handler.
  CallHandler GetNextHandler();

  // Convenience for the filter case, where a call always creates exactly one
  // child call: StartCall() followed by one GetNextHandler(). May be called
  // only once per test.
  void StartCallForFilter(ClientMetadataHandle client_initial_metadata);

  // Number of child calls started against the bottom of the stack, whether or
  // not they were collected via GetNextHandler().
  int ChildCallsStarted() const;

  // Driving the six call operations.
  //
  // Push*() is asynchronous and serializes operations onto the call's party in
  // FIFO order.
  //
  // Pull*() is synchronous and ticks the event engine until the value arrives.
  //
  // Client-side operations act on the implicit initiator. Handler-side
  // operations either take a handler explicitly (needed when there is more than
  // one child call) or act on the implicit handler.

  // client -> server
  void PushClientMessage(MessageHandle message);
  void PushClientHalfClose();
  ValueOrFailure<ClientMetadataHandle> PullClientInitialMetadata(
      CallHandler handler);
  ValueOrFailure<ClientMetadataHandle> PullClientInitialMetadata();
  ClientToServerNextMessage PullClientMessage(CallHandler handler);
  ClientToServerNextMessage PullClientMessage();
  // True iff the client->server stream ended cleanly (the client half-closed).
  // Otherwise records a test failure saying whether the call failed or a
  // message arrived instead, and returns false.
  bool PullClientHalfClose(CallHandler handler);
  bool PullClientHalfClose();

  // server -> client
  void PushServerInitialMetadata(CallHandler handler, ServerMetadataHandle md);
  void PushServerInitialMetadata(ServerMetadataHandle md);
  void PushServerMessage(CallHandler handler, MessageHandle message);
  void PushServerMessage(MessageHandle message);
  void PushServerTrailingMetadata(CallHandler handler, ServerMetadataHandle md);
  void PushServerTrailingMetadata(ServerMetadataHandle md);
  // nullopt for the trailers-only case (no separate initial metadata).
  ValueOrFailure<std::optional<ServerMetadataHandle>>
  PullServerInitialMetadata();
  ServerToClientNextMessage PullServerMessage();
  ValueOrFailure<ServerMetadataHandle> PullServerTrailingMetadata();
  // As above, but returns the call's status, for tests that only care about it.
  absl::Status PullServerTrailingStatus();

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
  virtual void InitAfterCallArena(Arena* arena) {}

 private:
  // The implicit initiator/handler used by the no-argument operation helpers.
  CallInitiator initiator();
  CallHandler handler();

  // The bottom of the chain under test: stands in for the transport (or for
  // whatever the next interceptor down would be), and records the handler for
  // every call started through it so that GetNextHandler() can hand them out.
  //
  // This is the same construct other yodel-based fixtures already use to
  // collect started calls -- `ServerCallDestination` in
  // test/core/transport/test_suite/transport_test.h
  class TestCallDestination final : public UnstartedCallDestination {
   public:
    void StartCall(UnstartedCallHandler unstarted_call_handler) override;
    void Orphaned() override {}

    std::optional<CallHandler> PopHandler();

    // Total number of calls ever started against this destination.
    int calls_started() const { return calls_started_; }

   private:
    std::queue<CallHandler> handlers_;
    int calls_started_ = 0;
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
  std::optional<CallHandler> handler_;
};

}  // namespace grpc_core

// Declare one test in a FilterTest-derived suite. Reads like TEST_F(), and
// like all yodel tests each one doubles as a fuzz target.
#define FILTER_TEST(suite, name) YODEL_TEST(suite, name)

#endif  // GRPC_TEST_CORE_FILTERS_FILTER_TEST_H
