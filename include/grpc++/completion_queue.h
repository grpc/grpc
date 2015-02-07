/*
 *
 * Copyright 2014, Google Inc.
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

#ifndef __GRPCPP_COMPLETION_QUEUE_H__
#define __GRPCPP_COMPLETION_QUEUE_H__

#include <functional>

struct grpc_completion_queue;

namespace grpc {

// grpc_completion_queue wrapper class
class CompletionQueue {
 public:
  CompletionQueue();
  ~CompletionQueue();

  // Blocking read from queue.
  // Returns true if an event was received, false if the queue is ready
  // for destruction.
  bool Next(void** tag);

  // Prepare a tag for the C api
  // Given a tag we'd like to receive from Next, what tag should we pass
  // down to the C api?
  // Usage example:
  //   grpc_call_start_batch(..., cq.PrepareTagForC(tag));
  // Allows attaching some work to be executed before the original tag
  // is returned.
  // MUST be used for all events that could be surfaced through this
  // wrapping API
  template <class F>
  void *PrepareTagForC(void *user_tag, F on_ready) {
    return new std::function<void*()>([user_tag, on_ready]() {
      on_ready();
      return user_tag;
    });
  }

  // Shutdown has to be called, and the CompletionQueue can only be
  // destructed when false is returned from Next().
  void Shutdown();

  grpc_completion_queue* cq() { return cq_; }

 private:
  typedef std::function<void*()> FinishFunc;

  grpc_completion_queue* cq_;  // owned
};

}  // namespace grpc

#endif  // __GRPCPP_COMPLETION_QUEUE_H__
