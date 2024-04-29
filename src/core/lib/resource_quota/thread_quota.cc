// Copyright 2021 gRPC authors.
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

#include "src/core/lib/resource_quota/thread_quota.h"

#include "absl/log/check.h"

#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

namespace grpc_core {

ThreadQuota::ThreadQuota() = default;

ThreadQuota::~ThreadQuota() = default;

void ThreadQuota::SetMax(size_t new_max) {
  MutexLock lock(&mu_);
  max_ = new_max;
}

bool ThreadQuota::Reserve(size_t num_threads) {
  MutexLock lock(&mu_);
  if (allocated_ + num_threads > max_) return false;
  allocated_ += num_threads;
  return true;
}

void ThreadQuota::Release(size_t num_threads) {
  MutexLock lock(&mu_);
  CHECK(num_threads <= allocated_);
  allocated_ -= num_threads;
}

}  // namespace grpc_core
