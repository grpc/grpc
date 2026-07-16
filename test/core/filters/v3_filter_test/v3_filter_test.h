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

#ifndef GRPC_TEST_CORE_FILTERS_V3_FILTER_TEST_V3_FILTER_TEST_H
#define GRPC_TEST_CORE_FILTERS_V3_FILTER_TEST_V3_FILTER_TEST_H

#include <cstddef>
#include <utility>
#include <vector>

#include "src/core/call/call_filters.h"
#include "src/core/call/call_spine.h"
#include "src/core/call/metadata.h"
#include "src/core/filter/filter_args.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/call/yodel/yodel_test.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

// FilterTestV3 is a unit-test harness for the *native v3* filter API.
//
// Unlike the older FilterTest<Filter> (test/core/filters/filter_test.h), which
// drives a single filter through the v2 `MakeCallPromise` bridge on a synthetic
// pipe rig, FilterTestV3 runs the filter(s) under test on the real v3 call
// machinery:
//
//   CallInitiator (client)  <->  CallFilters::Stack  <->  CallHandler (server)
//
// The stack is assembled with `CallFilters::StackBuilder` and attached directly
// to a real call spine, so the six call lifecycle hooks fire through the genuine
// CallFilters / CallSpine executor, exactly as they do in production.
//
// Because it is built on the yodel harness, every FILTER_TEST_V3 is also a
// fuzz target: the FuzzingEventEngine perturbs scheduling, message sizes, and
// event interleaving across runs.
//
// Typical use:
//
//   FILTER_TEST_V3(MyFilterDoesX) {
//     ASSERT_TRUE(Add<MyFilter>().Build(channel_args).ok());
//     auto [initiator, handler] = StartCall(NewClientMetadata());
//     // ... drive `initiator` (client) and `handler` (server) with
//     // concurrent SpawnTestSeq chains ...
//     WaitForAllPendingWork();
//   }

namespace grpc_core {

class FilterTestV3 : public YodelTest {
 public:
  using YodelTest::YodelTest;

  // Queue a v3 filter (a class with a nested `Call`) to be added to the stack
  // under test. Filters run in add-order (client -> server). Chainable; call
  // before Build(). The filter is constructed at Build() time, so construction
  // failures surface as Build()'s status.
  template <typename T>
  FilterTestV3& Add(RefCountedPtr<const FilterConfig> config = nullptr) {
    add_ops_.push_back([config = std::move(config)](
                           const ChannelArgs& args,
                           CallFilters::StackBuilder& builder,
                           size_t instance_id) mutable -> absl::Status {
      auto filter = T::Create(args, FilterArgs(instance_id, std::move(config)));
      if (!filter.ok()) return filter.status();
      // StackBuilder holds a bare pointer to the filter, so hand it ownership
      // of the ref to keep the filter alive for the lifetime of the stack.
      builder.Add(filter->get());
      builder.AddOwnedObject(std::move(*filter));
      return absl::OkStatus();
    });
    return *this;
  }

  // Finalize the stack: construct every Add()ed filter and build the
  // CallFilters::Stack they form. Must be called exactly once, before
  // StartCall(). Returns the build status so tests can assert on filter
  // construction failures.
  absl::Status Build(ChannelArgs args = ChannelArgs());

  // The two ends of a started call, for the test to drive as concurrent
  // SpawnTestSeq chains. Note that `initiator` and `handler` are structured
  // bindings at the call site, so lambdas must capture them by init-capture
  // (`[initiator = initiator]`) rather than plain capture (`[initiator]`).
  struct StartedCall {
    CallInitiator initiator;
    CallHandler handler;
  };

  // Start a call running through the filter stack, returning both ends. Both
  // exist as soon as the call is started, so no ticking is needed:
  //
  //   auto [initiator, handler] = StartCall(NewClientMetadata());
  StartedCall StartCall(ClientMetadataHandle client_initial_metadata);

 private:
  // Creates one filter and adds it to the stack under construction. Returns the
  // filter's construction status.
  using AddOp = absl::AnyInvocable<absl::Status(
      const ChannelArgs&, CallFilters::StackBuilder&, size_t instance_id)>;

  std::vector<AddOp> add_ops_;
  RefCountedPtr<CallFilters::Stack> stack_;
};

}  // namespace grpc_core

#define FILTER_TEST_V3(name) YODEL_TEST(FilterTestV3, name)

#endif  // GRPC_TEST_CORE_FILTERS_V3_FILTER_TEST_V3_FILTER_TEST_H
