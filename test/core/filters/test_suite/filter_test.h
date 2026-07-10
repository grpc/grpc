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

#ifndef GRPC_TEST_CORE_FILTERS_TEST_SUITE_FILTER_TEST_H
#define GRPC_TEST_CORE_FILTERS_TEST_SUITE_FILTER_TEST_H

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <queue>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "src/core/call/call_destination.h"
#include "src/core/call/call_spine.h"
#include "src/core/call/message.h"
#include "src/core/call/metadata.h"
#include "src/core/call/interception_chain.h"
#include "src/core/filter/filter_args.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/call/yodel/yodel_test.h"

// FilterTestV3 is a unit-test harness for the *native v3* filter API.
//
// Unlike the older FilterTest<Filter> (test/core/filters/filter_test.h), which
// drives a single filter through the v2 `MakeCallPromise` bridge on a synthetic
// pipe rig, FilterTestV3 runs the filter(s) under test on the real v3 stack:
//
//   CallInitiator (client)  ->  InterceptionChain (filters under test)  ->
//   TerminalDestination (server, exposes a CallHandler)
//
// The six call lifecycle hooks therefore fire through the genuine CallFilters /
// CallSpine executor, exactly as they do in production.
//
// Because it is built on the yodel harness, every FILTER_TEST_V3 is also a
// fuzz target: the FuzzingEventEngine perturbs scheduling, message sizes, and
// event interleaving across runs.
//
// Typical use:
//
//   FILTER_TEST_V3(MyFilterDoesX) {
//     ASSERT_TRUE(Add<MyFilter>().Build(channel_args).ok());
//     auto initiator = StartCall(NewClientMetadata());
//     // ... drive `initiator` (client) with SpawnTestSeq ...
//     auto handler = TickUntilServerCall();
//     // ... drive `handler` (server) with SpawnTestSeq ...
//     WaitForAllPendingWork();
//   }

namespace grpc_core {

class FilterTestV3 : public YodelTest {
 public:
  using YodelTest::YodelTest;

  // Queue a filter or interceptor to be added to the stack under test. Filters
  // run in add-order (client -> server). Chainable; call before Build().
  // `T` may be either a v3 filter (a class with a nested `Call`) or an
  // `Interceptor` subclass.
  template <typename T>
  FilterTestV3& Add(RefCountedPtr<const FilterConfig> config = nullptr) {
    add_ops_.push_back(
        [config = std::move(config)](InterceptionChainBuilder& b) mutable {
          b.Add<T>(std::move(config));
        });
    return *this;
  }

  // Finalize the stack: build the interception chain (with all Add()ed filters)
  // terminating at the fixture's controllable server destination. Must be
  // called exactly once, before StartCall(). Returns the build status so tests
  // can assert on filter construction failures.
  absl::Status Build(ChannelArgs args = ChannelArgs());

  // Start a call flowing client -> [filters] -> server. Returns the client-side
  // CallInitiator for the test to drive. The matching server-side CallHandler
  // becomes available via TickUntilServerCall().
  CallInitiator StartCall(ClientMetadataHandle client_initial_metadata);

  // Tick the event engine until the call has traversed the filter stack and
  // reached the server end, then return the server-side CallHandler.
  CallHandler TickUntilServerCall();

  // Construct client metadata, optionally seeded with key/value pairs.
  // Static so it can be called from driving lambdas without capturing `this`.
  static ClientMetadataHandle NewClientMetadata(
      std::initializer_list<std::pair<absl::string_view, absl::string_view>>
          init = {});
  // Construct server metadata, optionally seeded with key/value pairs.
  static ServerMetadataHandle NewServerMetadata(
      std::initializer_list<std::pair<absl::string_view, absl::string_view>>
          init = {});
  // Construct a message with the given payload and flags.
  static MessageHandle NewMessage(absl::string_view payload = "",
                                  uint32_t flags = 0);

 private:
  // The controllable server end of the call: each call that traverses the
  // filter stack is started here, and its CallHandler queued for the test to
  // pick up via TickUntilServerCall().
  class TerminalDestination final : public UnstartedCallDestination {
   public:
    void StartCall(UnstartedCallHandler unstarted_call_handler) override {
      handlers_.push(unstarted_call_handler.StartCall());
    }
    void Orphaned() override {}
    std::optional<CallHandler> PopHandler() {
      if (handlers_.empty()) return std::nullopt;
      auto handler = std::move(handlers_.front());
      handlers_.pop();
      return handler;
    }

   private:
    std::queue<CallHandler> handlers_;
  };

  ChannelArgs MakeChannelArgs(ChannelArgs args);

  std::vector<absl::AnyInvocable<void(InterceptionChainBuilder&)>> add_ops_;
  RefCountedPtr<TerminalDestination> server_destination_;
  RefCountedPtr<UnstartedCallDestination> chain_;
};

}  // namespace grpc_core

#define FILTER_TEST_V3(name) YODEL_TEST(FilterTestV3, name)

#endif  // GRPC_TEST_CORE_FILTERS_TEST_SUITE_FILTER_TEST_H
