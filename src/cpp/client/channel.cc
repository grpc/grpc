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

#include "src/cpp/client/channel.h"

#include <chrono>
#include <memory>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/log.h>
#include <grpc/support/slice.h>

#include "src/cpp/proto/proto_utils.h"
#include <grpc++/channel_arguments.h>
#include <grpc++/client_context.h>
#include <grpc++/completion_queue.h>
#include <grpc++/config.h>
#include <grpc++/credentials.h>
#include <grpc++/impl/call.h>
#include <grpc++/impl/rpc_method.h>
#include <grpc++/status.h>
#include <google/protobuf/message.h>

namespace grpc {

Channel::Channel(const grpc::string& target, grpc_channel* channel)
    : target_(target), c_channel_(channel) {}

Channel::~Channel() { grpc_channel_destroy(c_channel_); }

Call Channel::CreateCall(const RpcMethod& method, ClientContext* context,
                         CompletionQueue* cq) {
  auto c_call = grpc_channel_create_call(c_channel_, cq->cq(), method.name(),
                                         context->authority().empty()
                                             ? target_.c_str()
                                             : context->authority().c_str(),
                                         context->RawDeadline());
  context->set_call(c_call);
  return Call(c_call, this, cq);
}

void Channel::PerformOpsOnCall(CallOpBuffer* buf, Call* call) {
  static const size_t MAX_OPS = 8;
  size_t nops = MAX_OPS;
  grpc_op ops[MAX_OPS];
  buf->FillOps(ops, &nops);
  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_batch(call->call(), ops, nops, buf));
}

}  // namespace grpc
