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
#include <grpc/grpc.h>

namespace google {
namespace protobuf {
class Message;
}  // namespace protobuf
}  // namespace google

struct grpc_call;

namespace grpc {

class ChannelInterface;

class CallOpBuffer final {
 public:
  void AddSendMessage(const google::protobuf::Message &message);
  void AddRecvMessage(google::protobuf::Message *message);
  void AddClientSendClose();
  void AddClientRecvStatus(Status *status);

  void FinalizeResult();

 private:
  static const size_t MAX_OPS = 6;
  grpc_op ops_[MAX_OPS];
  int num_ops_ = 0;
};

// Straightforward wrapping of the C call object
class Call final {
 public:
  Call(grpc_call *call, ChannelInterface *channel);

  void PerformOps(const CallOpBuffer &buffer, void *tag);

 private:
  ChannelInterface *const channel_;
};

}  // namespace grpc

#endif  // __GRPCPP_CALL_INTERFACE_H__
