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

#include "src/core/ext/transport/chttp2/transport/reclaimer.h"

#include <memory>
#include <optional>
#include <utility>

#include "src/core/lib/promise/activity.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/sync.h"

namespace grpc_core {
namespace http2 {

void ReclamationManager::MaybePostReclaimer(const ReclamationPass pass) {
  GRPC_DCHECK(pass == ReclamationPass::kBenign ||
              pass == ReclamationPass::kDestructive);
  MemoryOwner* memory_owner = nullptr;
  {
    MutexLock lock(&mu_);
    if (is_closed_) {
      return;
    }
    if (pass == ReclamationPass::kBenign) {
      if (is_benign_reclaimer_registered_) {
        return;
      }
      is_benign_reclaimer_registered_ = true;
    } else {
      if (is_destructive_reclaimer_registered_) {
        return;
      }
      is_destructive_reclaimer_registered_ = true;
    }
    memory_owner = memory_owner_;
  }
  GRPC_DCHECK(memory_owner != nullptr);

  // PostReclaimer MUST be called without holding mu_. Registering a reclaimer
  // orphans any handle that is already registered for the same pass, and that
  // orphaning synchronously re-enters OnReclaimerTriggered.
  //
  // The functor captures a ref to this manager, and NOT a ref to the
  // transport. Capturing a transport ref would create a cycle through the
  // MemoryOwner and leak the transport. See the class comment in reclaimer.h.
  memory_owner->PostReclaimer(
      pass, [self = Ref(), pass](std::optional<ReclamationSweep> sweep) {
        self->OnReclaimerTriggered(pass, std::move(sweep));
      });
}

void ReclamationManager::OnReclaimerTriggered(
    const ReclamationPass pass, std::optional<ReclamationSweep> sweep) {
  Waker waker;
  {
    MutexLock lock(&mu_);
    if (pass == ReclamationPass::kBenign) {
      is_benign_reclaimer_registered_ = false;
    } else {
      is_destructive_reclaimer_registered_ = false;
    }
    // A nullopt sweep means the reclaimer was cancelled, not triggered. This
    // happens when the memory quota shuts down. There is nothing to reclaim.
    if (GPR_UNLIKELY(is_closed_ || !sweep.has_value())) {
      return;
    }
    // The memory quota never has two sweeps outstanding at once, so a task can
    // never overwrite an unprocessed one.
    GRPC_DCHECK(!pending_task_.has_value());
    pending_task_ = ReclamationTask{pass, std::move(*sweep)};
    // Waker move assignment swaps, so this leaves waker_ unwakeable.
    waker = std::move(waker_);
    GRPC_DCHECK(waker_.is_unwakeable());
  }
  // Wakeup MUST happen after releasing the lock.
  waker.Wakeup();
}

// Based on CHTTP2's benign_reclaimer_locked and destructive_reclaimer_locked in
// chttp2_transport.cc
void ReclamationManager::ProcessReclamationTask(
    ReclamationTask& task, ReclaimerInterface* reclaimer_interface) {
  GRPC_DCHECK(reclaimer_interface != nullptr);
  switch (task.pass) {
    case ReclamationPass::kBenign: {
      GRPC_HTTP2_RECLAIMER_LOG << "ReclamationManager: benign pass";
      reclaimer_interface->CloseTransportIfIdle();
      break;
    }
    case ReclamationPass::kDestructive: {
      GRPC_HTTP2_RECLAIMER_LOG << "ReclamationManager: destructive pass";
      const bool has_remaining_streams =
          reclaimer_interface->CancelOneActiveStream();
      // Only one stream is reclaimed per sweep. Re-post so that the quota can
      // keep reclaiming if memory pressure persists.
      if (has_remaining_streams) {
        MaybePostDestructiveReclaimer();
      }
      break;
    }
    default:
      break;
  }
  // Every branch MUST finish the sweep, otherwise the quota's reclamation loop
  // stalls forever.
  task.sweep.Finish();
}

void ReclamationManager::Close() {
  std::unique_ptr<ReclaimerInterface> reclaimer_interface;
  std::optional<ReclamationTask> pending_task;
  Waker waker;
  {
    MutexLock lock(&mu_);
    if (is_closed_) {
      return;
    }
    is_closed_ = true;
    memory_owner_ = nullptr;
    reclaimer_interface = std::move(reclaimer_interface_);
    // Moving a std::optional leaves it engaged, so reset() is needed.
    pending_task = std::move(pending_task_);
    pending_task_.reset();
    // Waker move assignment swaps, so this leaves waker_ unwakeable.
    waker = std::move(waker_);
    GRPC_DCHECK(waker_.is_unwakeable());
  }
  // Destroy these outside the lock. ~ReclamationSweep calls back into the
  // memory quota, and the interface destructor may touch the transport.
  pending_task.reset();
  reclaimer_interface.reset();
  // Let the ReclamationLoop observe is_closed_ and resolve.
  waker.Wakeup();
}

}  // namespace http2
}  // namespace grpc_core
