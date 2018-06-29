/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPCPP_IMPL_CODEGEN_RPC_SERVICE_METHOD_H
#define GRPCPP_IMPL_CODEGEN_RPC_SERVICE_METHOD_H

#include <climits>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <grpc/impl/codegen/log.h>
#include <grpcpp/impl/codegen/byte_buffer.h>
#include <grpcpp/impl/codegen/config.h>
#include <grpcpp/impl/codegen/rpc_method.h>
#include <grpcpp/impl/codegen/status.h>

namespace grpc {
class ServerContext;

namespace internal {
/// Base class for running an RPC handler.
class MethodHandler {
 public:
  virtual ~MethodHandler() {}
  struct HandlerParameter {
    HandlerParameter(Call* c, ServerContext* context, grpc_byte_buffer* req)
        : call(c), server_context(context) {
      request.set_buffer(req);
    }
    ~HandlerParameter() { request.Release(); }
    Call* call;
    ServerContext* server_context;
    // Handler required to destroy these contents
    ByteBuffer request;
  };
  virtual void RunHandler(const HandlerParameter& param) = 0;
};

/// Server side rpc method class
class RpcServiceMethod : public RpcMethod {
 public:
  /// Takes ownership of the handler
  RpcServiceMethod(const char* name, RpcMethod::RpcType type,
                   MethodHandler* handler)
      : RpcMethod(name, type),
        server_tag_(nullptr),
        async_type_(AsyncType::UNSET),
        handler_(handler) {}

  enum class AsyncType {
    UNSET,
    ASYNC,
    RAW,
  };

  void set_server_tag(void* tag) { server_tag_ = tag; }
  void* server_tag() const { return server_tag_; }
  /// if MethodHandler is nullptr, then this is an async method
  MethodHandler* handler() const { return handler_.get(); }
  void SetHandler(MethodHandler* handler) { handler_.reset(handler); }
  void SetServerAsyncType(RpcServiceMethod::AsyncType type) {
    if (async_type_ == AsyncType::UNSET) {
      // this marks this method as async
      handler_.reset();
    } else {
      // this is not an error condition, as it allows users to declare a server
      // like WithRawMethod_foo<AsyncService>. However since it
      // overwrites behavior, it should be logged.
      gpr_log(
          GPR_INFO,
          "You are marking method %s as '%s', even though it was "
          "previously marked '%s'. This behavior will overwrite the original "
          "behavior. If you expected this then ignore this message.",
          name(), TypeToString(async_type_), TypeToString(type));
    }
    async_type_ = type;
  }

 private:
  void* server_tag_;
  AsyncType async_type_;
  std::unique_ptr<MethodHandler> handler_;

  const char* TypeToString(RpcServiceMethod::AsyncType type) {
    switch (type) {
      case AsyncType::UNSET:
        return "unset";
      case AsyncType::ASYNC:
        return "async";
      case AsyncType::RAW:
        return "raw";
      default:
        GPR_UNREACHABLE_CODE(return "unknown");
    }
  }
};
}  // namespace internal

}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_RPC_SERVICE_METHOD_H
