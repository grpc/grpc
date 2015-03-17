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

ThreadPool::ThreadPool(int num_threads) : shutdown_(false) {
  for (int i = 0; i < num_threads; i++) {
    threads_.push_back(std::thread([this]() {
      for (;;) {
        // Wait until work is available or we are shutting down.
        auto have_work = [this]() { return shutdown_ || !callbacks_.empty(); };
        std::unique_lock<std::mutex> lock(mu_);
        if (!have_work()) {
          cv_.wait(lock, have_work);
        }
        // Drain callbacks before considering shutdown to ensure all work
        // gets completed.
        if (!callbacks_.empty()) {
          auto cb = callbacks_.front();
          callbacks_.pop();
          lock.unlock();
          cb();
        } else if (shutdown_) {
          return;
        }
      }
    }));
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
