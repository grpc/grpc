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

#ifndef GRPCPP_IMPL_CODEGEN_SERVER_CONTEXT_H
#define GRPCPP_IMPL_CODEGEN_SERVER_CONTEXT_H

#include <map>
#include <memory>
#include <vector>

#include <grpc/impl/codegen/compression_types.h>

#include <grpcpp/impl/codegen/call.h>
#include <grpcpp/impl/codegen/call_op_set.h>
#include <grpcpp/impl/codegen/callback_common.h>
#include <grpcpp/impl/codegen/completion_queue_tag.h>
#include <grpcpp/impl/codegen/config.h>
#include <grpcpp/impl/codegen/create_auth_context.h>
#include <grpcpp/impl/codegen/metadata_map.h>
#include <grpcpp/impl/codegen/security/auth_context.h>
#include <grpcpp/impl/codegen/server_interceptor.h>
#include <grpcpp/impl/codegen/string_ref.h>
#include <grpcpp/impl/codegen/time.h>

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
namespace internal {
template <class W, class R>
class ServerReaderWriterBody;
template <class ServiceType, class RequestType, class ResponseType>
class RpcMethodHandler;
template <class ServiceType, class RequestType, class ResponseType>
class ClientStreamingHandler;
template <class ServiceType, class RequestType, class ResponseType>
class ServerStreamingHandler;
template <class ServiceType, class RequestType, class ResponseType>
class BidiStreamingHandler;
template <class RequestType, class ResponseType>
class CallbackUnaryHandler;
template <class RequestType, class ResponseType>
class CallbackClientStreamingHandler;
template <class RequestType, class ResponseType>
class CallbackServerStreamingHandler;
template <class RequestType, class ResponseType>
class CallbackBidiHandler;
template <class Streamer, bool WriteNeeded>
class TemplatedBidiStreamingHandler;
template <StatusCode code>
class ErrorMethodHandler;
class Call;
class ServerReactor;
}  // namespace internal

class CompletionQueue;
class Server;
class ServerInterface;

namespace testing {
class InteropServerContextInspector;
class ServerContextTestSpouse;
}  // namespace testing

/// A ServerContext allows the person implementing a service handler to:
///
/// - Add custom initial and trailing metadata key-value pairs that will
///   propagated to the client side.
/// - Control call settings such as compression and authentication.
/// - Access metadata coming from the client.
/// - Get performance metrics (ie, census).
///
/// Context settings are only relevant to the call handler they are supplied to,
/// that is to say, they aren't sticky across multiple calls. Some of these
/// settings, such as the compression options, can be made persistent at server
/// construction time by specifying the appropriate \a ChannelArguments
/// to a \a grpc::ServerBuilder, via \a ServerBuilder::AddChannelArgument.
///
/// \warning ServerContext instances should \em not be reused across rpcs.
class ServerContext {
 public:
  ServerContext();  // for async calls
  ~ServerContext();

  /// Return the deadline for the server call.
  std::chrono::system_clock::time_point deadline() const {
    return Timespec2Timepoint(deadline_);
  }

  /// Return a \a gpr_timespec representation of the server call's deadline.
  gpr_timespec raw_deadline() const { return deadline_; }

  /// Add the (\a key, \a value) pair to the initial metadata
  /// associated with a server call. These are made available at the client side
  /// by the \a grpc::ClientContext::GetServerInitialMetadata() method.
  ///
  /// \warning This method should only be called before sending initial metadata
  /// to the client (which can happen explicitly, or implicitly when sending a
  /// a response message or status to the client).
  ///
  /// \param key The metadata key. If \a value is binary data, it must
  /// end in "-bin".
  /// \param value The metadata value. If its value is binary, the key name
  /// must end in "-bin".
  ///
  /// Metadata must conform to the following format:
  /// Custom-Metadata -> Binary-Header / ASCII-Header
  /// Binary-Header -> {Header-Name "-bin" } {binary value}
  /// ASCII-Header -> Header-Name ASCII-Value
  /// Header-Name -> 1*( %x30-39 / %x61-7A / "_" / "-" / ".") ; 0-9 a-z _ - .
  /// ASCII-Value -> 1*( %x20-%x7E ) ; space and printable ASCII
  void AddInitialMetadata(const grpc::string& key, const grpc::string& value);

