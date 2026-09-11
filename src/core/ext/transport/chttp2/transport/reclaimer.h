//
//
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
//
//

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_RECLAIMER_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_RECLAIMER_H

#include <memory>
#include <optional>
#include <utility>

#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/status/status.h"

namespace grpc_core {
namespace http2 {

#define GRPC_HTTP2_RECLAIMER_LOG VLOG(2)

// Each PH2 transport provides its own implementation of this interface.
// Each method maps to the actions that a memory reclamation pass performs on a
// PH2 transport.
class ReclaimerInterface {
 public:
  virtual ~ReclaimerInterface() = default;

  // Benign pass. Closes the transport if it has no active streams.
  // Returns true if the transport close was initiated.
  virtual bool CloseTransportIfIdle() = 0;

  // Destructive pass. Cancels exactly one active stream, if any exists.
  // Returns true if at least one stream is still active after the
  // cancellation. Returns false otherwise.
  virtual bool CancelOneActiveStream() = 0;
};

// Owns all resource quota reclamation state for one PH2 transport.
//
// Why this is a separate ref counted object, and not a part of the transport:
// A reclaimer that is posted to the memory quota keeps the posted functor (and
// everything that it captured) alive until the quota either runs it or is
// destroyed. If the functor captured a transport ref, we would form a cycle
// Transport -> MemoryOwner -> ReclaimerQueue -> functor -> Transport, and the
// transport would never be destroyed. The functor therefore captures a ref to
// this small manager instead, and the manager only ever holds a raw pointer
// back to the transport.
//
// Lifetime contract: the transport MUST call Close() before it is destroyed.
// Close() drops the raw MemoryOwner pointer and the ReclaimerInterface, so a
// reclaimer callback that fires after the transport is gone becomes a no-op.
//
// Threading: OnReclaimerTriggered() may run on any thread. It only enqueues
// work under mu_ and wakes the ReclamationLoop. All transport mutation happens
// inside the ReclamationLoop, on the transport party.
class ReclamationManager final : public RefCounted<ReclamationManager> {
 public:
  // memory_owner MUST outlive this object, minus the Close() contract above.
  // In practice it is a member of the transport that owns this manager.
  ReclamationManager(MemoryOwner* memory_owner,
                     std::unique_ptr<ReclaimerInterface> reclaimer_interface)
      : memory_owner_(memory_owner),
        reclaimer_interface_(std::move(reclaimer_interface)) {
    GRPC_DCHECK(memory_owner_ != nullptr);
    GRPC_DCHECK(reclaimer_interface_ != nullptr);
  }

  ReclamationManager() = delete;
  ReclamationManager(const ReclamationManager&) = delete;
  ReclamationManager& operator=(const ReclamationManager&) = delete;
  ReclamationManager(ReclamationManager&&) = delete;
  ReclamationManager& operator=(ReclamationManager&&) = delete;

  // Registers a benign reclaimer with the memory quota, unless one is already
  // registered or this manager is closed.
  // Based on CHTTP2's post_benign_reclaimer in chttp2_transport.cc
  void MaybePostBenignReclaimer() {
    MaybePostReclaimer(ReclamationPass::kBenign);
  }

  // Registers a destructive reclaimer with the memory quota, unless one is
  // already registered or this manager is closed.
  // Based on CHTTP2's post_destructive_reclaimer in chttp2_transport.cc
  void MaybePostDestructiveReclaimer() {
    MaybePostReclaimer(ReclamationPass::kDestructive);
  }

  // Returns a promise that drains reclamation work on the transport party.
  // This MUST be spawned at most once, and MUST be spawned on the transport
  // party. It uses exactly one party slot for the lifetime of the transport.
  // The promise resolves once Close() has been called.
  // The returned promise holds a raw pointer to this manager. That is safe
  // because the transport owns both the manager and the party, and the party
  // promise holds a ref to the transport.
  auto ReclamationLoop() {
    return Loop([this]() {
      return [this]() -> Poll<LoopCtl<absl::Status>> {
        std::optional<ReclamationTask> task;
        ReclaimerInterface* reclaimer_interface = nullptr;
        {
          MutexLock lock(&mu_);
          if (is_closed_) {
            return absl::OkStatus();
          }
          if (!pending_task_.has_value()) {
            waker_ = GetContext<Activity>()->MakeNonOwningWaker();
            return Pending{};
          }
          // Moving a std::optional leaves it engaged, so reset() is needed.
          task = std::move(pending_task_);
          pending_task_.reset();
          reclaimer_interface = reclaimer_interface_.get();
        }
        // The task is processed without holding mu_. This keeps the lock
        // ordering simple and prevents deadlocks with the transport mutex.
        ProcessReclamationTask(*task, reclaimer_interface);
        return Continue{};
      };
    });
  }

  // Permanently stops all reclamation activity. This is idempotent.
  // The transport MUST call this from CloseTransport() and from its
  // destructor. After this returns, no ReclaimerInterface method will ever be
  // called again, and the raw MemoryOwner pointer is never dereferenced again.
  void Close();

 private:
  struct ReclamationTask {
    ReclamationPass pass;
    ReclamationSweep sweep;
  };

  void MaybePostReclaimer(ReclamationPass pass);

  // Invoked by the memory quota, on an arbitrary thread. A nullopt sweep means
  // that the reclaimer was cancelled rather than triggered.
  void OnReclaimerTriggered(ReclamationPass pass,
                            std::optional<ReclamationSweep> sweep);

  // Runs on the transport party, without holding mu_.
  void ProcessReclamationTask(ReclamationTask& task,
                              ReclaimerInterface* reclaimer_interface);

  // Dedicated lock for all reclaimer state. This lock is intentionally NOT the
  // transport mutex. Reclaimer callbacks run on arbitrary threads, so a
  // separate lock keeps them off the hot transport path and avoids any lock
  // ordering issue with the transport mutex.
  // Rule: never acquire the transport mutex while holding mu_.
  Mutex mu_;
  MemoryOwner* memory_owner_ ABSL_GUARDED_BY(mu_);
  std::unique_ptr<ReclaimerInterface> reclaimer_interface_ ABSL_GUARDED_BY(mu_);
  // At most one task can ever be pending. BasicMemoryQuota runs a single
  // reclamation loop and blocks on the current ReclamationSweep until it is
  // finished. It therefore never triggers another reclaimer for this manager
  // while a task is still queued here.
  std::optional<ReclamationTask> pending_task_ ABSL_GUARDED_BY(mu_);
  Waker waker_ ABSL_GUARDED_BY(mu_);
  bool is_benign_reclaimer_registered_ ABSL_GUARDED_BY(mu_) = false;
  bool is_destructive_reclaimer_registered_ ABSL_GUARDED_BY(mu_) = false;
  bool is_closed_ ABSL_GUARDED_BY(mu_) = false;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_RECLAIMER_H