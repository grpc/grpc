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

#ifndef GRPCPP_IMPL_SYNC_H
#define GRPCPP_IMPL_SYNC_H

#include <grpc/support/port_platform.h>

#ifdef GPR_HAS_PTHREAD_H
#include <pthread.h>
#endif

#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include <mutex>

#include "absl/log/absl_check.h"
#include "absl/synchronization/mutex.h"

// The core library is not accessible in C++ codegen headers, and vice versa.
// Thus, we need to have duplicate headers with similar functionality.
// Make sure any change to this file is also reflected in
// src/core/util/sync.h too.
//
// Whenever possible, prefer "src/core/util/sync.h" over this file,
// since in core we do not rely on g_core_codegen_interface and hence do not
// pay the costs of virtual function calls.

namespace grpc {
namespace internal {

using Mutex = absl::Mutex;
using MutexLock = absl::MutexLock;
using ReleasableMutexLock = absl::ReleasableMutexLock;
using CondVar = absl::CondVar;

template <typename Predicate>
GRPC_DEPRECATED("incompatible with thread safety analysis")
static void WaitUntil(CondVar* cv, Mutex* mu, Predicate pred) {
  while (!pred()) {
    cv->Wait(mu);
  }
}

}  // namespace internal
}  // namespace grpc

#endif  // GRPCPP_IMPL_SYNC_H
