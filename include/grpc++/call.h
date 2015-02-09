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

#ifndef __GRPCPP_CALL_H__
#define __GRPCPP_CALL_H__

#include <grpc++/status.h>
#include <grpc++/completion_queue.h>

#include <memory>
#include <vector>

namespace google {
namespace protobuf {
class Message;
}  // namespace protobuf
}  // namespace google

struct grpc_call;
struct grpc_op;

namespace grpc {

class ChannelInterface;

class CallOpBuffer final : public CompletionQueueTag {
 public:
  void AddSendInitialMetadata(std::vector<std::pair<grpc::string, grpc::string> > *metadata);
  void AddSendMessage(const google::protobuf::Message &message);
  void AddRecvMessage(google::protobuf::Message *message);
  void AddClientSendClose();
  void AddClientRecvStatus(Status *status);

  // INTERNAL API:

  // Convert to an array of grpc_op elements
  void FillOps(grpc_op *ops, size_t *nops);

  // Called by completion queue just prior to returning from Next() or Pluck()
  FinalizeResultOutput FinalizeResult(bool status) override;
};

class CCallDeleter {
 public:
  void operator()(grpc_call *c);
};

// Straightforward wrapping of the C call object
class Call final {
 public:
  Call(grpc_call *call, ChannelInterface *channel, CompletionQueue *cq);

  void PerformOps(CallOpBuffer *buffer, void *tag);

  grpc_call *call() { return call_.get(); }
  CompletionQueue *cq() { return cq_; }

 private:
  ChannelInterface *channel_;
  CompletionQueue *cq_;
  std::unique_ptr<grpc_call, CCallDeleter> call_;
};

}  // namespace grpc

#endif  // __GRPCPP_CALL_INTERFACE_H__
