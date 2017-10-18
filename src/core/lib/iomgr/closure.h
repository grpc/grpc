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

class Combiner;

/// Closure base type
/// TODO(ctiller): ... write stuff here ...
template <typename... Args>
class Closure {
 public:
  Closure() {}
  /// Schedule this closure to run in the future
  /// This can be called from anywhere we have an exec_ctx (regardless of lock
  /// state)
  virtual void Schedule(Args&&... args) = 0;
  /// Run this closure from a closure-safe run point
  /// It's the callers responsibility to ensure that no locks are held by any
  /// possible path to this code.
  /// Note that within a closure callback is always (by definition) a safe place
  /// to call Run (assuming no locks are held)
  virtual void Run(Args&&... args) = 0;

 protected:
  ~Closure() {}
};

// Scheduler:
//  template <class F> void DoSchedule(F&& f);
//  template <class F> void DoRun(F&& f);

template <template <class Derived> class Scheduler, class T, T>
class MemberClosure;

template <template <class Derived> class Scheduler, class T, typename... Args,
          void (T::*F)(Args...)>
class MemberClosure<Scheduler, void (T::*)(Args...), F> final
    : public Closure<Args...>,
      public Scheduler<MemberClosure<Scheduler, void (T::*)(Args...), F>> {
 public:
  MemberClosure(T* p) : p_(p) {}

  void Schedule(Args&&... args) override {
    this->DoSchedule([this, args...]() { (p_->*F)(args...); });
  }

  void Run(Args&&... args) override {
    this->DoRun([this, args...]() { (p_->*F)(args...); });
  }

  Combiner* combiner() { return p_->combiner(); }

 private:
  T* const p_;
};

struct grpc_error;

template <template <class Derived> class Scheduler>
class LegacyClosure : public Closure<grpc_error*>,
                      public Scheduler<LegacyClosure<Scheduler>> {
 public:
  LegacyClosure(void (*const f)(void* arg, grpc_error* error), void* arg)
      : f_(f), arg_(arg) {}

  void Schedule(grpc_error*&& error) override {
    this->DoSchedule([this, error]() { f_(arg_, error); });
  }

  void Run(grpc_error*&& error) override {
    this->DoRun([this, error]() { f_(arg_, error); });
  }

 private:
  void (*const f_)(void* arg, grpc_error* error);
  void* arg_;
};

template <template <class Derived> class Scheduler,
          void (*f)(void* arg, grpc_error* error)>
class LegacyClosureT : public Closure<grpc_error*>,
                       public Scheduler<LegacyClosureT<Scheduler, f>> {
 public:
  explicit LegacyClosureT(void* arg) : arg_(arg) {}

  void Schedule(grpc_error*&& error) override {
    this->DoSchedule([this, error]() { f(arg_, error); });
  }

  void Run(grpc_error*&& error) override {
    this->DoRun([this, error]() { f(arg_, error); });
  }

 private:
  void* arg_;
};

template <class Derived>
class AcquiresNoLocks {
 protected:
  template <class F>
  void DoSchedule(F&& f) {
    f();
  }
  template <class F>
  void DoRun(F&& f) {
    f();
  }
};

template <class Derived>
class RunOnCombiner {
 protected:
  template <class F>
  void DoSchedule(F&& f) {
    static_cast<Derived*>(this)->combiner()->Schedule(f);
  }
  template <class F>
  void DoRun(F&& f) {
    static_cast<Derived*>(this)->combiner()->Run(f);
  }
};

class Combiner {
 public:
  void Schedule(std::function<void()>);
  void Run(std::function<void()>);
};

void foo(void*, grpc_error*);

void test() {
  class C {
    void Foo(int a, int b, int c) { printf("%d %d %d", a, b, c); }
    Combiner c_;

   public:
    MemberClosure<AcquiresNoLocks, void (C::*)(int, int, int), &C::Foo>
        foo_closure{this};
    Combiner* combiner() { return &c_; }
    MemberClosure<RunOnCombiner, void (C::*)(int, int, int), &C::Foo>
        foo_on_combiner_closure{this};
  };
  C c;
  LegacyClosure<AcquiresNoLocks> a(foo, nullptr);
  a.Run(nullptr);
  LegacyClosureT<AcquiresNoLocks, foo> b(nullptr);
  b.Schedule(nullptr);
  c.foo_closure.Run(sizeof(a), sizeof(b), sizeof(c.foo_on_combiner_closure));
  c.foo_on_combiner_closure.Run(1, 2, 3);
}

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_IOMGR_CLOSURE_H */
