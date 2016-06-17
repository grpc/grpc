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

#ifndef GRPCXX_IMPL_CODEGEN_SYNC_NO_CXX11_H
#define GRPCXX_IMPL_CODEGEN_SYNC_NO_CXX11_H

#include <grpc/impl/codegen/sync.h>

namespace grpc {

template <class mutex>
class lock_guard;
class condition_variable;

class mutex {
 public:
  mutex() { gpr_mu_init(&mu_); }
  ~mutex() { gpr_mu_destroy(&mu_); }

 private:
  ::gpr_mu mu_;
  template <class mutex>
  friend class lock_guard;
  friend class condition_variable;
};

template <class mutex>
class lock_guard {
 public:
  lock_guard(mutex &mu) : mu_(mu), locked(true) { gpr_mu_lock(&mu.mu_); }
  ~lock_guard() { unlock_internal(); }

 protected:
  void lock_internal() {
    if (!locked) gpr_mu_lock(&mu_.mu_);
    locked = true;
  }
  void unlock_internal() {
    if (locked) gpr_mu_unlock(&mu_.mu_);
    locked = false;
  }

 private:
  mutex &mu_;
  bool locked;
  friend class condition_variable;
};

template <class mutex>
class unique_lock : public lock_guard<mutex> {
 public:
  unique_lock(mutex &mu) : lock_guard<mutex>(mu) {}
  void lock() { this->lock_internal(); }
  void unlock() { this->unlock_internal(); }
};

class condition_variable {
 public:
  condition_variable() { gpr_cv_init(&cv_); }
  ~condition_variable() { gpr_cv_destroy(&cv_); }
  void wait(lock_guard<mutex> &mu) {
    mu.locked = false;
    gpr_cv_wait(&cv_, &mu.mu_.mu_, gpr_inf_future(GPR_CLOCK_REALTIME));
    mu.locked = true;
  }
  void notify_one() { gpr_cv_signal(&cv_); }
  void notify_all() { gpr_cv_broadcast(&cv_); }

 private:
  gpr_cv cv_;
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_SYNC_NO_CXX11_H
