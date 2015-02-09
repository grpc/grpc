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

#ifndef __GRPCPP_CHANNEL_INTERFACE_H__
#define __GRPCPP_CHANNEL_INTERFACE_H__

#include <grpc++/status.h>

namespace google {
namespace protobuf {
class Message;
}  // namespace protobuf
}  // namespace google

struct grpc_call;

namespace grpc {
class Call;
class CallOpBuffer;
class ClientContext;
class CompletionQueue;
class RpcMethod;
class StreamContextInterface;
class CallInterface;

class ChannelInterface {
 public:
  virtual ~ChannelInterface() {}

  virtual Call CreateCall(const RpcMethod &method, ClientContext *context,
                          CompletionQueue *cq) = 0;
  virtual void PerformOpsOnCall(CallOpBuffer *ops, void *tag, Call *call) = 0;
};

// Wrapper that begins an asynchronous unary call
void AsyncUnaryCall(ChannelInterface *channel, const RpcMethod &method,
                    ClientContext *context,
                    const google::protobuf::Message &request,
                    google::protobuf::Message *result, Status *status,
                    CompletionQueue *cq, void *tag);

// Wrapper that performs a blocking unary call
Status BlockingUnaryCall(ChannelInterface *channel, const RpcMethod &method,
                         ClientContext *context,
                         const google::protobuf::Message &request,
                         google::protobuf::Message *result);

}  // namespace grpc

#endif  // __GRPCPP_CHANNEL_INTERFACE_H__
