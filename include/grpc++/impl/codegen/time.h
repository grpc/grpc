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

#ifndef GRPCXX_IMPL_CODEGEN_TIME_H
#define GRPCXX_IMPL_CODEGEN_TIME_H

#include <grpc++/impl/codegen/config.h>
#include <grpc/impl/codegen/time.h>

namespace grpc {

/* If you are trying to use CompletionQueue::AsyncNext with a time class that
   isn't either gpr_timespec or std::chrono::system_clock::time_point, you
   will most likely be looking at this comment as your compiler will have
   fired an error below. In order to fix this issue, you have two potential
   solutions:

     1. Use gpr_timespec or std::chrono::system_clock::time_point instead
     2. Specialize the TimePoint class with whichever time class that you
        want to use here. See below for two examples of how to do this.
 */

template <typename T>
class TimePoint {
 public:
  TimePoint(const T& time) { you_need_a_specialization_of_TimePoint(); }
  gpr_timespec raw_time() {
    gpr_timespec t;
    return t;
  }

 private:
  void you_need_a_specialization_of_TimePoint();
};

template <>
class TimePoint<gpr_timespec> {
 public:
  TimePoint(const gpr_timespec& time) : time_(time) {}
  gpr_timespec raw_time() { return time_; }

 private:
  gpr_timespec time_;
};

}  // namespace grpc

#ifndef GRPC_CXX0X_NO_CHRONO

#include <chrono>

#include <grpc/impl/codegen/time.h>

namespace grpc {

// from and to should be absolute time.
void Timepoint2Timespec(const std::chrono::system_clock::time_point& from,
                        gpr_timespec* to);
void TimepointHR2Timespec(
    const std::chrono::high_resolution_clock::time_point& from,
    gpr_timespec* to);

std::chrono::system_clock::time_point Timespec2Timepoint(gpr_timespec t);

template <>
class TimePoint<std::chrono::system_clock::time_point> {
 public:
  TimePoint(const std::chrono::system_clock::time_point& time) {
    Timepoint2Timespec(time, &time_);
  }
  gpr_timespec raw_time() const { return time_; }

 private:
  gpr_timespec time_;
};

}  // namespace grpc

#endif  // !GRPC_CXX0X_NO_CHRONO

#endif  // GRPCXX_IMPL_CODEGEN_TIME_H
