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

#ifndef GRPCXX_COMPLETION_QUEUE_H
#define GRPCXX_COMPLETION_QUEUE_H

#include <grpc/support/time.h>
#include <grpc++/impl/client_unary_call.h>
#include <grpc++/impl/grpc_library.h>
#include <grpc++/time.h>

struct grpc_completion_queue;

namespace grpc {

template <class R>
class ClientReader;
template <class W>
class ClientWriter;
template <class R, class W>
class ClientReaderWriter;
template <class R>
class ServerReader;
template <class W>
class ServerWriter;
template <class R, class W>
class ServerReaderWriter;

class CompletionQueue;
class Server;
class ServerBuilder;
class ServerContext;

class CompletionQueueTag {
 public:
  virtual ~CompletionQueueTag() {}
  // Called prior to returning from Next(), return value
  // is the status of the operation (return status is the default thing
  // to do)
  // If this function returns false, the tag is dropped and not returned
  // from the completion queue
  virtual bool FinalizeResult(void** tag, bool* status) = 0;
};

// grpc_completion_queue wrapper class
class CompletionQueue : public GrpcLibrary {
 public:
  CompletionQueue();
  explicit CompletionQueue(grpc_completion_queue* take);
  ~CompletionQueue() GRPC_OVERRIDE;

  // Tri-state return for AsyncNext: SHUTDOWN, GOT_EVENT, TIMEOUT
  enum NextStatus { SHUTDOWN, GOT_EVENT, TIMEOUT };

  // Nonblocking (until deadline) read from queue.
  // Cannot rely on result of tag or ok if return is TIMEOUT
  template<typename T>
  NextStatus AsyncNext(void** tag, bool* ok, const T& deadline) {
    TimePoint<T> deadline_tp(deadline);
    return AsyncNextInternal(tag, ok, deadline_tp.raw_time());
  }

  // Blocking read from queue.
  // Returns false if the queue is ready for destruction, true if event

  bool Next(void** tag, bool* ok) {
    return (AsyncNextInternal(tag, ok, gpr_inf_future) != SHUTDOWN);
  }

  // Shutdown has to be called, and the CompletionQueue can only be
  // destructed when false is returned from Next().
  void Shutdown();

  grpc_completion_queue* cq() { return cq_; }

 private:
  // Friend synchronous wrappers so that they can access Pluck(), which is
  // a semi-private API geared towards the synchronous implementation.
  template <class R>
  friend class ::grpc::ClientReader;
  template <class W>
  friend class ::grpc::ClientWriter;
  template <class R, class W>
  friend class ::grpc::ClientReaderWriter;
  template <class R>
  friend class ::grpc::ServerReader;
  template <class W>
  friend class ::grpc::ServerWriter;
  template <class R, class W>
  friend class ::grpc::ServerReaderWriter;
  friend class ::grpc::Server;
  friend class ::grpc::ServerContext;
  friend Status BlockingUnaryCall(ChannelInterface* channel,
                                  const RpcMethod& method,
                                  ClientContext* context,
                                  const grpc::protobuf::Message& request,
                                  grpc::protobuf::Message* result);

  NextStatus AsyncNextInternal(void** tag, bool* ok, gpr_timespec deadline);

  // Wraps grpc_completion_queue_pluck.
  // Cannot be mixed with calls to Next().
  bool Pluck(CompletionQueueTag* tag);

  // Does a single polling pluck on tag
  void TryPluck(CompletionQueueTag* tag);

  grpc_completion_queue* cq_;  // owned
};

class ServerCompletionQueue : public CompletionQueue {
 private:
  friend class ServerBuilder;
  ServerCompletionQueue() {}
};

}  // namespace grpc

#endif  // GRPCXX_COMPLETION_QUEUE_H
