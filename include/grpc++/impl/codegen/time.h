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

#ifndef GRPCXX_IMPL_CODEGEN_TIME_H
#define GRPCXX_IMPL_CODEGEN_TIME_H

#include <chrono>

#include <grpc++/impl/codegen/config.h>
#include <grpc/impl/codegen/grpc_types.h>

namespace grpc {

// forward declarations
class Alarm;
class ChannelInterface;
class ClientContext;
class CompletionQueue;
class ServerInterface;

/** If you are trying to use CompletionQueue::AsyncNext with a time class that
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

 private:
  gpr_timespec raw_time() {
    gpr_timespec t;
    return t;
  }
  void you_need_a_specialization_of_TimePoint();
};

template <>
class TimePoint<gpr_timespec> {
 public:
  TimePoint(const gpr_timespec& time) : time_(time) {}

 private:
  friend class Alarm;
  friend class ChannelInterface;
  friend class ClientContext;
  friend class CompletionQueue;
  friend class ServerInterface;
 
  gpr_timespec raw_time() { return time_; }
  gpr_timespec time_;
};

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
 private:
  friend class Alarm;
  friend class ChannelInterface;
  friend class ClientContext;
  friend class CompletionQueue;
  friend class ServerInterface;

  gpr_timespec raw_time() const { return time_; }
  gpr_timespec time_;
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_TIME_H
