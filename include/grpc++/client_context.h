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

/// A ClientContext allows the person implementing a service client to:
///
/// - Add custom metadata key-value pairs that will propagated to the server
/// side.
/// - Control call settings such as compression and authentication.
/// - Initial and trailing metadata coming from the server.
/// - Get performance metrics (ie, census).
///
/// Context settings are only relevant to the call they are invoked with, that
/// is to say, they aren't sticky. Some of these settings, such as the
/// compression options, can be made persistant at channel construction time
/// (see \a grpc::CreateCustomChannel).
///
/// \warning ClientContext instances should \em not be reused across rpcs.

#ifndef GRPCXX_CLIENT_CONTEXT_H
#define GRPCXX_CLIENT_CONTEXT_H

#include <map>
#include <memory>
#include <string>

#include <grpc++/impl/sync.h>
#include <grpc++/security/auth_context.h>
#include <grpc++/support/config.h>
#include <grpc++/support/status.h>
#include <grpc++/support/string_ref.h>
#include <grpc++/support/time.h>
#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

struct census_context;

namespace grpc {

class Channel;
class CompletionQueue;
class Credentials;
class RpcMethod;
template <class R>
class ClientReader;
template <class W>
class ClientWriter;
template <class R, class W>
class ClientReaderWriter;
template <class R>
class ClientAsyncReader;
template <class W>
class ClientAsyncWriter;
template <class R, class W>
class ClientAsyncReaderWriter;
template <class R>
class ClientAsyncResponseReader;
class ServerContext;

/// Options for \a ClientContext::FromServerContext specifying which traits from
/// the \a ServerContext to propagate (copy) from it into a new \a
/// ClientContext.
///
/// \see ClientContext::FromServerContext
class PropagationOptions {
 public:
  PropagationOptions() : propagate_(GRPC_PROPAGATE_DEFAULTS) {}

  PropagationOptions& enable_deadline_propagation() {
    propagate_ |= GRPC_PROPAGATE_DEADLINE;
    return *this;
  }

  PropagationOptions& disable_deadline_propagation() {
    propagate_ &= ~GRPC_PROPAGATE_DEADLINE;
    return *this;
  }

  PropagationOptions& enable_census_stats_propagation() {
    propagate_ |= GRPC_PROPAGATE_CENSUS_STATS_CONTEXT;
    return *this;
  }

  PropagationOptions& disable_census_stats_propagation() {
    propagate_ &= ~GRPC_PROPAGATE_CENSUS_STATS_CONTEXT;
    return *this;
  }

  PropagationOptions& enable_census_tracing_propagation() {
    propagate_ |= GRPC_PROPAGATE_CENSUS_TRACING_CONTEXT;
    return *this;
  }

  PropagationOptions& disable_census_tracing_propagation() {
    propagate_ &= ~GRPC_PROPAGATE_CENSUS_TRACING_CONTEXT;
    return *this;
  }

  PropagationOptions& enable_cancellation_propagation() {
    propagate_ |= GRPC_PROPAGATE_CANCELLATION;
    return *this;
  }

  PropagationOptions& disable_cancellation_propagation() {
    propagate_ &= ~GRPC_PROPAGATE_CANCELLATION;
    return *this;
  }

  gpr_uint32 c_bitmask() const { return propagate_; }

 private:
  gpr_uint32 propagate_;
};

namespace testing {
class InteropClientContextInspector;
}  // namespace testing

class ClientContext {
 public:
  ClientContext();
  ~ClientContext();

  /// Create a new \a ClientContext as a child of an incoming server call,
  /// according to \a options (\see PropagationOptions).
  ///
  /// \param server_context The source server context to use as the basis for
  /// constructing the client context.
  /// \param options The options controlling what to copy from the \a
  /// server_context.
  ///
  /// \return A newly constructed \a ClientContext instance based on \a
  /// server_context, with traits propagated (copied) according to \a options.
  static std::unique_ptr<ClientContext> FromServerContext(
      const ServerContext& server_context,
      PropagationOptions options = PropagationOptions());

  /// Add the (\a meta_key, \a meta_value) pair to the metadata associated with
  /// a client call. These are made available at the server side by the \a
  /// grpc::ServerContext::client_metadata() method.
  ///
  /// \warning This method should only be called before invoking the rpc.
  ///
  /// \param meta_key The metadata key. If \a meta_value is binary data, it must
  /// end in "-bin".
  /// \param meta_value The metadata value. If its value is binary, it must be
  /// base64-encoding (see https://tools.ietf.org/html/rfc4648#section-4) and \a
  /// meta_key must end in "-bin".
  void AddMetadata(const grpc::string& meta_key,
                   const grpc::string& meta_value);

  /// Return a collection of initial metadata key-value pairs. Note that keys
  /// may happen more than once (ie, a \a std::multimap is returned).
  ///
  /// \warning This method should only be called after initial metadata has been
  /// received. For streaming calls, see \a
  /// ClientReaderInterface::WaitForInitialMetadata().
  ///
  /// \return A multimap of initial metadata key-value pairs from the server.
  const std::multimap<grpc::string_ref, grpc::string_ref>&
  GetServerInitialMetadata() {
    GPR_ASSERT(initial_metadata_received_);
    return recv_initial_metadata_;
  }

