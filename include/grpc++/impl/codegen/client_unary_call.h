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

#ifndef GRPCXX_IMPL_CODEGEN_CLIENT_UNARY_CALL_H
#define GRPCXX_IMPL_CODEGEN_CLIENT_UNARY_CALL_H

#include <grpc++/impl/codegen/call.h>
#include <grpc++/impl/codegen/channel_interface.h>
#include <grpc++/impl/codegen/config.h>
#include <grpc++/impl/codegen/core_codegen_interface.h>
#include <grpc++/impl/codegen/status.h>

namespace grpc {

class Channel;
class ClientContext;
class CompletionQueue;
class RpcMethod;

// Wrapper that performs a blocking unary call
template <class InputMessage, class OutputMessage>
Status BlockingUnaryCall(ChannelInterface* channel, const RpcMethod& method,
                         ClientContext* context, const InputMessage& request,
                         OutputMessage* result) {
  CompletionQueue cq;
  Call call(channel->CreateCall(method, context, &cq));
  CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage,
            CallOpRecvInitialMetadata, CallOpRecvMessage<OutputMessage>,
            CallOpClientSendClose, CallOpClientRecvStatus>
      ops;
  Status status = ops.SendMessage(request);
  if (!status.ok()) {
    return status;
  }
  ops.SendInitialMetadata(context->send_initial_metadata_,
                          context->initial_metadata_flags());
  ops.RecvInitialMetadata(context);
  ops.RecvMessage(result);
  ops.ClientSendClose();
  ops.ClientRecvStatus(context, &status);
  call.PerformOps(&ops);
  GPR_CODEGEN_ASSERT((cq.Pluck(&ops) && ops.got_message) || !status.ok());
  return status;
}

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_CLIENT_UNARY_CALL_H
