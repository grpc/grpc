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

#include <stdio.h>
#include <stdlib.h>
#include <utility>
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

  // Run this closure, in-place if possible
  // Requires that no grpc-locks be held in the current callstack
  void UnsafeRun(Args&&... args) {
    vtable_->run(env_, std::forward<Args>(args)...);
  }

  // Schedule this closure for execution in a safe environment
  void Schedule(Args&&... args) {
    vtable_->schedule(env_, std::forward<Args>(args)...);
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
    ClosureRef<Args...>::null_exec, ClosureRef<Args...>::null_exec,
};

//
// MakeClosure implementation details...
//

namespace closure_impl {

template <class Scheduler>
class MakesClosuresForScheduler {
 public:
  template <class... Args>
  class MakeClosureWithArgs {
   private:
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

   public:
    template <void (*F)(Args...), typename Env = void>
    static ClosureRef<Args...> FromFreeFunction(Env* env = nullptr) {
      return ClosureRef<Args...>(&FuncClosure<Env, F>::vtable, env);
    }

    template <class C, void (C::*F)(Args...)>
    static ClosureRef<Args...> FromMemberFunction(C* p) {
      return ClosureRef<Args...>(&MemberClosure<C, F>::vtable, p);
    }
  };
};

template <class Scheduler>
template <class... Args>
template <class Env, void (*F)(Args...)>
const typename ClosureRef<Args...>::VTable MakesClosuresForScheduler<
    Scheduler>::MakeClosureWithArgs<Args...>::FuncClosure<Env, F>::vtable = {
    Schedule, Run};

template <class Scheduler>
template <class... Args>
template <class C, void (C::*F)(Args...)>
const typename ClosureRef<Args...>::VTable MakesClosuresForScheduler<
    Scheduler>::MakeClosureWithArgs<Args...>::MemberClosure<C, F>::vtable = {
    Schedule, Run};

}  // namespace closure_impl

//
// SCHEDULERS
//

class AcquiresNoLocks
    : public closure_impl::MakesClosuresForScheduler<AcquiresNoLocks> {
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

// TODO(ctiller): move this into it's final place
#if 0
template <class F>
void QueueOnExecCtx(F&& f);

class RunInCurrentThread : public closure_impl::MakesClosuresForScheduler<RunInCurrentThread> {
 public:
  template <class T, class F>
  static void Schedule(F&& f, T* env) {
    QueueOnExecCtx([f, env]() { f(env); });
  }
  template <class T, class F>
  static void UnsafeRun(F&& f, T* env) {
    f(env);
  }
};

class RunInCombiner : public closure_impl::MakesClosuresForScheduler<RunInCombiner> {
 public:
  template <class T, class F>
  static void Schedule(F&& f, T* env) {
    env->combiner()->Schedule([f, env]() { f(env); });
  }
  template <class T, class F>
  static void UnsafeRun(F&& f, T* env) {
    env->combiner()->UnsafeRun([f, env]() { f(env); });
  }
};

// Dummy combiner lock impl

class Combiner {
 public:
  template <class F>
  void Schedule(F&& f);
  template <class F>
  void UnsafeRun(F&& f);
};

//
// TEST CODE
//

void PrintLine();
void PrintInt(int);

class Foo {
 public:
  void Callback();

  Combiner* combiner() { return &combiner_; }

 private:
  Combiner combiner_;
};

ClosureRef<> Hidden();

void test() {
}
#endif

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_SUPPORT_CLOSURE_REF_H */
