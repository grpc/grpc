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

#include "test/core/filters/test_suite/filter_test.h"

#include <optional>
#include <string>
#include <utility>

#include <grpc/event_engine/event_engine.h>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/crash.h"
#include "src/core/util/ref_counted_ptr.h"

namespace grpc_core {

namespace {

// Append key/value pairs onto a metadata batch, crashing (loudly, in-test) on a
// malformed value rather than silently dropping it.
void FillMetadata(
    std::initializer_list<std::pair<absl::string_view, absl::string_view>> init,
    grpc_metadata_batch& out) {
  for (const auto& p : init) {
    out.Append(p.first, Slice::FromCopiedString(p.second),
               [&](absl::string_view error, const Slice& value) {
                 Crash(absl::StrCat("Illegal metadata: ", p.first, "=",
                                    value.as_string_view(), " (", error, ")"));
               });
  }
}

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// FilterTestV3

ChannelArgs FilterTestV3::MakeChannelArgs(ChannelArgs args) {
  return args.SetObject<grpc_event_engine::experimental::EventEngine>(
      event_engine());
}

absl::Status FilterTestV3::Build(ChannelArgs args) {
  server_destination_ = MakeRefCounted<TerminalDestination>();
  InterceptionChainBuilder builder(MakeChannelArgs(std::move(args)));
  for (auto& op : add_ops_) op(builder);
  auto chain = builder.Build(
      RefCountedPtr<UnstartedCallDestination>(server_destination_));
  if (!chain.ok()) return chain.status();
  chain_ = std::move(*chain);
  return absl::OkStatus();
}

CallInitiator FilterTestV3::StartCall(
    ClientMetadataHandle client_initial_metadata) {
  CHECK(chain_ != nullptr) << "Build() must be called before StartCall()";
  auto call = MakeCall(std::move(client_initial_metadata));
  call.initiator.SpawnInfallible(
      "start-call",
      [chain = chain_, handler = std::move(call.handler)]() mutable {
        chain->StartCall(std::move(handler));
      });
  return std::move(call.initiator);
}

CallHandler FilterTestV3::TickUntilServerCall() {
  auto poll = [this]() -> Poll<CallHandler> {
    auto handler = server_destination_->PopHandler();
    if (handler.has_value()) return std::move(*handler);
    return Pending();
  };
  return TickUntil(absl::FunctionRef<Poll<CallHandler>()>(poll));
}

ClientMetadataHandle FilterTestV3::NewClientMetadata(
    std::initializer_list<std::pair<absl::string_view, absl::string_view>>
        init) {
  auto md = Arena::MakePooledForOverwrite<ClientMetadata>();
  FillMetadata(init, *md);
  return md;
}

ServerMetadataHandle FilterTestV3::NewServerMetadata(
    std::initializer_list<std::pair<absl::string_view, absl::string_view>>
        init) {
  auto md = Arena::MakePooledForOverwrite<ServerMetadata>();
  FillMetadata(init, *md);
  return md;
}

MessageHandle FilterTestV3::NewMessage(absl::string_view payload,
                                       uint32_t flags) {
  SliceBuffer buffer;
  if (!payload.empty()) buffer.Append(Slice::FromCopiedString(payload));
  return Arena::MakePooled<Message>(std::move(buffer), flags);
}

}  // namespace grpc_core
