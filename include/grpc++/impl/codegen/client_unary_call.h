/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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

namespace internal {
class RpcMethod;
/// Wrapper that performs a blocking unary call
template <class InputMessage, class OutputMessage>
Status BlockingUnaryCall(ChannelInterface* channel, const RpcMethod& method,
                         ClientContext* context, const InputMessage& request,
                         OutputMessage* result) {
  return BlockingUnaryCallImpl<InputMessage, OutputMessage>(
             channel, method, context, request, result)
      .status();
}

template <class InputMessage, class OutputMessage>
class BlockingUnaryCallImpl {
 public:
  BlockingUnaryCallImpl(ChannelInterface* channel, const RpcMethod& method,
                        ClientContext* context, const InputMessage& request,
                        OutputMessage* result) {
    CompletionQueue cq(grpc_completion_queue_attributes{
        GRPC_CQ_CURRENT_VERSION, GRPC_CQ_PLUCK,
        GRPC_CQ_DEFAULT_POLLING});  // Pluckable completion queue
    Call call(channel->CreateCall(method, context, &cq));
    CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage,
              CallOpRecvInitialMetadata, CallOpRecvMessage<OutputMessage>,
              CallOpClientSendClose, CallOpClientRecvStatus>
        ops;
    status_ = ops.SendMessage(request);
    if (!status_.ok()) {
      return;
    }
    ops.SendInitialMetadata(context->send_initial_metadata_,
                            context->initial_metadata_flags());
    ops.RecvInitialMetadata(context);
    ops.RecvMessage(result);
    ops.ClientSendClose();
    ops.ClientRecvStatus(context, &status_);
    call.PerformOps(&ops);
    if (cq.Pluck(&ops)) {
      if (!ops.got_message && status_.ok()) {
        status_ = Status(StatusCode::UNIMPLEMENTED,
                         "No message returned for unary request");
      }
    } else {
      GPR_CODEGEN_ASSERT(!status_.ok());
    }
  }
  Status status() { return status_; }

 private:
  Status status_;
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_CLIENT_UNARY_CALL_H
