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

#ifndef GRPCXX_ASYNC_GENERIC_SERVICE_H
#define GRPCXX_ASYNC_GENERIC_SERVICE_H

#include <grpc++/byte_buffer.h>
#include <grpc++/stream.h>

struct grpc_server;

namespace grpc {

typedef ServerAsyncReaderWriter<ByteBuffer, ByteBuffer>
    GenericServerAsyncReaderWriter;

class GenericServerContext GRPC_FINAL : public ServerContext {
 public:
  const grpc::string& method() const { return method_; }
  const grpc::string& host() const { return host_; }

 private:
  friend class Server;

  grpc::string method_;
  grpc::string host_;
};

class AsyncGenericService GRPC_FINAL {
 public:
  // TODO(yangg) Once we can add multiple completion queues to the server
  // in c core, add a CompletionQueue* argument to the ctor here.
  // TODO(yangg) support methods list.
  AsyncGenericService(const grpc::string& methods) : server_(nullptr) {}

  void RequestCall(GenericServerContext* ctx,
                   GenericServerAsyncReaderWriter* reader_writer,
                   CompletionQueue* call_cq,
                   ServerCompletionQueue* notification_cq, void* tag);

 private:
  friend class Server;
  Server* server_;
};

}  // namespace grpc

#endif  // GRPCXX_ASYNC_GENERIC_SERVICE_H
