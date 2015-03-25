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

#include "src/cpp/server/thread_pool.h"

namespace grpc {

#ifdef __GNUC__
#if (__GNUC__ * 100 + __GNUC_MINOR__ < 406)
#define GRPC_NO_NULLPTR
#endif
#endif

#ifdef GRPC_NO_NULLPTR
#include <memory>
const class {
public:
  template <class T> operator T*() const {return static_cast<T *>(0);}
  template <class T> operator std::unique_ptr<T>() const {
    return std::unique_ptr<T>(static_cast<T *>(0));
  }
  operator bool() const {return false;}
private:
  void operator&() const = delete;
} nullptr = {};
#endif

ThreadPool::ThreadPool(int num_threads) : shutdown_(false) {
  for (int i = 0; i < num_threads; i++) {
    threads_.push_back(std::thread(&ThreadPool::ThreadFunc, this));
  }
}

ThreadPool::~ThreadPool() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    shutdown_ = true;
    cv_.notify_all();
  }
  for (auto& t : threads_) {
    t.join();
  }
}

void ThreadPool::ScheduleCallback(const std::function<void()>& callback) {
  std::lock_guard<std::mutex> lock(mu_);
  callbacks_.push(callback);
  cv_.notify_one();
}

}  // namespace grpc
