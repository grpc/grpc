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

#ifndef __GRPCPP_ASYNC_SERVER_CONTEXT_H__
#define __GRPCPP_ASYNC_SERVER_CONTEXT_H__

#include <chrono>

#include <grpc++/config.h>

struct grpc_byte_buffer;
struct grpc_call;
struct grpc_completion_queue;

namespace google {
namespace protobuf {
class Message;
}
}

using std::chrono::system_clock;

namespace grpc {
class Status;

// TODO(rocking): wrap grpc c structures.
class AsyncServerContext {
 public:
  AsyncServerContext(grpc_call* call, const grpc::string& method,
                     const grpc::string& host,
                     system_clock::time_point absolute_deadline);
  ~AsyncServerContext();

  // Accept this rpc, bind it to a completion queue.
  void Accept(grpc_completion_queue* cq);

  // Read and write calls, all async. Return true for success.
  bool StartRead(google::protobuf::Message* request);
  bool StartWrite(const google::protobuf::Message& response, int flags);
  bool StartWriteStatus(const Status& status);

  bool ParseRead(grpc_byte_buffer* read_buffer);

  grpc::string method() const { return method_; }
  grpc::string host() const { return host_; }
  system_clock::time_point absolute_deadline() { return absolute_deadline_; }

  grpc_call* call() { return call_; }

 private:
  AsyncServerContext(const AsyncServerContext&);
  AsyncServerContext& operator=(const AsyncServerContext&);

  // These properties may be moved to a ServerContext class.
  const grpc::string method_;
  const grpc::string host_;
  system_clock::time_point absolute_deadline_;

  google::protobuf::Message* request_;  // not owned
  grpc_call* call_;           // owned
};

}  // namespace grpc

#endif  // __GRPCPP_ASYNC_SERVER_CONTEXT_H__