  /// Add the (\a key, \a value) pair to the initial metadata
  /// associated with a server call. These are made available at the client
  /// side by the \a grpc::ClientContext::GetServerTrailingMetadata() method.
  ///
  /// \warning This method should only be called before sending trailing
  /// metadata to the client (which happens when the call is finished and a
  /// status is sent to the client).
  ///
  /// \param key The metadata key. If \a value is binary data,
  /// it must end in "-bin".
  /// \param value The metadata value. If its value is binary, the key name
  /// must end in "-bin".
  ///
  /// Metadata must conform to the following format:
  /// Custom-Metadata -> Binary-Header / ASCII-Header
  /// Binary-Header -> {Header-Name "-bin" } {binary value}
  /// ASCII-Header -> Header-Name ASCII-Value
  /// Header-Name -> 1*( %x30-39 / %x61-7A / "_" / "-" / ".") ; 0-9 a-z _ - .
  /// ASCII-Value -> 1*( %x20-%x7E ) ; space and printable ASCII
  void AddTrailingMetadata(const grpc::string& key, const grpc::string& value);

  /// IsCancelled is always safe to call when using sync or callback API.
  /// When using async API, it is only safe to call IsCancelled after
  /// the AsyncNotifyWhenDone tag has been delivered.
  bool IsCancelled() const;

  /// Cancel the Call from the server. This is a best-effort API and
  /// depending on when it is called, the RPC may still appear successful to
  /// the client.
  /// For example, if TryCancel() is called on a separate thread, it might race
  /// with the server handler which might return success to the client before
  /// TryCancel() was even started by the thread.
  ///
  /// It is the caller's responsibility to prevent such races and ensure that if
  /// TryCancel() is called, the serverhandler must return Status::CANCELLED.
  /// The only exception is that if the serverhandler is already returning an
  /// error status code, it is ok to not return Status::CANCELLED even if
  /// TryCancel() was called.
  ///
  /// Note that TryCancel() does not change any of the tags that are pending
  /// on the completion queue. All pending tags will still be delivered
  /// (though their ok result may reflect the effect of cancellation).
  void TryCancel() const;

  /// Return a collection of initial metadata key-value pairs sent from the
  /// client. Note that keys may happen more than
  /// once (ie, a \a std::multimap is returned).
  ///
  /// It is safe to use this method after initial metadata has been received,
  /// Calls always begin with the client sending initial metadata, so this is
  /// safe to access as soon as the call has begun on the server side.
  ///
  /// \return A multimap of initial metadata key-value pairs from the server.
  const std::multimap<grpc::string_ref, grpc::string_ref>& client_metadata()
      const {
    return *client_metadata_.map();
  }

  /// Return the compression algorithm to be used by the server call.
  grpc_compression_level compression_level() const {
    return compression_level_;
  }

  /// Set \a level to be the compression level used for the server call.
  ///
  /// \param level The compression level used for the server call.
  void set_compression_level(grpc_compression_level level) {
    compression_level_set_ = true;
    compression_level_ = level;
  }

  /// Return a bool indicating whether the compression level for this call
  /// has been set (either implicitly or through a previous call to
  /// \a set_compression_level.
  bool compression_level_set() const { return compression_level_set_; }

  /// Return the compression algorithm the server call will request be used.
  /// Note that the gRPC runtime may decide to ignore this request, for example,
  /// due to resource constraints, or if the server is aware the client doesn't
  /// support the requested algorithm.
  grpc_compression_algorithm compression_algorithm() const {
    return compression_algorithm_;
  }
  /// Set \a algorithm to be the compression algorithm used for the server call.
  ///
  /// \param algorithm The compression algorithm used for the server call.
  void set_compression_algorithm(grpc_compression_algorithm algorithm);

  /// Set the serialized load reporting costs in \a cost_data for the call.
  void SetLoadReportingCosts(const std::vector<grpc::string>& cost_data);

  /// Return the authentication context for this server call.
  ///
  /// \see grpc::AuthContext.
  std::shared_ptr<const AuthContext> auth_context() const {
    if (auth_context_.get() == nullptr) {
      auth_context_ = CreateAuthContext(call_);
    }
    return auth_context_;
  }

