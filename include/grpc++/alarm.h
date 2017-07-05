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

/// An Alarm posts the user provided tag to its associated completion queue upon
/// expiry or cancellation.
#ifndef GRPCXX_ALARM_H
#define GRPCXX_ALARM_H

#include <grpc++/impl/codegen/completion_queue.h>
#include <grpc++/impl/codegen/completion_queue_tag.h>
#include <grpc++/impl/codegen/grpc_library.h>
#include <grpc++/impl/codegen/time.h>
#include <grpc++/impl/grpc_library.h>
#include <grpc/grpc.h>

struct grpc_alarm;

namespace grpc {

class CompletionQueue;

/// A thin wrapper around \a grpc_alarm (see / \a / src/core/surface/alarm.h).
class Alarm : private GrpcLibraryCodegen {
 public:
  /// Create a completion queue alarm instance associated to \a cq.
  ///
  /// Once the alarm expires (at \a deadline) or it's cancelled (see \a Cancel),
  /// an event with tag \a tag will be added to \a cq. If the alarm expired, the
  /// event's success bit will be true, false otherwise (ie, upon cancellation).
  /// \internal We rely on the presence of \a cq for grpc initialization. If \a
  /// cq were ever to be removed, a reference to a static
  /// internal::GrpcLibraryInitializer instance would need to be introduced
  /// here. \endinternal.
  template <typename T>
  Alarm(CompletionQueue* cq, const T& deadline, void* tag)
      : tag_(tag),
        alarm_(grpc_alarm_create(cq->cq(), TimePoint<T>(deadline).raw_time(),
                                 static_cast<void*>(&tag_))) {}

  /// Alarms aren't copyable.
  Alarm(const Alarm&) = delete;
  Alarm& operator=(const Alarm&) = delete;

  /// Alarms are movable.
  Alarm(Alarm&& rhs) : tag_(rhs.tag_), alarm_(rhs.alarm_) {
    rhs.alarm_ = nullptr;
  }
  Alarm& operator=(Alarm&& rhs) {
    tag_ = rhs.tag_;
    alarm_ = rhs.alarm_;
    rhs.alarm_ = nullptr;
    return *this;
  }

  /// Destroy the given completion queue alarm, cancelling it in the process.
  ~Alarm() {
    if (alarm_ != nullptr) grpc_alarm_destroy(alarm_);
  }

  /// Cancel a completion queue alarm. Calling this function over an alarm that
  /// has already fired has no effect.
  void Cancel() { grpc_alarm_cancel(alarm_); }

 private:
  class AlarmEntry : public CompletionQueueTag {
   public:
    AlarmEntry(void* tag) : tag_(tag) {}
    bool FinalizeResult(void** tag, bool* status) override {
      *tag = tag_;
      return true;
    }

   private:
    void* tag_;
  };

  AlarmEntry tag_;
  grpc_alarm* alarm_;  // owned
};

}  // namespace grpc

#endif  // GRPCXX_ALARM_H
