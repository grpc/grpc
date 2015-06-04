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

#ifndef GRPCXX_SERVER_CONTEXT_H
#define GRPCXX_SERVER_CONTEXT_H

#include <map>

#include <grpc/support/time.h>
#include <grpc++/config.h>
#include <grpc++/time.h>

struct gpr_timespec;
struct grpc_metadata;
struct grpc_call;

namespace grpc {

template <class W, class R>
class ServerAsyncReader;
template <class W>
class ServerAsyncWriter;
template <class W>
class ServerAsyncResponseWriter;
template <class R, class W>
class ServerAsyncReaderWriter;
template <class R>
class ServerReader;
template <class W>
class ServerWriter;
template <class R, class W>
class ServerReaderWriter;

class Call;
class CallOpBuffer;
class CompletionQueue;
class Server;

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

  bool IsCancelled();

  const std::multimap<grpc::string, grpc::string>& client_metadata() {
    return client_metadata_;
  }

 private:
  friend class ::grpc::Server;
  template <class W, class R>
  friend class ::grpc::ServerAsyncReader;
  template <class W>
  friend class ::grpc::ServerAsyncWriter;
  template <class W>
  friend class ::grpc::ServerAsyncResponseWriter;
  template <class R, class W>
  friend class ::grpc::ServerAsyncReaderWriter;
  template <class R>
  friend class ::grpc::ServerReader;
  template <class W>
  friend class ::grpc::ServerWriter;
  template <class R, class W>
  friend class ::grpc::ServerReaderWriter;

  // Prevent copying.
  ServerContext(const ServerContext&);
  ServerContext& operator=(const ServerContext&);

  class CompletionOp;

  void BeginCompletionOp(Call* call);

  ServerContext(gpr_timespec deadline, grpc_metadata* metadata,
                size_t metadata_count);

  CompletionOp* completion_op_;

  gpr_timespec deadline_;
  grpc_call* call_;
  CompletionQueue* cq_;
  bool sent_initial_metadata_;
  std::multimap<grpc::string, grpc::string> client_metadata_;
  std::multimap<grpc::string, grpc::string> initial_metadata_;
  std::multimap<grpc::string, grpc::string> trailing_metadata_;
};

}  // namespace grpc

#endif  // GRPCXX_SERVER_CONTEXT_H
