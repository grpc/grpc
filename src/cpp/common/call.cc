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

#include <include/grpc/support/alloc.h>
#include <include/grpc++/impl/call.h>
#include <include/grpc++/channel_interface.h>

#include "src/cpp/proto/proto_utils.h"

namespace grpc {

void CallOpBuffer::Reset(void* next_return_tag) {
  return_tag_ = next_return_tag;
  initial_metadata_count_ = 0;
  if (initial_metadata_) {
    gpr_free(initial_metadata_);
  }
  send_message_ = nullptr;
  if (write_buffer_) {
    grpc_byte_buffer_destroy(write_buffer_);
    write_buffer_ = nullptr;
  }
  recv_message_ = nullptr;
  if (recv_message_buf_) {
    grpc_byte_buffer_destroy(recv_message_buf_);
    recv_message_buf_ = nullptr;
  }
  client_send_close_ = false;
  recv_status_ = nullptr;
  status_code_ = GRPC_STATUS_OK;
  if (status_details_) {
    gpr_free(status_details_);
    status_details_ = nullptr;
  }
  status_details_capacity_ = 0;
}

namespace {
// TODO(yangg) if the map is changed before we send, the pointers will be a
// mess. Make sure it does not happen.
grpc_metadata* FillMetadata(
    std::multimap<grpc::string, grpc::string>* metadata) {
  if (metadata->empty()) { return nullptr; }
  grpc_metadata* metadata_array = (grpc_metadata*)gpr_malloc(
      metadata->size()* sizeof(grpc_metadata));
  size_t i = 0;
  for (auto iter = metadata->cbegin();
       iter != metadata->cend();
       ++iter, ++i) {
    metadata_array[i].key = iter->first.c_str();
    metadata_array[i].value = iter->second.c_str();
    metadata_array[i].value_length = iter->second.size();
  }
  return metadata_array;
}
}  // namespace

void CallOpBuffer::AddSendInitialMetadata(
    std::multimap<grpc::string, grpc::string>* metadata) {
  initial_metadata_count_ = metadata->size();
  initial_metadata_ = FillMetadata(metadata);
}

void CallOpBuffer::AddSendMessage(const google::protobuf::Message& message) {
  send_message_ = &message;
}

void CallOpBuffer::AddRecvMessage(google::protobuf::Message *message) {
  recv_message_ = message;
}

void CallOpBuffer::AddClientSendClose() {
  client_send_close_ = true;
}

void CallOpBuffer::AddClientRecvStatus(Status *status) {
  recv_status_ = status;
}


void CallOpBuffer::FillOps(grpc_op *ops, size_t *nops) {
  *nops = 0;
  if (initial_metadata_count_) {
    ops[*nops].op = GRPC_OP_SEND_INITIAL_METADATA;
    ops[*nops].data.send_initial_metadata.count = initial_metadata_count_;
    ops[*nops].data.send_initial_metadata.metadata = initial_metadata_;
    (*nops)++;
  }
  if (send_message_) {
    bool success = SerializeProto(*send_message_, &write_buffer_);
    if (!success) {
      // TODO handle parse failure
    }
    ops[*nops].op = GRPC_OP_SEND_MESSAGE;
    ops[*nops].data.send_message = write_buffer_;
    (*nops)++;
  }
  if (recv_message_) {
    ops[*nops].op = GRPC_OP_RECV_MESSAGE;
    ops[*nops].data.recv_message = &recv_message_buf_;
    (*nops)++;
  }
  if (client_send_close_) {
    ops[*nops].op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
    (*nops)++;
  }
  if (recv_status_) {
    ops[*nops].op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    // ops[*nops].data.recv_status_on_client.trailing_metadata =
    ops[*nops].data.recv_status_on_client.status = &status_code_;
    ops[*nops].data.recv_status_on_client.status_details = &status_details_;
    ops[*nops].data.recv_status_on_client.status_details_capacity = &status_details_capacity_;
    (*nops)++;
  }
}

void CallOpBuffer::FinalizeResult(void *tag, bool *status) {
  // Release send buffers
  if (write_buffer_) {
    grpc_byte_buffer_destroy(write_buffer_);
    write_buffer_ = nullptr;
  }
}

void CCallDeleter::operator()(grpc_call* c) {
  grpc_call_destroy(c);
}

Call::Call(grpc_call* call, ChannelInterface* channel, CompletionQueue* cq)
    : channel_(channel), cq_(cq), call_(call) {}

void Call::PerformOps(CallOpBuffer* buffer) {
  channel_->PerformOpsOnCall(buffer, this);

}  // namespace grpc