  /// Return a collection of trailing metadata key-value pairs. Note that keys
  /// may happen more than once (ie, a \a std::multimap is returned).
  ///
  /// \warning This method is only callable once the stream has finished.
  ///
  /// \return A multimap of metadata trailing key-value pairs from the server.
  const std::multimap<grpc::string_ref, grpc::string_ref>&
  GetServerTrailingMetadata() {
    // TODO(yangg) check finished
    return trailing_metadata_;
  }

  /// Set the deadline for the client call.
  ///
  /// \warning This method should only be called before invoking the rpc.
  ///
  /// \param deadline the deadline for the client call. Units are determined by
  /// the type used.
  template <typename T>
  void set_deadline(const T& deadline) {
    TimePoint<T> deadline_tp(deadline);
    deadline_ = deadline_tp.raw_time();
  }

#ifndef GRPC_CXX0X_NO_CHRONO
  /// Return the deadline for the client call.
  std::chrono::system_clock::time_point deadline() {
    return Timespec2Timepoint(deadline_);
  }
#endif  // !GRPC_CXX0X_NO_CHRONO

  /// Return a \a gpr_timespec representation of the client call's deadline.
  gpr_timespec raw_deadline() { return deadline_; }

  /// Set the per call authority header (see
  /// https://tools.ietf.org/html/rfc7540#section-8.1.2.3).
  void set_authority(const grpc::string& authority) { authority_ = authority; }

  /// Return the authentication context for this client call.
  ///
  /// \see grpc::AuthContext.
  std::shared_ptr<const AuthContext> auth_context() const;

  /// Set credentials for the client call.
  ///
  /// A credentials object encapsulates all the state needed by a client to
  /// authenticate with a server and make various assertions, e.g., about the
  /// client’s identity, role, or whether it is authorized to make a particular
  /// call.
  ///
  /// \see  https://github.com/grpc/grpc/blob/master/doc/grpc-auth-support.md
  void set_credentials(const std::shared_ptr<Credentials>& creds) {
    creds_ = creds;
  }

  /// Return the compression algorithm to be used by the client call.
  grpc_compression_algorithm compression_algorithm() const {
    return compression_algorithm_;
  }

  /// Set \a algorithm to be the compression algorithm used for the client call.
  ///
  /// \param algorith The compression algorithm used for the client call.
  void set_compression_algorithm(grpc_compression_algorithm algorithm);

  /// Return the peer uri in a string.
  ///
  /// \warning This value is never authenticated or subject to any security
  /// related code. It must not be used for any authentication related
  /// functionality. Instead, use auth_context.
  ///
  /// \return The call's peer URI.
  grpc::string peer() const;

  /// Get and set census context
  void set_census_context(struct census_context* ccp) { census_context_ = ccp; }
  struct census_context* census_context() const {
    return census_context_;
  }

  /// Send a best-effort out-of-band cancel. The call could be in any stage.
  /// e.g. if it is already finished, it may still return success.
  ///
  /// There is no guarantee the call will be cancelled.
  void TryCancel();

 private:
  // Disallow copy and assign.
  ClientContext(const ClientContext&);
  ClientContext& operator=(const ClientContext&);

  friend class ::grpc::testing::InteropClientContextInspector;
  friend class CallOpClientRecvStatus;
  friend class CallOpRecvInitialMetadata;
  friend class Channel;
  template <class R>
  friend class ::grpc::ClientReader;
  template <class W>
  friend class ::grpc::ClientWriter;
  template <class R, class W>
  friend class ::grpc::ClientReaderWriter;
  template <class R>
  friend class ::grpc::ClientAsyncReader;
  template <class W>
  friend class ::grpc::ClientAsyncWriter;
  template <class R, class W>
  friend class ::grpc::ClientAsyncReaderWriter;
  template <class R>
  friend class ::grpc::ClientAsyncResponseReader;
  template <class InputMessage, class OutputMessage>
  friend Status BlockingUnaryCall(Channel* channel, const RpcMethod& method,
                                  ClientContext* context,
                                  const InputMessage& request,
                                  OutputMessage* result);

  grpc_call* call() { return call_; }
  void set_call(grpc_call* call, const std::shared_ptr<Channel>& channel);

  grpc::string authority() { return authority_; }

  bool initial_metadata_received_;
  std::shared_ptr<Channel> channel_;
  grpc::mutex mu_;
  grpc_call* call_;
  bool call_canceled_;
  gpr_timespec deadline_;
  grpc::string authority_;
  std::shared_ptr<Credentials> creds_;
  mutable std::shared_ptr<const AuthContext> auth_context_;
  struct census_context* census_context_;
  std::multimap<grpc::string, grpc::string> send_initial_metadata_;
  std::multimap<grpc::string_ref, grpc::string_ref> recv_initial_metadata_;
  std::multimap<grpc::string_ref, grpc::string_ref> trailing_metadata_;

  grpc_call* propagate_from_call_;
  PropagationOptions propagation_options_;

  grpc_compression_algorithm compression_algorithm_;
};

}  // namespace grpc

#endif  // GRPCXX_CLIENT_CONTEXT_H
