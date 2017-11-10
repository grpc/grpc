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

#ifndef GRPC_CORE_LIB_SUPPORT_CLOSURE_REF_H
#define GRPC_CORE_LIB_SUPPORT_CLOSURE_REF_H

// This file declares gRPC's ClosureRef infrastructure and a single scheduler
// that makes use of it: closure_scheduler::NonLockingScheduler.
//
// ClosureRefs are used to pass functions down our stack that will be called
// back later. They are always created against some scheduling class - this
// scheduling class tells the closure where it should run (on the same thread,
// vs in some different thread pull; serially in a combiner vs concurrently;
// etc... - individual scheduler classes will list the guarantees they provide)
//
// A ClosureRef itself is non-copyable but movable, and MUST have either its
// UnsafeRun or Schedule function called before destruction of a non-null
// ClosureRef. Not doing so will cause a runtime crash. Doing so twice will
// cause a runtime crash.
//
// UnsafeRun will endeavour to call the callback within the current callstack
// where allowed by the scheduler. It's only safe to call this member when it is
// known that there is no possible call path leading to the execution that holds
// a mutex owned by gRPC code.
//
// Schedule will wait for callback execution until such a safe location is
// reached.
//
// A few places we guarantee to be safe for callback execution:
// - StartStreamOp calls in filters & transports
// - callback functions executed via this closure system (if it was safe to
//   execute one callback, it's safe to execute another)
// - Top-of-thread stacks (we can visually identify a lack of mutex
//   acquisitions)
// - API entry points (for the same reasoning)
//
// Schedulers derive from closure_impl::MakesClosuresForScheduler using the
// curiously recurring template pattern. They must declare two methods for
// scheduling:
//
//   template <class T, class F>
//   static void Schedule(F&& f, T* env);
//   template <class T, class F>
//   static void UnsafeRun(F&& f, T* env);
//
// T is an arbitrary 'environment' type - Schedulers may place constraints on
// what that environment looks like, and it's supplied at ClosureRef creation
// time. F is a functor to run (which will execute the closure).
//
// All Schedulers inherit a set of constructor functions for ClosureRef, to
// create closures that will be scheduled using their algorithm. These
// constructors are created from a nested set of template classes to help
// distinguish what all of the different type arguments are for. For scheduler
// $SCHED$:
//
// $SCHED$::MakeClosureWithArgs<TL> defines the first level of the hierarchy
//   with TL giving a list of types for the callback arguments for this closure
//   .. for instance, $SCHEDULER$::MakeClosureWithArgs<int, Error*> declares a
//   closure that takes arguments (int, Error*); these arguments are expected to
//   be suppled to either UnsafeRun or Schedule on the ClosureRef when it comes
//   time to execute it.
//
// Within MakeClosureWithArgs<TL> we declare constructor functions. In some
// cases these functions take pointers as template arguments. This allows us to
// keep the ClosureRef type to exactly two points in size, whilst also allowing
// us to use NO memory to store data regarding an uninvoked closure.
//
// Constructors available:
//   FromFreeFunction<F>()  - constructs a ClosureRef around a free function F
//                            (which should be a function pointer) - the closure
//                            environment is assumed to be nullptr
//                    sample: void foo();
//                            CR<> c = $SCHED$::MakeClosureWithArgs<>
//                                            ::FromFreeFunction<&foo>()
//   FromFreeFunction<F>(p) - constructs a ClosureRef around a free function F
//                            (which should be a function pointer) - the closure
//                            environment is taken as p, which can be of
//                            arbitrary pointer type
//   FromNonRefCountedMemberFunction<C, F>(p)
//                          - constructs a ClosureRef around a member function F
//                            from class C. The C argument here is necessary to
//                            get template deduction to work. p declares the
//                            instance of the object to call against (it must be
//                            of type C), and defines the environment for the
//                            closure
//                          - this variant ignores ref counting - it's the
//                            callers responsibility to ensure
//                    sample: struct Foo { void foo(); }
//                            Foo foo;
//                            CR<> c = $SCHED$::MakeClosureWithArgs<>
//                                            ::FromNonRefCountedMemberFunction
//                                              <Foo, &Foo::foo>(&foo);
//   FromRefCountedMemberFunction<C, F>(p)
//                          - as FromNonRefCountedMemberFunction, but calls Ref
//                            upon creation, and Unref after F returns, thus
//                            ensuring that p is present for the lifetime of the
//                            ClosureRef
//   FromRefCountedMemberFunctionWithBarrier
//                          - experimental for now barrier closure system
//   FromFunctor            - creates a ClosureRef from an arbitrary functor
//                            this variant performs a memory allocation to store
//                            the functor object (and destroys said object after
//                            the callback has completed)

