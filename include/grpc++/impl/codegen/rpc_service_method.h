/*
 *
 * Copyright 2016, Google Inc.
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

#ifndef GRPCXX_IMPL_CODEGEN_RPC_SERVICE_METHOD_H
#define GRPCXX_IMPL_CODEGEN_RPC_SERVICE_METHOD_H

#include <climits>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <grpc++/impl/codegen/config.h>
#include <grpc++/impl/codegen/rpc_method.h>
#include <grpc++/impl/codegen/status.h>

extern "C" {
struct grpc_byte_buffer;
}

namespace grpc {
class ServerContext;
class StreamContextInterface;

// Base class for running an RPC handler.
class MethodHandler {
 public:
  virtual ~MethodHandler() {}
  struct HandlerParameter {
    HandlerParameter(Call* c, ServerContext* context, grpc_byte_buffer* req)
        : call(c), server_context(context), request(req) {}
    Call* call;
    ServerContext* server_context;
    // Handler required to grpc_byte_buffer_destroy this
    grpc_byte_buffer* request;
  };
  virtual void RunHandler(const HandlerParameter& param) = 0;
};

// Server side rpc method class
class RpcServiceMethod : public RpcMethod {
 public:
  // Takes ownership of the handler
  RpcServiceMethod(const char* name, RpcMethod::RpcType type,
                   MethodHandler* handler)
      : RpcMethod(name, type), server_tag_(nullptr), handler_(handler) {}

  void set_server_tag(void* tag) { server_tag_ = tag; }
  void* server_tag() const { return server_tag_; }
  // if MethodHandler is nullptr, then this is an async method
  MethodHandler* handler() const { return handler_.get(); }
  void ResetHandler() { handler_.reset(); }
  void SetHandler(MethodHandler* handler) { handler_.reset(handler); }

 private:
  void* server_tag_;
  std::unique_ptr<MethodHandler> handler_;
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_RPC_SERVICE_METHOD_H
