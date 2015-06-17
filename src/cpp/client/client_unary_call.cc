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

#include <grpc++/impl/client_unary_call.h>
#include <grpc++/impl/call.h>
#include <grpc++/channel_interface.h>
#include <grpc++/client_context.h>
#include <grpc++/completion_queue.h>
#include <grpc++/status.h>
#include <grpc/support/log.h>

namespace grpc {

// Wrapper that performs a blocking unary call
Status BlockingUnaryCall(ChannelInterface* channel, const RpcMethod& method,
                         ClientContext* context,
                         const grpc::protobuf::Message& request,
                         grpc::protobuf::Message* result) {
  CompletionQueue cq;
  Call call(channel->CreateCall(method, context, &cq));
  CallOpBuffer buf;
  Status status;
  buf.AddSendInitialMetadata(context);
  buf.AddSendMessage(request);
  buf.AddRecvInitialMetadata(context);
  buf.AddRecvMessage(result);
  buf.AddClientSendClose();
  buf.AddClientRecvStatus(context, &status);
  call.PerformOps(&buf);
  GPR_ASSERT((cq.Pluck(&buf) && buf.got_message) || !status.ok());
  return status;
}

}  // namespace grpc
