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

#ifndef BUFEP_H
#define BUFEP_H

#include "absl/cleanup/cleanup.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/util/sync.h"

namespace grpc_core {
namespace chaotic_good {

class ControlEndpoint {
 private:
  class Buffer : public RefCounted<Buffer> {
   public:
    auto Queue(SliceBuffer&& buffer) {
      return [buffer = std::move(buffer), this]() mutable -> Poll<Empty> {
        Waker waker;
        auto cleanup = absl::MakeCleanup([&waker]() { waker.Wakeup(); });
        MutexLock lock(&mu_);
        if (queued_output_.Length() + buffer.Length() > MaxQueued()) {
          write_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
          return Pending{};
        }
        waker = std::move(flush_waker_);
        queued_output_.Append(buffer);
        return Empty{};
      };
    }

    auto Pull();

   private:
    size_t MaxQueued() const { return 1024 * 1024; }

    Mutex mu_;
    Waker write_waker_ ABSL_GUARDED_BY(mu_);
    Waker flush_waker_ ABSL_GUARDED_BY(mu_);
    SliceBuffer queued_output_ ABSL_GUARDED_BY(mu_);
  };

 public:
  explicit ControlEndpoint(PromiseEndpoint endpoint);

  // Write some data to the control endpoint; returns a promise that resolves
  // to Empty{} -- it's not possible to see errors from this api.
  auto Write(SliceBuffer&& bytes) { return buffer_->Queue(std::move(bytes)); }

  auto ReadSlice(size_t length) { return endpoint_->ReadSlice(length); }
  auto Read(size_t length) { return endpoint_->Read(length); }
  auto GetPeerAddress() const { return endpoint_->GetPeerAddress(); }
  auto GetLocalAddress() const { return endpoint_->GetLocalAddress(); }

 private:
  std::shared_ptr<PromiseEndpoint> endpoint_;
  RefCountedPtr<Party> write_party_ =
      Party::Make(SimpleArenaAllocator(0)->MakeArena());
  RefCountedPtr<Buffer> buffer_ = MakeRefCounted<Buffer>();
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif
