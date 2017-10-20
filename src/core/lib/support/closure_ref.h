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

namespace grpc_core {

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

  ClosureRef(const VTable* vtable, void* env) : vtable_(vtable), env_(env) {}
  ClosureRef() : vtable_(&null_vtable_), env_(nullptr) {}

  // Run this closure, in-place if possible
  void Run(Args&&... args) { vtable_->run(env_, std::forward<Args>(args)...); }

  // Schedule this closure for execution in a safe environment
  void Schedule(Args&&... args) {
    vtable_->schedule(env_, std::forward<Args>(args)...);
  }

 private:
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
// Expect to write a code generator for this
//

namespace closure_impl {

template <class Scheduler, class Env, void (*F)()>
class FnClosure {
 public:
  static const typename ClosureRef<>::VTable vtable;

 private:
  static void Schedule(void* env) {
    Scheduler::Schedule([](void*) { F(); }, static_cast<Env*>(env));
  }
  static void Run(void* env) {
    Scheduler::Run([](void*) { F(); }, static_cast<Env*>(env));
  }
};

template <class Scheduler, class Env, void (*F)()>
const typename ClosureRef<>::VTable FnClosure<Scheduler, Env, F>::vtable = {
    FnClosure<Scheduler, Env, F>::Schedule, FnClosure<Scheduler, Env, F>::Run};

template <class Scheduler, class Env, typename T, void (*F)(T)>
class FnClosure1 {
 public:
  static const typename ClosureRef<T>::VTable vtable;

 private:
  static void Schedule(void* env, T&& t) {
    Scheduler::Schedule([t](void*) { F(t); }, static_cast<Env*>(env));
  }
  static void Run(void* env, T&& t) {
    Scheduler::Run([t](void*) { F(t); }, static_cast<Env*>(env));
  }
};

template <class Scheduler, class Env, typename T, void (*F)(T)>
const typename ClosureRef<T>::VTable FnClosure1<Scheduler, Env, T, F>::vtable =
    {FnClosure1<Scheduler, Env, T, F>::Schedule,
     FnClosure1<Scheduler, Env, T, F>::Run};

template <class Scheduler, class T, void (T::*F)()>
class MemClosure {
 public:
  static const typename ClosureRef<>::VTable vtable;

 private:
  static void Schedule(void* env) {
    Scheduler::Schedule([](void* env) { (static_cast<T*>(env)->*F)(); },
                        static_cast<T*>(env));
  }
  static void Run(void* env) {
    Scheduler::Run([](void* env) { (static_cast<T*>(env)->*F)(); },
                   static_cast<T*>(env));
  }
};

template <class Scheduler, class T, void (T::*F)()>
const typename ClosureRef<>::VTable MemClosure<Scheduler, T, F>::vtable = {
    MemClosure<Scheduler, T, F>::Schedule, MemClosure<Scheduler, T, F>::Run};

}  // namespace closure_impl

template <class Scheduler, void (*F)(), typename Env = std::nullptr_t>
ClosureRef<> MakeClosure(Env* env = nullptr) {
  return ClosureRef<>(&closure_impl::FnClosure<Scheduler, Env, F>::vtable, env);
}

template <class Scheduler, typename T, void (*F)(T),
          typename Env = std::nullptr_t>
ClosureRef<T> MakeClosure(Env* env = nullptr) {
  return ClosureRef<int>(
      &closure_impl::FnClosure1<Scheduler, Env, T, F>::vtable, env);
}

template <class Scheduler, typename T, void (T::*F)()>
ClosureRef<> MakeClosure(T* p) {
  return ClosureRef<>(&closure_impl::MemClosure<Scheduler, T, F>::vtable, p);
}

//
// SCHEDULERS
//

class AcquiresNoLocks {
 public:
  template <class T, class F>
  static void Schedule(F&& f, T* env) {
    f(env);
  }
  template <class T, class F>
  static void Run(F&& f, T* env) {
    f(env);
  }
};

// TODO(ctiller): move this into it's final place
#if 0
template <class F>
void QueueOnExecCtx(F&& f);

class RunInCurrentThread {
 public:
  template <class T, class F>
  static void Schedule(F&& f, T* env) {
    QueueOnExecCtx([f, env]() { f(env); });
  }
  template <class T, class F>
  static void Run(F&& f, T* env) {
    f(env);
  }
};

class RunInCombiner {
 public:
  template <class T, class F>
  static void Schedule(F&& f, T* env) {
    env->combiner()->Schedule([f, env]() { f(env); });
  }
  template <class T, class F>
  static void Run(F&& f, T* env) {
    env->combiner()->Run([f, env]() { f(env); });
  }
};

// Dummy combiner lock impl

class Combiner {
 public:
  template <class F>
  void Schedule(F&& f);
  template <class F>
  void Run(F&& f);
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
  // simple closures around functions, member functions
  ClosureRef<> print_line = MakeClosure<AcquiresNoLocks, PrintLine>();
  ClosureRef<int> print_int = MakeClosure<AcquiresNoLocks, int, PrintInt>();
  Foo foo;
  ClosureRef<> foo_cb = MakeClosure<AcquiresNoLocks, Foo, &Foo::Callback>(&foo);

  print_line.Run();
  print_int.Run(42);
  foo_cb.Run();

  // exec context test
  ClosureRef<> foo_cb_in_exec_ctx =
      MakeClosure<RunInCurrentThread, Foo, &Foo::Callback>(&foo);
  foo_cb_in_exec_ctx.Schedule();

  // combiner lock test - picks up combiner from Foo
  ClosureRef<> foo_cb_in_combiner =
      MakeClosure<RunInCombiner, Foo, &Foo::Callback>(&foo);
  foo_cb_in_combiner.Schedule();
  // can pass in a raw function too, but need to provide an environment
  // in this case something that provides a combiner() method
  ClosureRef<> print_line_in_combiner =
      MakeClosure<RunInCombiner, PrintLine>(&foo);
  print_line_in_combiner.Schedule();

  Hidden().Run();

  // empty closure test
  ClosureRef<> empty;
  empty.Run();
}
#endif

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_SUPPORT_CLOSURE_REF_H */
