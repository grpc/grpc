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

#include <grpc++/async_server_context.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include "src/cpp/proto/proto_utils.h"
#include <grpc++/config.h>
#include <grpc++/status.h>

namespace grpc {

AsyncServerContext::AsyncServerContext(
    grpc_call* call, const grpc::string& method, const grpc::string& host,
    system_clock::time_point absolute_deadline)
    : method_(method),
      host_(host),
      absolute_deadline_(absolute_deadline),
      request_(nullptr),
      call_(call) {}

AsyncServerContext::~AsyncServerContext() { grpc_call_destroy(call_); }

void AsyncServerContext::Accept(grpc_completion_queue* cq) {
  GPR_ASSERT(grpc_call_server_accept_old(call_, cq, this) == GRPC_CALL_OK);
  GPR_ASSERT(grpc_call_server_end_initial_metadata_old(
                 call_, GRPC_WRITE_BUFFER_HINT) == GRPC_CALL_OK);
}

bool AsyncServerContext::StartRead(grpc::protobuf::Message* request) {
  GPR_ASSERT(request);
  request_ = request;
  grpc_call_error err = grpc_call_start_read_old(call_, this);
  return err == GRPC_CALL_OK;
}

bool AsyncServerContext::StartWrite(const grpc::protobuf::Message& response,
                                    int flags) {
  grpc_byte_buffer* buffer = nullptr;
  GRPC_TIMER_MARK(SER_PROTO_BEGIN, call_->call());
  if (!SerializeProto(response, &buffer)) {
    return false;
  }
  GRPC_TIMER_MARK(SER_PROTO_END, call_->call());
  grpc_call_error err = grpc_call_start_write_old(call_, buffer, this, flags);
  grpc_byte_buffer_destroy(buffer);
  return err == GRPC_CALL_OK;
}

bool AsyncServerContext::StartWriteStatus(const Status& status) {
  grpc_call_error err = grpc_call_start_write_status_old(
      call_, static_cast<grpc_status_code>(status.code()),
      status.details().empty() ? nullptr
                               : const_cast<char*>(status.details().c_str()),
      this);
  return err == GRPC_CALL_OK;
}

bool AsyncServerContext::ParseRead(grpc_byte_buffer* read_buffer) {
  GPR_ASSERT(request_);
  GRPC_TIMER_MARK(DESER_PROTO_BEGIN, call_->call());
  bool success = DeserializeProto(read_buffer, request_);
  GRPC_TIMER_MARK(DESER_PROTO_END, call_->call());
  request_ = nullptr;
  return success;
}

}  // namespace grpc
