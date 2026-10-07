// Copyright 2024 gRPC authors.
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

#include "src/core/client_channel/lame_channel.h"

#include "src/core/call/client_call.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/surface/lame_client.h"

namespace grpc_core {

absl::StatusOr<RefCountedPtr<LameChannel>> LameChannel::Create(
    std::string target, ChannelArgs args) {
  absl::Status* status =
      args.GetPointer<absl::Status>(GRPC_ARG_LAME_FILTER_ERROR);
  if (status == nullptr) {
    return absl::InvalidArgumentError("Lame status not in ChannelArgs");
  }
  auto event_engine =
      args.GetObjectRef<grpc_event_engine::experimental::EventEngine>();
  if (event_engine == nullptr) {
    return absl::InvalidArgumentError("EventEngine not set in ChannelArgs");
  }
  return MakeRefCounted<LameChannel>(
      std::move(target), args, std::move(event_engine), std::move(*status));
}

void LameChannel::StartCall(UnstartedCallHandler unstarted_handler) {
  auto handler = unstarted_handler.StartCall();
  handler.PushServerTrailingMetadata(ServerMetadataFromStatus(status_));
}

grpc_call* LameChannel::CreateCall(
    grpc_call* parent_call, uint32_t propagation_mask,
    grpc_completion_queue* cq, grpc_pollset_set* /*pollset_set_alternative*/,
    Slice path, std::optional<Slice> authority, Timestamp deadline,
    bool /*registered_method*/,
    std::optional<absl::FunctionRef<void(Arena*)>> arena_init_function) {
  auto arena = call_arena_allocator()->MakeArena();
  arena->SetContext<grpc_event_engine::experimental::EventEngine>(
      event_engine_.get());
  if (arena_init_function.has_value()) {
    (*arena_init_function)(arena.get());
  }
  return MakeClientCall(parent_call, propagation_mask, cq, std::move(path),
                        std::move(authority), false, deadline,
                        compression_options(), std::move(arena), Ref());
}

}  // namespace grpc_core
