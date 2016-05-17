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

#ifndef GRPCXX_IMPL_CODEGEN_SERVER_CONTEXT_H
#define GRPCXX_IMPL_CODEGEN_SERVER_CONTEXT_H

#include <map>
#include <memory>

#include <grpc++/impl/codegen/config.h>
#include <grpc++/impl/codegen/create_auth_context.h>
#include <grpc++/impl/codegen/security/auth_context.h>
#include <grpc++/impl/codegen/string_ref.h>
#include <grpc++/impl/codegen/time.h>
#include <grpc/impl/codegen/compression_types.h>
#include <grpc/impl/codegen/time.h>

struct gpr_timespec;
struct grpc_metadata;
struct grpc_call;
struct census_context;

namespace grpc {

class ClientContext;
template <class W, class R>
class ServerAsyncReader;
template <class W>
class ServerAsyncWriter;
template <class W>
class ServerAsyncResponseWriter;
template <class W, class R>
class ServerAsyncReaderWriter;
template <class R>
class ServerReader;
template <class W>
class ServerWriter;
template <class W, class R>
class ServerReaderWriter;
template <class ServiceType, class RequestType, class ResponseType>
class RpcMethodHandler;
template <class ServiceType, class RequestType, class ResponseType>
class ClientStreamingHandler;
template <class ServiceType, class RequestType, class ResponseType>
class ServerStreamingHandler;
template <class ServiceType, class RequestType, class ResponseType>
class BidiStreamingHandler;
class UnknownMethodHandler;

class Call;
class CallOpBuffer;
class CompletionQueue;
class Server;
class ServerInterface;

namespace testing {
class InteropServerContextInspector;
}  // namespace testing

// Interface of server side rpc context.
class ServerContext {
 public:
  ServerContext();  // for async calls
  ~ServerContext();

#ifndef GRPC_CXX0X_NO_CHRONO
  std::chrono::system_clock::time_point deadline() {
    return Timespec2Timepoint(deadline_);
  }
#endif  // !GRPC_CXX0X_NO_CHRONO

  gpr_timespec raw_deadline() { return deadline_; }

  void AddInitialMetadata(const grpc::string& key, const grpc::string& value);
  void AddTrailingMetadata(const grpc::string& key, const grpc::string& value);

  // IsCancelled is always safe to call when using sync API
  // When using async API, it is only safe to call IsCancelled after
  // the AsyncNotifyWhenDone tag has been delivered
  bool IsCancelled() const;

  // Cancel the Call from the server. This is a best-effort API and depending on
  // when it is called, the RPC may still appear successful to the client.
  // For example, if TryCancel() is called on a separate thread, it might race
  // with the server handler which might return success to the client before
  // TryCancel() was even started by the thread.
  //
  // It is the caller's responsibility to prevent such races and ensure that if
  // TryCancel() is called, the serverhandler must return Status::CANCELLED. The
  // only exception is that if the serverhandler is already returning an error
  // status code, it is ok to not return Status::CANCELLED even if TryCancel()
  // was called.
  void TryCancel() const;

  const std::multimap<grpc::string_ref, grpc::string_ref>& client_metadata() {
    return client_metadata_;
  }

  grpc_compression_level compression_level() const {
    return compression_level_;
  }
  void set_compression_level(grpc_compression_level level);

  grpc_compression_algorithm compression_algorithm() const {
    return compression_algorithm_;
  }
  void set_compression_algorithm(grpc_compression_algorithm algorithm);

  std::shared_ptr<const AuthContext> auth_context() const {
    if (auth_context_.get() == nullptr) {
      auth_context_ = CreateAuthContext(call_);
    }
    return auth_context_;
  }

  // Return the peer uri in a string.
  // WARNING: this value is never authenticated or subject to any security
  // related code. It must not be used for any authentication related
  // functionality. Instead, use auth_context.
  grpc::string peer() const;

  const struct census_context* census_context() const;

  // Async only. Has to be called before the rpc starts.
  // Returns the tag in completion queue when the rpc finishes.
  // IsCancelled() can then be called to check whether the rpc was cancelled.
  void AsyncNotifyWhenDone(void* tag) {
    has_notify_when_done_tag_ = true;
    async_notify_when_done_tag_ = tag;
  }

 private:
  friend class ::grpc::testing::InteropServerContextInspector;
  friend class ::grpc::ServerInterface;
  friend class ::grpc::Server;
  template <class W, class R>
  friend class ::grpc::ServerAsyncReader;
  template <class W>
  friend class ::grpc::ServerAsyncWriter;
  template <class W>
  friend class ::grpc::ServerAsyncResponseWriter;
  template <class W, class R>
  friend class ::grpc::ServerAsyncReaderWriter;
  template <class R>
  friend class ::grpc::ServerReader;
  template <class W>
  friend class ::grpc::ServerWriter;
  template <class W, class R>
  friend class ::grpc::ServerReaderWriter;
  template <class ServiceType, class RequestType, class ResponseType>
  friend class RpcMethodHandler;
  template <class ServiceType, class RequestType, class ResponseType>
  friend class ClientStreamingHandler;
  template <class ServiceType, class RequestType, class ResponseType>
  friend class ServerStreamingHandler;
  template <class ServiceType, class RequestType, class ResponseType>
  friend class BidiStreamingHandler;
  friend class UnknownMethodHandler;
  friend class ::grpc::ClientContext;

  // Prevent copying.
  ServerContext(const ServerContext&);
  ServerContext& operator=(const ServerContext&);

  class CompletionOp;

  void BeginCompletionOp(Call* call);

  ServerContext(gpr_timespec deadline, grpc_metadata* metadata,
                size_t metadata_count);

  void set_call(grpc_call* call) { call_ = call; }

  uint32_t initial_metadata_flags() const { return 0; }

  CompletionOp* completion_op_;
  bool has_notify_when_done_tag_;
  void* async_notify_when_done_tag_;

  gpr_timespec deadline_;
  grpc_call* call_;
  CompletionQueue* cq_;
  bool sent_initial_metadata_;
  mutable std::shared_ptr<const AuthContext> auth_context_;
  std::multimap<grpc::string_ref, grpc::string_ref> client_metadata_;
  std::multimap<grpc::string, grpc::string> initial_metadata_;
  std::multimap<grpc::string, grpc::string> trailing_metadata_;

  grpc_compression_level compression_level_;
  grpc_compression_algorithm compression_algorithm_;
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_SERVER_CONTEXT_H
