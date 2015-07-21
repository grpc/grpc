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
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpc++/auth_context.h>
#include <grpc++/config.h>
#include <grpc++/status.h>
#include <grpc++/time.h>

struct grpc_call;
struct grpc_completion_queue;
struct census_context;

namespace grpc {

class ChannelInterface;
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

namespace testing {
class InteropClientContextInspector;
}  // namespace testing

class ClientContext {
 public:
  ClientContext();
  ~ClientContext();

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

  grpc_compression_algorithm get_compression_algorithm() const {
    return compression_algorithm_;
  }

  void set_compression_algorithm(grpc_compression_algorithm algorithm);

  std::shared_ptr<const AuthContext> auth_context() const;

  // Get and set census context
  void set_census_context(census_context* ccp) { census_context_ = ccp; }
  census_context* get_census_context() const { return census_context_; }

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
  friend Status BlockingUnaryCall(ChannelInterface* channel,
                                  const RpcMethod& method,
                                  ClientContext* context,
                                  const InputMessage& request,
                                  OutputMessage* result);

  grpc_call* call() { return call_; }
  void set_call(grpc_call* call,
                const std::shared_ptr<ChannelInterface>& channel);

  grpc_completion_queue* cq() { return cq_; }
  void set_cq(grpc_completion_queue* cq) { cq_ = cq; }

  grpc::string authority() { return authority_; }

  bool initial_metadata_received_;
  std::shared_ptr<ChannelInterface> channel_;
  grpc_call* call_;
  grpc_completion_queue* cq_;
  gpr_timespec deadline_;
  grpc::string authority_;
  std::shared_ptr<Credentials> creds_;
  mutable std::shared_ptr<const AuthContext> auth_context_;
  census_context* census_context_;
  std::multimap<grpc::string, grpc::string> send_initial_metadata_;
  std::multimap<grpc::string, grpc::string> recv_initial_metadata_;
  std::multimap<grpc::string, grpc::string> trailing_metadata_;

  grpc_compression_algorithm compression_algorithm_;
};

}  // namespace grpc

#endif  // GRPCXX_CLIENT_CONTEXT_H
