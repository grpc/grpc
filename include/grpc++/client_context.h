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

#include <chrono>
#include <map>
#include <string>

#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpc++/config.h>

using std::chrono::system_clock;

struct grpc_call;
struct grpc_completion_queue;

namespace google {
namespace protobuf {
class Message;
}  // namespace protobuf
}  // namespace google

namespace grpc {

class CallOpBuffer;
class ChannelInterface;
class CompletionQueue;
class RpcMethod;
class Status;
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

class ClientContext {
 public:
  ClientContext();
  ~ClientContext();

  void AddMetadata(const grpc::string &meta_key,
                   const grpc::string &meta_value);

  const std::multimap<grpc::string, grpc::string>& GetServerInitialMetadata() {
    GPR_ASSERT(initial_metadata_received_);
    return recv_initial_metadata_;
  }

  const std::multimap<grpc::string, grpc::string>& GetServerTrailingMetadata() {
    // TODO(yangg) check finished
    return trailing_metadata_;
  }

  void set_absolute_deadline(const system_clock::time_point &deadline);
  system_clock::time_point absolute_deadline();

  void set_authority(const grpc::string& authority) {
    authority_ = authority;
  }

  void TryCancel();

 private:
  // Disallow copy and assign.
  ClientContext(const ClientContext &);
  ClientContext &operator=(const ClientContext &);

  friend class CallOpBuffer;
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

  grpc_call *call() { return call_; }
  void set_call(grpc_call *call) {
    GPR_ASSERT(call_ == nullptr);
    call_ = call;
  }

  grpc_completion_queue *cq() { return cq_; }
  void set_cq(grpc_completion_queue *cq) { cq_ = cq; }

  gpr_timespec RawDeadline() { return absolute_deadline_; }

  grpc::string authority() {
    return authority_;
  }

  bool initial_metadata_received_;
  grpc_call *call_;
  grpc_completion_queue *cq_;
  gpr_timespec absolute_deadline_;
  grpc::string authority_;
  std::multimap<grpc::string, grpc::string> send_initial_metadata_;
  std::multimap<grpc::string, grpc::string> recv_initial_metadata_;
  std::multimap<grpc::string, grpc::string> trailing_metadata_;
};

}  // namespace grpc

#endif  // GRPCXX_CLIENT_CONTEXT_H
