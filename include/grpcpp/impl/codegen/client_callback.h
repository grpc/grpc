/*
 *
 * Copyright 2018 gRPC authors.
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

#ifndef GRPCPP_IMPL_CODEGEN_CLIENT_CALLBACK_H
#define GRPCPP_IMPL_CODEGEN_CLIENT_CALLBACK_H

#include <functional>

#include <grpcpp/impl/codegen/call.h>
#include <grpcpp/impl/codegen/callback_common.h>
#include <grpcpp/impl/codegen/channel_interface.h>
#include <grpcpp/impl/codegen/config.h>
#include <grpcpp/impl/codegen/core_codegen_interface.h>
#include <grpcpp/impl/codegen/status.h>

namespace grpc {

class Channel;
class ClientContext;
class CompletionQueue;

namespace internal {
class RpcMethod;

/// Perform a callback-based unary call
/// TODO(vjpai): Combine as much as possible with the blocking unary call code
template <class InputMessage, class OutputMessage>
void CallbackUnaryCall(ChannelInterface* channel, const RpcMethod& method,
                       ClientContext* context, const InputMessage* request,
                       OutputMessage* result,
                       std::function<void(Status)> on_completion) {
  CallbackUnaryCallImpl<InputMessage, OutputMessage> x(
      channel, method, context, request, result, on_completion);
}

template <class InputMessage, class OutputMessage>
class CallbackUnaryCallImpl {
 public:
  CallbackUnaryCallImpl(ChannelInterface* channel, const RpcMethod& method,
                        ClientContext* context, const InputMessage* request,
                        OutputMessage* result,
                        std::function<void(Status)> on_completion) {
    CompletionQueue* cq = channel->CallbackCQ();
    GPR_CODEGEN_ASSERT(cq != nullptr);
    Call call(channel->CreateCall(method, context, cq));

    using FullCallOpSet =
        CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage,
                  CallOpRecvInitialMetadata, CallOpRecvMessage<OutputMessage>,
                  CallOpClientSendClose, CallOpClientRecvStatus>;

    auto* ops = new (g_core_codegen_interface->grpc_call_arena_alloc(
        call.call(), sizeof(FullCallOpSet))) FullCallOpSet;

    auto* tag = new (g_core_codegen_interface->grpc_call_arena_alloc(
        call.call(), sizeof(CallbackWithStatusTag)))
        CallbackWithStatusTag(call.call(), on_completion, ops);

    // TODO(vjpai): Unify code with sync API as much as possible
    Status s = ops->SendMessage(*request);
    if (!s.ok()) {
      tag->force_run(s);
      return;
    }
    ops->SendInitialMetadata(&context->send_initial_metadata_,
                             context->initial_metadata_flags());
    ops->RecvInitialMetadata(context);
    ops->RecvMessage(result);
    ops->AllowNoMessage();
    ops->ClientSendClose();
    ops->ClientRecvStatus(context, tag->status_ptr());
    ops->set_core_cq_tag(tag);
    call.PerformOps(ops);
  }
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_CLIENT_CALLBACK_H
