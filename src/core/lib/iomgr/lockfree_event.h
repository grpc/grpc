/*
 *
 * Copyright 2017 gRPC authors.
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

#ifndef GRPC_CORE_LIB_IOMGR_LOCKFREE_EVENT_H
#define GRPC_CORE_LIB_IOMGR_LOCKFREE_EVENT_H

/* Lock free event notification for file descriptors */

#include <grpc/support/atm.h>

#include "src/core/lib/iomgr/exec_ctx.h"

namespace grpc_core {

class LockfreeEvent {
 public:
  LockfreeEvent();

  LockfreeEvent(const LockfreeEvent&) = delete;
  LockfreeEvent& operator=(const LockfreeEvent&) = delete;

  // These methods are used to initialize and destroy the internal state. These
  // cannot be done in constructor and destructor because SetReady may be called
  // when the event is destroyed and put in a freelist.
  void InitEvent();
  void DestroyEvent();

  bool IsShutdown() const {
    return (gpr_atm_no_barrier_load(&state_) & kShutdownBit) != 0;
  }

  void NotifyOn(grpc_exec_ctx* exec_ctx, grpc_closure* closure);
  bool SetShutdown(grpc_exec_ctx* exec_ctx, grpc_error* error);
  void SetReady(grpc_exec_ctx* exec_ctx);

 private:
  enum State { kClosureNotReady = 0, kClosureReady = 2, kShutdownBit = 1 };

  gpr_atm state_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_IOMGR_LOCKFREE_EVENT_H */
