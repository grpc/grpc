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

#ifndef __GRPCPP_INTERNAL_STREAM_STREAM_CONTEXT_H__
#define __GRPCPP_INTERNAL_STREAM_STREAM_CONTEXT_H__

#include <grpc/grpc.h>
#include <grpc++/status.h>
#include <grpc++/stream_context_interface.h>

namespace google {
namespace protobuf {
class Message;
}
}

namespace grpc {
class ClientContext;
class RpcMethod;

class StreamContext : public StreamContextInterface {
 public:
  StreamContext(const RpcMethod& method, ClientContext* context,
                const google::protobuf::Message* request,
                google::protobuf::Message* result);
  StreamContext(const RpcMethod& method, grpc_call* call,
                grpc_completion_queue* cq, google::protobuf::Message* request,
                google::protobuf::Message* result);
  ~StreamContext();
  // Start the stream, if there is a final write following immediately, set
  // buffered so that the messages can be sent in batch.
  void Start(bool buffered) override;
  bool Read(google::protobuf::Message* msg) override;
  bool Write(const google::protobuf::Message* msg, bool is_last) override;
  const Status& Wait() override;
  void FinishStream(const Status& status, bool send) override;

  google::protobuf::Message* request() override { return request_; }
  google::protobuf::Message* response() override { return result_; }

 private:
  // Unique tags for plucking events from the c layer. this pointer is casted
  // to char* to create single byte step between tags. It implicitly relies on
  // that StreamContext is large enough to contain all the pointers.
  void* finished_tag() { return reinterpret_cast<char*>(this); }
  void* read_tag() { return reinterpret_cast<char*>(this) + 1; }
  void* write_tag() { return reinterpret_cast<char*>(this) + 2; }
  void* halfclose_tag() { return reinterpret_cast<char*>(this) + 3; }
  void* client_metadata_read_tag() { return reinterpret_cast<char*>(this) + 5; }
  grpc_call* call() { return call_; }
  grpc_completion_queue* cq() { return cq_; }

  bool is_client_;
  const RpcMethod* method_;             // not owned
  grpc_call* call_;                     // not owned
  grpc_completion_queue* cq_;           // not owned
  google::protobuf::Message* request_;  // first request, not owned
  google::protobuf::Message* result_;   // last response, not owned

  bool peer_halfclosed_;
  bool self_halfclosed_;
  Status final_status_;
};

}  // namespace grpc

#endif  // __GRPCPP_INTERNAL_STREAM_STREAM_CONTEXT_H__