#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <utility>
#include "src/core/lib/support/memory.h"
#include "src/core/lib/support/tuple.h"

namespace grpc_core {

namespace closure_impl {
// forward-declaration
template <class Scheduler>
class MakesClosuresForScheduler;
}  // namespace closure_impl

// Value type reference to some closure
// Template arguments list argument types to the closure
// Closures have an implicit scheduling policy bound when they are created
template <typename... Args>
class ClosureRef {
 public:
  struct VTable {
    void (*schedule)(void* env, Args&&... args);
    void (*run)(void* env, Args&&... args);
  };

  ClosureRef() : vtable_(&null_vtable_), env_(nullptr) {}
  ClosureRef(ClosureRef&& other) : vtable_(other.vtable_), env_(other.env_) {
    other.vtable_ = &null_vtable_;
  }
  ClosureRef& operator=(ClosureRef&& other) {
    // can only assign over an empty ClosureRef
    GPR_ASSERT(vtable_ == &null_vtable_);
    vtable_ = other.vtable_;
    env_ = other.env_;
    other.vtable_ = &null_vtable_;
    return *this;
  }
  // explicitly disable copying: ClosureRef can be moved, but never copied
  // This allows us to enforce that a ClosureRef is executed once and only once
  ClosureRef(const ClosureRef&) = delete;
  ClosureRef& operator=(const ClosureRef&) = delete;
  ~ClosureRef() {
    // a ClosureRef must be invoked before being destroyed
    GPR_ASSERT(vtable_ == &null_vtable_);
  }

  // Run this closure, in-place if possible
  // Requires that no grpc-locks be held in the current callstack
  void UnsafeRun(Args&&... args) {
    vtable_->run(env_, std::forward<Args>(args)...);
    vtable_ = &null_vtable_;
  }

  // Schedule this closure for execution in a safe environment
  void Schedule(Args&&... args) {
    vtable_->schedule(env_, std::forward<Args>(args)...);
    vtable_ = &null_vtable_;
  }

 private:
  template <class Scheduler>
  friend class closure_impl::MakesClosuresForScheduler;

  ClosureRef(const VTable* vtable, void* env) : vtable_(vtable), env_(env) {}

  const VTable* vtable_;
  void* env_;

  static void null_exec(void* env, Args&&... args) { abort(); }

  static const VTable null_vtable_;
};

template <typename... Args>
const typename ClosureRef<Args...>::VTable ClosureRef<Args...>::null_vtable_ = {
    ClosureRef<Args...>::null_exec,
    ClosureRef<Args...>::null_exec,
};

//
// MakeClosure implementation details...
//

template <class Barrier>
struct BarrierOps;

template <>
struct BarrierOps<gpr_atm> {
  static bool PassesBarrier(gpr_atm* barrier) {
    gpr_atm last = gpr_atm_full_fetch_add(barrier, -1);
    GPR_ASSERT(last > 0);
    return last == 1;
  }
};

template <>
struct BarrierOps<int> {
  static bool PassesBarrier(int* barrier) {
    int value = --*barrier;
    GPR_ASSERT(value >= 0);
    return value == 0;
  }
};

namespace closure_impl {

template <class Scheduler, class... Args>
struct ClosureImpl {
  template <class Env, void (*F)(Args...)>
  class FuncClosure {
   public:
    static const typename ClosureRef<Args...>::VTable vtable;

