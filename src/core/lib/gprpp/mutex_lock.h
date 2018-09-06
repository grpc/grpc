/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_LIB_GPRPP_MUTEX_LOCK_H
#define GRPC_CORE_LIB_GPRPP_MUTEX_LOCK_H

#include <grpc/support/port_platform.h>

#include <grpc/support/sync.h>

namespace grpc_core {

class MutexLock {
 public:
  explicit MutexLock(gpr_mu* mu) : mu_(mu) { gpr_mu_lock(mu); }
  ~MutexLock() { gpr_mu_unlock(mu_); }

  MutexLock(const MutexLock&) = delete;
  MutexLock& operator=(const MutexLock&) = delete;

 private:
  gpr_mu* const mu_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_GPRPP_MUTEX_LOCK_H */
