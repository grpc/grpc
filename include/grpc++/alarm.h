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

/// An Alarm posts the user provided tag to its associated completion queue upon
/// expiry or cancellation.
#ifndef GRPCXX_ALARM_H
#define GRPCXX_ALARM_H

#include <grpc++/completion_queue.h>
#include <grpc++/impl/grpc_library.h>
#include <grpc++/support/time.h>

namespace grpc {

/// A thin wrapper around \a grpc_alarm (see / \a / src/core/surface/alarm.h).
class Alarm: public GrpcLibrary {
 public:
  /// Create a completion queue alarm instance associated to \a cq.
  ///
  /// Once the alarm expires (at \a deadline) or it's cancelled (see \a Cancel),
  /// an event with tag \a tag will be added to \a cq. If the alarm expired, the
  /// event's success bit will be true, false otherwise (ie, upon cancellation).
  Alarm(CompletionQueue* cq, gpr_timespec deadline, void* tag);

  /// Destroy the given completion queue alarm, cancelling it in the process.
  ~Alarm();

  /// Cancel a completion queue alarm. Calling this function over an alarm that
  /// has already fired has no effect.
  void Cancel();

 private:
  grpc_alarm* const alarm_;  // owned
};

}  // namespace grpc

#endif  // GRPCXX_ALARM_H