  /// Return the peer uri in a string.
  /// WARNING: this value is never authenticated or subject to any security
  /// related code. It must not be used for any authentication related
  /// functionality. Instead, use auth_context.
  grpc::string peer() const;

  /// Get the census context associated with this server call.
  const struct census_context* census_context() const;

  /// Async only. Has to be called before the rpc starts.
  /// Returns the tag in completion queue when the rpc finishes.
  /// IsCancelled() can then be called to check whether the rpc was cancelled.
  /// TODO(vjpai): Fix this so that the tag is returned even if the call never
  /// starts (https://github.com/grpc/grpc/issues/10136).
  void AsyncNotifyWhenDone(void* tag) {
    has_notify_when_done_tag_ = true;
    async_notify_when_done_tag_ = tag;
  }

  /// Should be used for framework-level extensions only.
  /// Applications never need to call this method.
  grpc_call* c_call() { return call_; }

 private:
  friend class ::grpc::testing::InteropServerContextInspector;
  friend class ::grpc::testing::ServerContextTestSpouse;
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
  friend class ::grpc::internal::ServerReaderWriterBody;
  template <class ServiceType, class RequestType, class ResponseType>
  friend class ::grpc::internal::RpcMethodHandler;
  template <class ServiceType, class RequestType, class ResponseType>
  friend class ::grpc::internal::ClientStreamingHandler;
  template <class ServiceType, class RequestType, class ResponseType>
  friend class ::grpc::internal::ServerStreamingHandler;
  template <class Streamer, bool WriteNeeded>
  friend class ::grpc::internal::TemplatedBidiStreamingHandler;
  template <class RequestType, class ResponseType>
  friend class ::grpc::internal::CallbackUnaryHandler;
  template <class RequestType, class ResponseType>
  friend class ::grpc::internal::CallbackClientStreamingHandler;
  template <class RequestType, class ResponseType>
  friend class ::grpc::internal::CallbackServerStreamingHandler;
  template <class RequestType, class ResponseType>
  friend class ::grpc::internal::CallbackBidiHandler;
  template <StatusCode code>
  friend class internal::ErrorMethodHandler;
  friend class ::grpc::ClientContext;

  /// Prevent copying.
  ServerContext(const ServerContext&);
  ServerContext& operator=(const ServerContext&);

  class CompletionOp;

  void BeginCompletionOp(internal::Call* call,
                         std::function<void(bool)> callback,
                         internal::ServerReactor* reactor);
  /// Return the tag queued by BeginCompletionOp()
  internal::CompletionQueueTag* GetCompletionOpTag();

  ServerContext(gpr_timespec deadline, grpc_metadata_array* arr);

  void set_call(grpc_call* call) { call_ = call; }

  void BindDeadlineAndMetadata(gpr_timespec deadline, grpc_metadata_array* arr);

  void Clear();

  void Setup(gpr_timespec deadline);

  uint32_t initial_metadata_flags() const { return 0; }

  experimental::ServerRpcInfo* set_server_rpc_info(
      const char* method, internal::RpcMethod::RpcType type,
      const std::vector<
          std::unique_ptr<experimental::ServerInterceptorFactoryInterface>>&
          creators) {
    if (creators.size() != 0) {
      rpc_info_ = new experimental::ServerRpcInfo(this, method, type);
      rpc_info_->RegisterInterceptors(creators);
    }
    return rpc_info_;
  }

  CompletionOp* completion_op_;
  bool has_notify_when_done_tag_;
  void* async_notify_when_done_tag_;
  internal::CallbackWithSuccessTag completion_tag_;

  gpr_timespec deadline_;
  grpc_call* call_;
  CompletionQueue* cq_;
  bool sent_initial_metadata_;
  mutable std::shared_ptr<const AuthContext> auth_context_;
  mutable internal::MetadataMap client_metadata_;
  std::multimap<grpc::string, grpc::string> initial_metadata_;
  std::multimap<grpc::string, grpc::string> trailing_metadata_;

  bool compression_level_set_;
  grpc_compression_level compression_level_;
  grpc_compression_algorithm compression_algorithm_;

  internal::CallOpSet<internal::CallOpSendInitialMetadata,
                      internal::CallOpSendMessage>
      pending_ops_;
  bool has_pending_ops_;

  experimental::ServerRpcInfo* rpc_info_;
};

}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_SERVER_CONTEXT_H
