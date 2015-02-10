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

#include <include/grpc++/impl/call.h>
#include <include/grpc++/channel_interface.h>

namespace grpc {

void CallOpBuffer::Reset(void* next_return_tag) {
  return_tag_ = next_return_tag;
  metadata_ = nullptr;
  send_message_ = nullptr;
  recv_message_ = nullptr;
  client_send_close_ = false;
  status_ = false;
}

void CallOpBuffer::AddSendInitialMetadata(
    std::multimap<igrpc::string, grpc::string>* metadata) {
  metadata_ = metadata;
}

void CallOpBuffer::AddSendMessage(const google::protobuf::Message& message) {
  send_message_ = &message;
}

void CallOpBuffer::AddRecvMessage(google::protobuf::Message *message) {
  recv_message_ = message;
}

void CallOpBuffer::AddClientSendClose() {
  client_sent_close_ = true;
}

void CallOpBuffer::AddClientRecvStatus(Status *status) {
  status_ = status;
}

void CallOpBuffer::FillOps(grpc_op *ops, size_t *nops) {


}

void CallOpBuffer::FinalizeResult(void *tag, bool *status) {

}

void CCallDeleter::operator()(grpc_call* c) {
  grpc_call_destroy(c);
}

Call::Call(grpc_call* call, ChannelInterface* channel, CompletionQueue* cq)
    : channel_(channel), cq_(cq), call_(call) {}

void Call::PerformOps(CallOpBuffer* buffer) {
  channel_->PerformOpsOnCall(buffer, this);
}

}  // namespace grpc