    static void Schedule(void* env, Args&&... args) {
      Scheduler::Schedule(FuncClosure(std::forward<Args>(args)...),
                          static_cast<Env*>(env));
    }
    static void Run(void* env, Args&&... args) {
      Scheduler::UnsafeRun(FuncClosure(std::forward<Args>(args)...),
                           static_cast<Env*>(env));
    }

    void operator()(Env* env) { TupleCall(F, std::move(args_)); }

   private:
    FuncClosure(Args&&... args) : args_(std::forward<Args>(args)...) {}

    Tuple<Args...> args_;
  };

  template <class C, void (C::*F)(Args...)>
  class MemberClosure {
   public:
    static const typename ClosureRef<Args...>::VTable vtable;

    static void Schedule(void* env, Args&&... args) {
      Scheduler::Schedule(MemberClosure(std::forward<Args>(args)...),
                          static_cast<C*>(env));
    }
    static void Run(void* env, Args&&... args) {
      Scheduler::UnsafeRun(MemberClosure(std::forward<Args>(args)...),
                           static_cast<C*>(env));
    }

    void operator()(C* p) { TupleCallMember(p, F, std::move(args_)); }

   private:
    MemberClosure(Args&&... args) : args_(std::forward<Args>(args)...) {}

    Tuple<Args...> args_;
  };

  template <class C, void (C::*F)(Args...)>
  class RefCountedMemberClosure {
   public:
    static const typename ClosureRef<Args...>::VTable vtable;

    static void Schedule(void* env, Args&&... args) {
      Scheduler::Schedule(RefCountedMemberClosure(std::forward<Args>(args)...),
                          static_cast<C*>(env));
    }
    static void Run(void* env, Args&&... args) {
      Scheduler::UnsafeRun(RefCountedMemberClosure(std::forward<Args>(args)...),
                           static_cast<C*>(env));
    }

    void operator()(C* p) {
      TupleCallMember(p, F, std::move(args_));
      p->Unref();
    }

   protected:
    RefCountedMemberClosure(Args&&... args)
        : args_(std::forward<Args>(args)...) {}

   private:
    Tuple<Args...> args_;
  };

  template <class C, void (C::*F)(Args...), class BarrierType,
            BarrierType(C::*Barrier)>
  class RefCountedMemberWithBarrier : public RefCountedMemberClosure<C, F> {
   public:
    static const typename ClosureRef<Args...>::VTable vtable;

    static void Schedule(void* env, Args&&... args) {
      auto p = static_cast<C*>(env);
      if (BarrierOps<BarrierType>::PassesBarrier(&(p->*Barrier), &args...)) {
        Scheduler::Schedule(
            RefCountedMemberWithBarrier(std::forward<Args>(args)...), p);
      } else {
        p->Unref();
      }
    }
    static void Run(void* env, Args&&... args) {
      auto p = static_cast<C*>(env);
      if (BarrierOps<BarrierType>::PassesBarrier(&(p->*Barrier), &args...)) {
        Scheduler::UnsafeRun(
            RefCountedMemberWithBarrier(std::forward<Args>(args)...), p);
      } else {
        p->Unref();
      }
    }
  };

  template <class F>
  class FunctorClosure {
   public:
    static const typename ClosureRef<Args...>::VTable vtable;

    explicit FunctorClosure(F&& f) : f_(std::move(f)) {}

    static void Schedule(void* env, Args&&... args) {
      static_cast<FunctorClosure*>(env)->args_ =
          Tuple<Args...>(std::forward<Args>(args)...);
      Scheduler::Schedule(
          [](FunctorClosure* functor) {
            TupleCall(functor->f_, std::move(functor->args_));
            Delete(functor);
          },
          static_cast<FunctorClosure*>(env));
    }
    static void Run(void* env, Args&&... args) {
      static_cast<FunctorClosure*>(env)->args_ =
          Tuple<Args...>(std::forward<Args>(args)...);
      Scheduler::UnsafeRun(
          [](FunctorClosure* functor) {
            TupleCall(functor->f_, std::move(functor->args_));
            Delete(functor);
          },
          static_cast<FunctorClosure*>(env));
    }

