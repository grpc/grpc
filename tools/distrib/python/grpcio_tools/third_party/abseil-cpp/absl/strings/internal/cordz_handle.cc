// Copyright 2019 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "absl/strings/internal/cordz_handle.h"

#include <atomic>

#include "absl/base/internal/raw_logging.h"  // For ABSL_RAW_CHECK
#include "absl/base/internal/spinlock.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

using ::absl::base_internal::SpinLockHolder;

ABSL_CONST_INIT CordzHandle::Queue CordzHandle::global_queue_(absl::kConstInit);

CordzHandle::CordzHandle(bool is_snapshot) : is_snapshot_(is_snapshot) {
  if (is_snapshot) {
    SpinLockHolder lock(&queue_->mutex);
    CordzHandle* dq_tail = queue_->dq_tail.load(std::memory_order_acquire);
    if (dq_tail != nullptr) {
      dq_prev_ = dq_tail;
      dq_tail->dq_next_ = this;
    }
    queue_->dq_tail.store(this, std::memory_order_release);
  }
}

CordzHandle::~CordzHandle() {
  ODRCheck();
  if (is_snapshot_) {
    std::vector<CordzHandle*> to_delete;
    {
      SpinLockHolder lock(&queue_->mutex);
      CordzHandle* next = dq_next_;
      if (dq_prev_ == nullptr) {
        // We were head of the queue, delete every CordzHandle until we reach
        // either the end of the list, or a snapshot handle.
        while (next && !next->is_snapshot_) {
          to_delete.push_back(next);
          next = next->dq_next_;
        }
      } else {
        // Another CordzHandle existed before this one, don't delete anything.
        dq_prev_->dq_next_ = next;
      }
      if (next) {
        next->dq_prev_ = dq_prev_;
      } else {
        queue_->dq_tail.store(dq_prev_, std::memory_order_release);
      }
    }
    for (CordzHandle* handle : to_delete) {
      delete handle;
    }
  }
}

bool CordzHandle::SafeToDelete() const {
  return is_snapshot_ || queue_->IsEmpty();
}

void CordzHandle::Delete(CordzHandle* handle) {
  assert(handle);
  if (handle) {
    handle->ODRCheck();
    Queue* const queue = handle->queue_;
    if (!handle->SafeToDelete()) {
      SpinLockHolder lock(&queue->mutex);
      CordzHandle* dq_tail = queue->dq_tail.load(std::memory_order_acquire);
      if (dq_tail != nullptr) {
        handle->dq_prev_ = dq_tail;
        dq_tail->dq_next_ = handle;
        queue->dq_tail.store(handle, std::memory_order_release);
        return;
      }
    }
    delete handle;
  }
}

std::vector<const CordzHandle*> CordzHandle::DiagnosticsGetDeleteQueue() {
  std::vector<const CordzHandle*> handles;
  SpinLockHolder lock(&global_queue_.mutex);
  CordzHandle* dq_tail = global_queue_.dq_tail.load(std::memory_order_acquire);
  for (const CordzHandle* p = dq_tail; p; p = p->dq_prev_) {
    handles.push_back(p);
  }
  return handles;
}

bool CordzHandle::DiagnosticsHandleIsSafeToInspect(
    const CordzHandle* handle) const {
  ODRCheck();
  if (!is_snapshot_) return false;
  if (handle == nullptr) return true;
  if (handle->is_snapshot_) return false;
  bool snapshot_found = false;
  SpinLockHolder lock(&queue_->mutex);
  for (const CordzHandle* p = queue_->dq_tail; p; p = p->dq_prev_) {
    if (p == handle) return !snapshot_found;
    if (p == this) snapshot_found = true;
  }
  ABSL_ASSERT(snapshot_found);  // Assert that 'this' is in delete queue.
  return true;
}

std::vector<const CordzHandle*>
CordzHandle::DiagnosticsGetSafeToInspectDeletedHandles() {
  ODRCheck();
  std::vector<const CordzHandle*> handles;
  if (!is_snapshot()) {
    return handles;
  }

  SpinLockHolder lock(&queue_->mutex);
  for (const CordzHandle* p = dq_next_; p != nullptr; p = p->dq_next_) {
    if (!p->is_snapshot()) {
      handles.push_back(p);
    }
  }
  return handles;
}

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
