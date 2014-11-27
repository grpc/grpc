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

struct grpc_completion_queue;

namespace grpc {

// grpc_completion_queue wrapper class
class CompletionQueue {
 public:
  CompletionQueue();
  ~CompletionQueue();

  enum CompletionType {
    QUEUE_CLOSED = 0,       // Shutting down.
    RPC_END = 1,            // An RPC finished. Either at client or server.
    CLIENT_READ_OK = 2,     // A client-side read has finished successfully.
    CLIENT_READ_ERROR = 3,  // A client-side read has finished with error.
    CLIENT_WRITE_OK = 4,
    CLIENT_WRITE_ERROR = 5,
    SERVER_RPC_NEW = 6,     // A new RPC just arrived at the server.
    SERVER_READ_OK = 7,     // A server-side read has finished successfully.
    SERVER_READ_ERROR = 8,  // A server-side read has finished with error.
    SERVER_WRITE_OK = 9,
    SERVER_WRITE_ERROR = 10,
    // Client or server has sent half close successfully.
    HALFCLOSE_OK = 11,
    // New CompletionTypes may be added in the future, so user code should
    // always
    // handle the default case of a CompletionType that appears after such code
    // was
    // written.
    DO_NOT_USE = 20,
  };

  // Blocking read from queue.
  // For QUEUE_CLOSED, *tag is not changed.
  // For SERVER_RPC_NEW, *tag will be a newly allocated AsyncServerContext.
  // For others, *tag will be the AsyncServerContext of this rpc.
  CompletionType Next(void** tag);

  // Shutdown has to be called, and the CompletionQueue can only be
  // destructed when the QUEUE_CLOSED message has been read with Next().
  void Shutdown();

  grpc_completion_queue* cq() { return cq_; }

 private:
  grpc_completion_queue* cq_;  // owned
};

}  // namespace grpc

#endif  // __GRPCPP_COMPLETION_QUEUE_H__
