//
//
// Copyright 2019 gRPC authors.
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

#ifndef GRPC_SRC_CORE_UTIL_SYNC_H
#define GRPC_SRC_CORE_UTIL_SYNC_H

#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>

#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "src/core/util/time_util.h"

// The core library is not accessible in C++ codegen headers, and vice versa.
// Thus, we need to have duplicate headers with similar functionality.
// Make sure any change to this file is also reflected in
// include/grpcpp/impl/sync.h.
//
// Whenever possible, prefer using this file over <grpcpp/impl/sync.h>
// since this file doesn't rely on g_core_codegen_interface and hence does not
// pay the costs of virtual function calls.

namespace grpc_core {

using Mutex = absl::Mutex;
using MutexLock = absl::MutexLock;
using ReleasableMutexLock = absl::ReleasableMutexLock;
using CondVar = absl::CondVar;

// Returns the underlying gpr_mu from Mutex. This should be used only when
// it has to like passing the C++ mutex to C-core API.
// TODO(veblush): Remove this after C-core no longer uses gpr_mu.
inline gpr_mu* GetUnderlyingGprMu(Mutex* mutex) {
  return reinterpret_cast<gpr_mu*>(mutex);
}

// Deprecated. Prefer MutexLock
class MutexLockForGprMu {
 public:
  explicit MutexLockForGprMu(gpr_mu* mu) : mu_(mu) { gpr_mu_lock(mu_); }
  ~MutexLockForGprMu() { gpr_mu_unlock(mu_); }

  MutexLockForGprMu(const MutexLock&) = delete;
  MutexLockForGprMu& operator=(const MutexLock&) = delete;

 private:
  gpr_mu* const mu_;
};

// Deprecated. Prefer MutexLock or ReleasableMutexLock
class ABSL_SCOPED_LOCKABLE LockableAndReleasableMutexLock {
 public:
  explicit LockableAndReleasableMutexLock(Mutex* mu)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : mu_(mu) {
    mu_->Lock();
  }
  ~LockableAndReleasableMutexLock() ABSL_UNLOCK_FUNCTION() {
    if (!released_) mu_->Unlock();
  }

  LockableAndReleasableMutexLock(const LockableAndReleasableMutexLock&) =
      delete;
  LockableAndReleasableMutexLock& operator=(
      const LockableAndReleasableMutexLock&) = delete;

  void Lock() ABSL_EXCLUSIVE_LOCK_FUNCTION() {
    DCHECK(released_);
    mu_->Lock();
    released_ = false;
  }

  void Release() ABSL_UNLOCK_FUNCTION() {
    DCHECK(!released_);
    released_ = true;
    mu_->Unlock();
  }

 private:
  Mutex* const mu_;
  bool released_ = false;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_UTIL_SYNC_H
