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

#include "test/core/filters/v3_filter_test/v3_filter_test.h"

#include <grpc/event_engine/event_engine.h>

#include <cstddef>
#include <utility>

#include "src/core/util/ref_counted_ptr.h"
#include "absl/log/check.h"

namespace grpc_core {

///////////////////////////////////////////////////////////////////////////////
// FilterTestV3

absl::Status FilterTestV3::Build(ChannelArgs args) {
  CHECK(stack_ == nullptr) << "Build() must be called exactly once";
  args = args.SetObject<grpc_event_engine::experimental::EventEngine>(
      event_engine());
  CallFilters::StackBuilder builder;
  size_t instance_id = 0;
  for (auto& op : add_ops_) {
    auto status = op(args, builder, instance_id++);
    if (!status.ok()) return status;
  }
  stack_ = builder.Build();
  return absl::OkStatus();
}

FilterTestV3::StartedCall FilterTestV3::StartCall(
    ClientMetadataHandle client_initial_metadata) {
  CHECK(stack_ != nullptr) << "Build() must be called before StartCall()";
  auto call = MakeCall(std::move(client_initial_metadata));
  call.handler.AddCallStack(stack_);
  auto handler = call.handler.StartCall();
  return StartedCall{std::move(call.initiator), std::move(handler)};
}

}  // namespace grpc_core
