/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPCXX_IMPL_THD_NO_CXX11_H
#define GRPCXX_IMPL_THD_NO_CXX11_H

#include <grpc/support/thd.h>

namespace grpc {

class thread {
 public:
  template <class T>
  thread(void (T::*fptr)(), T *obj) {
    func_ = new thread_function<T>(fptr, obj);
    joined_ = false;
    start();
  }
  template <class T, class U>
  thread(void (T::*fptr)(U arg), T *obj, U arg) {
    func_ = new thread_function_arg<T, U>(fptr, obj, arg);
    joined_ = false;
    start();
  }
  ~thread() {
    if (!joined_) std::terminate();
    delete func_;
  }
  thread(thread &&other)
      : func_(other.func_), thd_(other.thd_), joined_(other.joined_) {
    other.joined_ = true;
    other.func_ = NULL;
  }
  void join() {
    gpr_thd_join(thd_);
    joined_ = true;
  }

 private:
  void start() {
    gpr_thd_options options = gpr_thd_options_default();
    gpr_thd_options_set_joinable(&options);
    gpr_thd_new(&thd_, thread_func, (void *)func_, &options);
  }
  static void thread_func(void *arg) {
    thread_function_base *func = (thread_function_base *)arg;
    func->call();
  }
  class thread_function_base {
   public:
    virtual ~thread_function_base() {}
    virtual void call() = 0;
  };
  template <class T>
  class thread_function : public thread_function_base {
   public:
    thread_function(void (T::*fptr)(), T *obj) : fptr_(fptr), obj_(obj) {}
    virtual void call() { (obj_->*fptr_)(); }

   private:
    void (T::*fptr_)();
    T *obj_;
  };
  template <class T, class U>
  class thread_function_arg : public thread_function_base {
   public:
    thread_function_arg(void (T::*fptr)(U arg), T *obj, U arg)
        : fptr_(fptr), obj_(obj), arg_(arg) {}
    virtual void call() { (obj_->*fptr_)(arg_); }

   private:
    void (T::*fptr_)(U arg);
    T *obj_;
    U arg_;
  };
  thread_function_base *func_;
  gpr_thd_id thd_;
  bool joined_;

  // Disallow copy and assign.
  thread(const thread &);
  void operator=(const thread &);
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_THD_NO_CXX11_H
