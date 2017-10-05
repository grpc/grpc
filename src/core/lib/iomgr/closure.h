/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_LIB_IOMGR_CLOSURE_H
#define GRPC_CORE_LIB_IOMGR_CLOSURE_H

#include <grpc/support/port_platform.h>

#include <grpc/impl/codegen/exec_ctx_fwd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <atomic>
#include <utility>
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/support/memory.h"
#include "src/core/lib/support/mpscq.h"

namespace grpc_core {

class Closure;

class ClosureScheduler {
 public:
  virtual void Schedule(grpc_exec_ctx *exec_ctx, Closure *closure,
                        grpc_error *error) = 0;
  virtual void Run(grpc_exec_ctx *exec_ctx, Closure *closure,
                   grpc_error *error) = 0;
};

class Closure {
 public:
  virtual ~Closure() {}
  void Schedule(grpc_exec_ctx *exec_ctx, grpc_error *error) {
    scheduler_->Schedule(exec_ctx, this, error);
  }
  void Run(grpc_exec_ctx *exec_ctx, grpc_error *error) {
    scheduler_->Schedule(exec_ctx, this, error);
  }

 private:
  friend class ClosureScheduler;
  virtual void Execute(grpc_exec_ctx *exec_ctx, grpc_error *error) = 0;

  union {
    struct {
      union {
        Closure *next;
        std::atomic<Closure *> atm_next;
      };
      grpc_error *error;
    } queued_;
    struct {
      uintptr_t scratch[2];
    } unqueued_;
  };

  ClosureScheduler *scheduler_;

 public:
  enum { kScratchSpaceSize = sizeof(unqueued_.scratch) };

  void *unqueued_closure_scratch_space() { return unqueued_.scratch; }
};

// a closure over a member function
template <class T,
          void (T::*Callback)(grpc_exec_ctx *exec_ctx, grpc_error *error)>
class MemberClosure final : public Closure {
 public:
  MemberClosure(T *p) : p_(p) {}

 private:
  void Execute(grpc_exec_ctx *exec_ctx, grpc_error *error) {
    (p_->*Callback)(exec_ctx, error);
  }

  T *p_;
};

// create a closure given some lambda
template <class F>
UniquePtr<Closure> MakeClosure(F f) {
  class Impl final : public Closure {
   public:
    Impl(F f) : f_(std::move(f)) {}

   private:
    void Execute(grpc_exec_ctx *exec_ctx, grpc_error *error) {
      f_(exec_ctx, error);
    }

    F f_;
  };
  return MakeUnique<Impl>(std::move(f));
}

// create a closure given some lambda that automatically deletes itself
template <class F>
Closure *MakeOneShotClosure(F f) {
  class Impl final : public Closure {
   public:
    Impl(F f) : f_(std::move(f)) {}

   private:
    void Execute(grpc_exec_ctx *exec_ctx, grpc_error *error) {
      f_(exec_ctx, error);
      delete this;
    }

    F f_;
  };
  return New<Impl>(std::move(f));
}

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_IOMGR_CLOSURE_H */
