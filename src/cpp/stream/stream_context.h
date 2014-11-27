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

#include <condition_variable>
#include <mutex>
#include <thread>

#include <grpc++/status.h>
#include <grpc++/stream_context_interface.h>

namespace google {
namespace protobuf {
class Message;
}
}

struct grpc_event;

namespace grpc {
class ClientContext;
class RpcMethod;

class StreamContext : public StreamContextInterface {
 public:
  StreamContext(const RpcMethod& method, ClientContext* context,
                const google::protobuf::Message* request, google::protobuf::Message* result);
  ~StreamContext();
  // Start the stream, if there is a final write following immediately, set
  // buffered so that the messages can be sent in batch.
  void Start(bool buffered) override;
  bool Read(google::protobuf::Message* msg) override;
  bool Write(const google::protobuf::Message* msg, bool is_last) override;
  const Status& Wait() override;
  void FinishStream(const Status& status, bool send) override;

  const google::protobuf::Message* request() override { return request_; }
  google::protobuf::Message* response() override { return result_; }

 private:
  void PollingLoop();
  bool BlockingStart();
  bool is_client_;
  const RpcMethod* method_;         // not owned
  ClientContext* context_;          // now owned
  const google::protobuf::Message* request_;  // not owned
  google::protobuf::Message* result_;         // not owned

  std::thread cq_poller_;
  std::mutex mu_;
  std::condition_variable invoke_cv_;
  std::condition_variable read_cv_;
  std::condition_variable write_cv_;
  std::condition_variable finish_cv_;
  grpc_event* invoke_ev_;
  // TODO(yangg) make these two into queues to support concurrent reads and
  // writes
  grpc_event* read_ev_;
  grpc_event* write_ev_;
  bool reading_;
  bool writing_;
  bool got_read_;
  bool got_write_;
  bool peer_halfclosed_;
  bool self_halfclosed_;
  bool stream_finished_;
  bool waiting_;
  Status final_status_;
};

}  // namespace grpc

#endif  // __GRPCPP_INTERNAL_STREAM_STREAM_CONTEXT_H__
