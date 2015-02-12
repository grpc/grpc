/*
 *
 * Copyright 2014, Google Inc.
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

#ifndef __GRPCPP_SERVER_CONTEXT_H_
#define __GRPCPP_SERVER_CONTEXT_H_

#include <chrono>
#include <map>

#include "config.h"

struct grpc_metadata;
struct gpr_timespec;

namespace grpc {

template <class R> class ServerAsyncReader;
template <class W> class ServerAsyncWriter;
template <class R, class W> class ServerAsyncReaderWriter;
template <class R> class ServerReader;
template <class W> class ServerWriter;
template <class R, class W> class ServerReaderWriter;

class CallOpBuffer;
class Server;

// Interface of server side rpc context.
class ServerContext {
 public:
  virtual ~ServerContext() {}

  std::chrono::system_clock::time_point absolute_deadline() { return deadline_; }

  void AddInitialMetadata(const grpc::string& key, const grpc::string& value);
  void AddTrailingMetadata(const grpc::string& key, const grpc::string& value);

 private:
  friend class ::grpc::Server;
  template <class R> friend class ::grpc::ServerAsyncReader;
  template <class W> friend class ::grpc::ServerAsyncWriter;
  template <class R, class W> friend class ::grpc::ServerAsyncReaderWriter;
  template <class R> friend class ::grpc::ServerReader;
  template <class W> friend class ::grpc::ServerWriter;
  template <class R, class W> friend class ::grpc::ServerReaderWriter;
  
  ServerContext(gpr_timespec deadline, grpc_metadata *metadata, size_t metadata_count);

  void SendInitialMetadataIfNeeded(CallOpBuffer *buf);

  const std::chrono::system_clock::time_point deadline_;
  bool sent_initial_metadata_ = false;
  std::multimap<grpc::string, grpc::string> client_metadata_;
  std::multimap<grpc::string, grpc::string> initial_metadata_;
  std::multimap<grpc::string, grpc::string> trailing_metadata_;
};

}  // namespace grpc

#endif  // __GRPCPP_SERVER_CONTEXT_H_
