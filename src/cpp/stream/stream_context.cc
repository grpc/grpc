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

#include "src/cpp/stream/stream_context.h"

#include <grpc/support/log.h>
#include "src/cpp/proto/proto_utils.h"
#include "src/cpp/util/time.h"
#include <grpc++/client_context.h>
#include <grpc++/config.h>
#include <grpc++/impl/rpc_method.h>
#include <google/protobuf/message.h>

namespace grpc {

// Client only ctor
StreamContext::StreamContext(const RpcMethod& method, ClientContext* context,
                             const google::protobuf::Message* request,
                             google::protobuf::Message* result)
    : is_client_(true),
      method_(&method),
      call_(context->call()),
      cq_(context->cq()),
      request_(const_cast<google::protobuf::Message*>(request)),
      result_(result),
      peer_halfclosed_(false),
      self_halfclosed_(false) {
  GPR_ASSERT(method_->method_type() != RpcMethod::RpcType::NORMAL_RPC);
}

// Server only ctor
StreamContext::StreamContext(const RpcMethod& method, grpc_call* call,
                             grpc_completion_queue* cq,
                             google::protobuf::Message* request,
                             google::protobuf::Message* result)
    : is_client_(false),
      method_(&method),
      call_(call),
      cq_(cq),
      request_(request),
      result_(result),
      peer_halfclosed_(false),
      self_halfclosed_(false) {
  GPR_ASSERT(method_->method_type() != RpcMethod::RpcType::NORMAL_RPC);
}

StreamContext::~StreamContext() {}

void StreamContext::Start(bool buffered) {
  if (is_client_) {
    // TODO(yangg) handle metadata send path
    int flag = buffered ? GRPC_WRITE_BUFFER_HINT : 0;
    grpc_call_error error = grpc_call_invoke(
        call(), cq(), client_metadata_read_tag(), finished_tag(), flag);
    GPR_ASSERT(GRPC_CALL_OK == error);
  } else {
    // TODO(yangg) metadata needs to be added before accept
    // TODO(yangg) correctly set flag to accept
    GPR_ASSERT(grpc_call_server_accept(call(), cq(), finished_tag()) ==
               GRPC_CALL_OK);
    GPR_ASSERT(grpc_call_server_end_initial_metadata(call(), 0) ==
               GRPC_CALL_OK);
  }
}

bool StreamContext::Read(google::protobuf::Message* msg) {
  // TODO(yangg) check peer_halfclosed_ here for possible early return.
  grpc_call_error err = grpc_call_start_read(call(), read_tag());
  GPR_ASSERT(err == GRPC_CALL_OK);
  grpc_event* read_ev =
      grpc_completion_queue_pluck(cq(), read_tag(), gpr_inf_future);
  GPR_ASSERT(read_ev->type == GRPC_READ);
  bool ret = true;
  if (read_ev->data.read) {
    if (!DeserializeProto(read_ev->data.read, msg)) {
      ret = false;
      grpc_call_cancel_with_status(call(), GRPC_STATUS_DATA_LOSS,
                                   "Failed to parse incoming proto");
    }
  } else {
    ret = false;
    peer_halfclosed_ = true;
  }
  grpc_event_finish(read_ev);
  return ret;
}

bool StreamContext::Write(const google::protobuf::Message* msg, bool is_last) {
  // TODO(yangg) check self_halfclosed_ for possible early return.
  bool ret = true;
  grpc_event* ev = nullptr;

  if (msg) {
    grpc_byte_buffer* out_buf = nullptr;
    if (!SerializeProto(*msg, &out_buf)) {
      grpc_call_cancel_with_status(call(), GRPC_STATUS_INVALID_ARGUMENT,
                                   "Failed to serialize outgoing proto");
      return false;
    }
    int flag = is_last ? GRPC_WRITE_BUFFER_HINT : 0;
    grpc_call_error err =
        grpc_call_start_write(call(), out_buf, write_tag(), flag);
    grpc_byte_buffer_destroy(out_buf);
    GPR_ASSERT(err == GRPC_CALL_OK);

    ev = grpc_completion_queue_pluck(cq(), write_tag(), gpr_inf_future);
    GPR_ASSERT(ev->type == GRPC_WRITE_ACCEPTED);

    ret = ev->data.write_accepted == GRPC_OP_OK;
    grpc_event_finish(ev);
  }
  if (ret && is_last) {
    grpc_call_error err = grpc_call_writes_done(call(), halfclose_tag());
    GPR_ASSERT(err == GRPC_CALL_OK);
    ev = grpc_completion_queue_pluck(cq(), halfclose_tag(), gpr_inf_future);
    GPR_ASSERT(ev->type == GRPC_FINISH_ACCEPTED);
    grpc_event_finish(ev);

    self_halfclosed_ = true;
  } else if (!ret) {  // Stream broken
    self_halfclosed_ = true;
    peer_halfclosed_ = true;
  }

  return ret;
}

const Status& StreamContext::Wait() {
  // TODO(yangg) properly support metadata
  grpc_event* metadata_ev = grpc_completion_queue_pluck(
      cq(), client_metadata_read_tag(), gpr_inf_future);
  grpc_event_finish(metadata_ev);
  // TODO(yangg) protect states by a mutex, including other places.
  if (!self_halfclosed_ || !peer_halfclosed_) {
    Cancel();
  }
  grpc_event* finish_ev =
      grpc_completion_queue_pluck(cq(), finished_tag(), gpr_inf_future);
  GPR_ASSERT(finish_ev->type == GRPC_FINISHED);
  final_status_ = Status(
      static_cast<StatusCode>(finish_ev->data.finished.status),
      finish_ev->data.finished.details ? finish_ev->data.finished.details : "");
  grpc_event_finish(finish_ev);
  return final_status_;
}

void StreamContext::Cancel() { grpc_call_cancel(call()); }

}  // namespace grpc