   private:
    F f_;
    Tuple<Args...> args_;
  };
};

template <class Scheduler>
class MakesClosuresForScheduler {
 public:
  template <class... Args>
  class MakeClosureWithArgs {
   public:
    template <void (*F)(Args...), typename Env = void>
    static ClosureRef<Args...> FromFreeFunction(Env* env = nullptr) {
      return ClosureRef<Args...>(
          &ClosureImpl<Scheduler, Args...>::template FuncClosure<Env,
                                                                 F>::vtable,
          env);
    }

    template <class C, void (C::*F)(Args...)>
    static ClosureRef<Args...> FromNonRefCountedMemberFunction(C* p) {
      return ClosureRef<Args...>(
          &ClosureImpl<Scheduler, Args...>::template MemberClosure<C,
                                                                   F>::vtable,
          p);
    }

    template <class C, void (C::*F)(Args...)>
    static ClosureRef<Args...> FromRefCountedMemberFunction(C* p) {
      p->Ref();
      return ClosureRef<Args...>(
          &ClosureImpl<Scheduler,
                       Args...>::template RefCountedMemberClosure<C, F>::vtable,
          p);
    }

    template <class C, void (C::*F)(Args...), class BarrierType,
              BarrierType(C::*Barrier)>
    static ClosureRef<Args...> FromRefCountedMemberFunctionWithBarrier(C* p) {
      p->Ref();
      return ClosureRef<Args...>(
          &ClosureImpl<Scheduler, Args...>::
              template RefCountedMemberWithBarrier<C, F, BarrierType,
                                                   Barrier>::vtable,
          p);
    }

    template <class F>
    static ClosureRef<Args...> FromFunctor(F&& f) {
      typedef
          typename ClosureImpl<Scheduler, Args...>::template FunctorClosure<F>
              Impl;
      return ClosureRef<Args...>(&Impl::vtable, New<Impl>(std::move(f)));
    }
  };
};

template <class Scheduler, class... Args>
template <class Env, void (*F)(Args...)>
const typename ClosureRef<Args...>::VTable
    ClosureImpl<Scheduler, Args...>::FuncClosure<Env, F>::vtable = {Schedule,
                                                                    Run};

template <class Scheduler, class... Args>
template <class C, void (C::*F)(Args...)>
const typename ClosureRef<Args...>::VTable
    ClosureImpl<Scheduler, Args...>::MemberClosure<C, F>::vtable = {Schedule,
                                                                    Run};

template <class Scheduler, class... Args>
template <class C, void (C::*F)(Args...)>
const typename ClosureRef<Args...>::VTable
    ClosureImpl<Scheduler, Args...>::RefCountedMemberClosure<C, F>::vtable = {
        Schedule, Run};

template <class Scheduler, class... Args>
template <class C, void (C::*F)(Args...), class BarrierType,
          BarrierType(C::*Barrier)>
const typename ClosureRef<Args...>::VTable ClosureImpl<Scheduler, Args...>::
    RefCountedMemberWithBarrier<C, F, BarrierType, Barrier>::vtable = {Schedule,
                                                                       Run};

template <class Scheduler, class... Args>
template <class F>
const typename ClosureRef<Args...>::VTable
    ClosureImpl<Scheduler, Args...>::FunctorClosure<F>::vtable = {Schedule,
                                                                  Run};

}  // namespace closure_impl

//
// SCHEDULERS
//
// Most schedulers depend on the iomgr runtime environment, and so are declared
// there. Schedulers here are only those that have no runtime dependencies on
// other larger systems.
//

// Scheduler for callbacks that promise to acquire no mutexes
// In this case Schedule is equivalent to UnsafeRun
class NonLockingScheduler
    : public closure_impl::MakesClosuresForScheduler<NonLockingScheduler> {
 public:
  template <class T, class F>
  static void Schedule(F&& f, T* env) {
    f(env);
  }
  template <class T, class F>
  static void UnsafeRun(F&& f, T* env) {
    f(env);
  }
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_SUPPORT_CLOSURE_REF_H */
