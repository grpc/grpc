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
#include "src/core/lib/support/atomic.h"
#include "src/core/lib/support/memory.h"
#include "src/core/lib/support/mpscq.h"

namespace grpc_core {

class Closure;

// Base class for closure schedulers
// TODO(ctiller): ... write stuff here ...
class ClosureScheduler {
 public:
  class ScheduledClosure {
   public:
    ScheduledClosure(Closure *closure, grpc_error *error);

   private:
    Closure *closure_;
    grpc_error *error_;
  };

  virtual void Schedule(grpc_exec_ctx *exec_ctx,
                        ScheduledClosure scheduled_closure) = 0;
  virtual void Run(grpc_exec_ctx *exec_ctx,
                   ScheduledClosure scheduled_closure) = 0;
};

/// Closure base type
/// TODO(ctiller): ... write stuff here ...
class Closure {
 public:
  Closure(ClosureScheduler *scheduler) : scheduler_(scheduler) {}
  /// Schedule this closure to run in the future
  /// This can be called from anywhere we have an exec_ctx (regardless of lock
  /// state)
  void Schedule(grpc_exec_ctx *exec_ctx, grpc_error *error);
  /// Run this closure from a closure-safe run point
  /// It's the callers responsibility to ensure that no locks are held by any
  /// possible path to this code.
  /// Note that within a closure callback is always (by definition) a safe place
  /// to call Run (assuming no locks are held)
  void Run(grpc_exec_ctx *exec_ctx, grpc_error *error);

 protected:
  ~Closure() {}

 private:
  /// Allow access to Execute
  friend class ClosureScheduler::ScheduledClosure;

  /// Actual invoked callback for the closure
  virtual void Execute(grpc_exec_ctx *exec_ctx, grpc_error *error) = 0;

  ClosureScheduler *const scheduler_;
};

inline void Closure::Schedule(grpc_exec_ctx *exec_ctx, grpc_error *error) {
  scheduler_->Schedule(exec_ctx,
                       ClosureScheduler::ScheduledClosure(this, error));
}

inline void Closure::Run(grpc_exec_ctx *exec_ctx, grpc_error *error) {
  scheduler_->Run(exec_ctx, ClosureScheduler::ScheduledClosure(this, error));
}

namespace closure_impl {
// Per-closure scheduler to override how Schedule/Run are implemented for a
// barrier
class BarrierScheduler final : public ClosureScheduler {
 public:
  BarrierScheduler(ClosureScheduler *next) : barrier_(0), next_(next) {}

  virtual void Schedule(grpc_exec_ctx *exec_ctx,
                        ScheduledClosure scheduled_closure) override {
    if (MaybePassBarrier(&scheduled_closure)) {
      next_->Schedule(exec_ctx, scheduled_closure);
    }
  }
  virtual void Run(grpc_exec_ctx *exec_ctx,
                   ScheduledClosure scheduled_closure) override {
    if (MaybePassBarrier(&scheduled_closure)) {
      next_->Run(exec_ctx, scheduled_closure);
    }
  }

  void InitiateOne() { gpr_atm_no_barrier_fetch_add(&barrier_, 1); }

 private:
  bool MaybePassBarrier(ScheduledClosure *scheduled_closure);

  gpr_atm barrier_;
  ClosureScheduler *const next_;
};
}  // namespace closure_impl

/// Adaptor that produces closures which must be called multiple times, and only
/// on the Nth call (where N == the number of prior Initiate() calls) does the
/// closure get executed
template <class ContainedClosure>
class BarrierClosure final {
 public:
  template <typename... Args>
  BarrierClosure(ClosureScheduler *scheduler, Args &&... args)
      : scheduler_(scheduler),
        closure_(&scheduler_, std::forward<Args>(args)...) {}

  /// Produce a new closure
  Closure *Initiate() {
    scheduler_.InitiateOne();
    return &closure_;
  }

 private:
  closure_impl::BarrierScheduler scheduler_;
  ContainedClosure closure_;
};

/// A closure over a member function
/// Templatized to avoid excessive virtual calls
///
/// Example:
/// class Foo {
///  public:
///   Foo() : foo_closure_(this) {}
///  private:
///   void DoStuff(grpc_exec_ctx *exec_ctx, grpc_error *error) { /* ... */ }
///   MemberClosure<Foo, &Foo::DoStuff> foo_closure_;
/// };
template <class T,
          void (T::*Callback)(grpc_exec_ctx *exec_ctx, grpc_error *error)>
class MemberClosure final : public Closure {
 public:
  MemberClosure(ClosureScheduler *scheduler, T *p)
      : Closure(scheduler), p_(p) {}

 private:
  void Execute(grpc_exec_ctx *exec_ctx, grpc_error *error) override {
    (p_->*Callback)(exec_ctx, error);
  }

  T *p_;
};

// Create a closure that can be called repeatedly (on the heap) given some
// lambda
//
// Example:
// auto c = MakeRepeatableClosure(OnExecCtx(),
//                                [](grpc_exec_ctx *exec_ctx,
//                                   grpc_error *error) {});
// c->Schedule(exec_ctx, GRPC_ERROR_NONE);
template <class F>
UniquePtr<Closure> MakeRepeatableClosure(ClosureScheduler *scheduler, F f) {
  class Impl final : public Closure {
   public:
    Impl(ClosureScheduler *scheduler, F f)
        : Closure(scheduler), f_(std::move(f)) {}

   private:
    void Execute(grpc_exec_ctx *exec_ctx, grpc_error *error) override {
      f_(exec_ctx, error);
    }

    F f_;
  };
  return MakeUnique<Impl>(scheduler, std::move(f));
}

// Create a closure given some lambda that automatically deletes itself after
// execution
//
// Example:
// MakeOneShotClosure(OnExecCtx(),
//                    [](grpc_exec_ctx *exec_ctx, grpc_error *error) {})
//   ->Schedule(exec_ctx, GRPC_ERROR_NONE);
template <class F>
Closure *MakeOneShotClosure(F f) {
  class Impl final : public Closure {
   public:
    Impl(F f) : f_(std::move(f)) {}

   private:
    void Execute(grpc_exec_ctx *exec_ctx, grpc_error *error) override {
      f_(exec_ctx, error);
      Delete(this);
    }

    F f_;
  };
  return New<Impl>(std::move(f));
}

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_IOMGR_CLOSURE_H */
