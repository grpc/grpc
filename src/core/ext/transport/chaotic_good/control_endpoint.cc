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

#include "src/core/ext/transport/chaotic_good/control_endpoint.h"

#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/try_seq.h"

namespace grpc_core {
namespace chaotic_good {

auto ControlEndpoint::Buffer::Pull() {
  return [this]() -> Poll<SliceBuffer> {
    Waker waker;
    auto cleanup = absl::MakeCleanup([&waker]() { waker.Wakeup(); });
    MutexLock lock(&mu_);
    if (queued_output_.Length() == 0) {
      flush_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
      return Pending{};
    }
    waker = std::move(write_waker_);
    return std::move(queued_output_);
  };
}

ControlEndpoint::ControlEndpoint(PromiseEndpoint endpoint)
    : endpoint_(std::make_shared<PromiseEndpoint>(std::move(endpoint))) {
  write_party_->Spawn(
      "flush",
      GRPC_LATENT_SEE_PROMISE(
          "FlushLoop", Loop([endpoint = endpoint_, buffer = buffer_]() {
            return TrySeq(
                buffer->Pull(),
                [endpoint](SliceBuffer flushing) {
                  return endpoint->Write(std::move(flushing));
                },
                []() -> LoopCtl<absl::Status> { return Continue{}; });
          })),
      [](absl::Status) {});
}

}  // namespace chaotic_good
}  // namespace grpc_core
