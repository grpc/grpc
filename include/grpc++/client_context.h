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

#ifndef GRPCXX_CLIENT_CONTEXT_H
#define GRPCXX_CLIENT_CONTEXT_H

#include <map>
#include <memory>
#include <string>

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpc++/support/auth_context.h>
#include <grpc++/support/config.h>
#include <grpc++/support/status.h>
#include <grpc++/support/time.h>

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

  /// Create a new ClientContext that propagates some or all of its attributes
  static std::unique_ptr<ClientContext> FromServerContext(
      const ServerContext& server_context,
      PropagationOptions options = PropagationOptions());

  void AddMetadata(const grpc::string& meta_key,
                   const grpc::string& meta_value);

  const std::multimap<grpc::string, grpc::string>& GetServerInitialMetadata() {
    GPR_ASSERT(initial_metadata_received_);
    return recv_initial_metadata_;
  }

  const std::multimap<grpc::string, grpc::string>& GetServerTrailingMetadata() {
    // TODO(yangg) check finished
    return trailing_metadata_;
  }

  template <typename T>
  void set_deadline(const T& deadline) {
    TimePoint<T> deadline_tp(deadline);
    deadline_ = deadline_tp.raw_time();
  }

#ifndef GRPC_CXX0X_NO_CHRONO
  std::chrono::system_clock::time_point deadline() {
    return Timespec2Timepoint(deadline_);
  }
#endif  // !GRPC_CXX0X_NO_CHRONO

  gpr_timespec raw_deadline() { return deadline_; }

  void set_authority(const grpc::string& authority) { authority_ = authority; }

  // Set credentials for the rpc.
  void set_credentials(const std::shared_ptr<Credentials>& creds) {
    creds_ = creds;
  }

  grpc_compression_algorithm compression_algorithm() const {
    return compression_algorithm_;
  }

  void set_compression_algorithm(grpc_compression_algorithm algorithm);

  std::shared_ptr<const AuthContext> auth_context() const;

  // Return the peer uri in a string.
  // WARNING: this value is never authenticated or subject to any security
  // related code. It must not be used for any authentication related
  // functionality. Instead, use auth_context.
  grpc::string peer() const;

  // Get and set census context
  void set_census_context(struct census_context* ccp) { census_context_ = ccp; }
  struct census_context* census_context() const {
    return census_context_;
  }

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
  grpc_call* call_;
  gpr_timespec deadline_;
  grpc::string authority_;
  std::shared_ptr<Credentials> creds_;
  mutable std::shared_ptr<const AuthContext> auth_context_;
  struct census_context* census_context_;
  std::multimap<grpc::string, grpc::string> send_initial_metadata_;
  std::multimap<grpc::string, grpc::string> recv_initial_metadata_;
  std::multimap<grpc::string, grpc::string> trailing_metadata_;

  grpc_call* propagate_from_call_;
  PropagationOptions propagation_options_;

  grpc_compression_algorithm compression_algorithm_;
};

}  // namespace grpc

#endif  // GRPCXX_CLIENT_CONTEXT_H
