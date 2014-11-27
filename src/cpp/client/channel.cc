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

#include "src/cpp/client/channel.h"

#include <chrono>
#include <string>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice.h>
#include <grpc/support/time.h>

#include "src/cpp/rpc_method.h"
#include "src/cpp/proto/proto_utils.h"
#include "src/cpp/stream/stream_context.h"
#include "src/cpp/util/time.h"
#include <grpc++/config.h>
#include <google/protobuf/message.h>
#include <grpc++/client_context.h>
#include <grpc++/status.h>

namespace grpc {

Channel::Channel(const grpc::string& target) : target_(target) {
  c_channel_ = grpc_channel_create(target_.c_str(), nullptr);
}

Channel::~Channel() { grpc_channel_destroy(c_channel_); }

namespace {
// Poll one event from the compeletion queue. Return false when an error
// occured or the polled type is not expected. If a finished event has been
// polled, set finished and set status if it has not been set.
bool NextEvent(grpc_completion_queue* cq, grpc_completion_type expected_type,
               bool* finished, bool* status_set, Status* status,
               google::protobuf::Message* result) {
  // We rely on the c layer to enforce deadline and thus do not use deadline
  // here.
  grpc_event* ev = grpc_completion_queue_next(cq, gpr_inf_future);
  if (!ev) {
    return false;
  }
  bool ret = ev->type == expected_type;
  switch (ev->type) {
    case GRPC_INVOKE_ACCEPTED:
      ret = ret && (ev->data.invoke_accepted == GRPC_OP_OK);
      break;
    case GRPC_READ:
      ret = ret && (ev->data.read != nullptr);
      if (ret && !DeserializeProto(ev->data.read, result)) {
        *status_set = true;
        *status =
            Status(StatusCode::DATA_LOSS, "Failed to parse response proto.");
        ret = false;
      }
      break;
    case GRPC_WRITE_ACCEPTED:
      ret = ret && (ev->data.write_accepted == GRPC_OP_OK);
      break;
    case GRPC_FINISH_ACCEPTED:
      ret = ret && (ev->data.finish_accepted == GRPC_OP_OK);
      break;
    case GRPC_CLIENT_METADATA_READ:
      break;
    case GRPC_FINISHED:
      *finished = true;
      if (!*status_set) {
        *status_set = true;
        StatusCode error_code = static_cast<StatusCode>(ev->data.finished.code);
        grpc::string details(
            ev->data.finished.details ? ev->data.finished.details : "");
        *status = Status(error_code, details);
      }
      break;
    default:
      gpr_log(GPR_ERROR, "Dropping unhandled event with type %d", ev->type);
      break;
  }
  grpc_event_finish(ev);
  return ret;
}

// If finished is not true, get final status by polling until a finished
// event is obtained.
void GetFinalStatus(grpc_completion_queue* cq, bool status_set, bool finished,
                    Status* status) {
  while (!finished) {
    NextEvent(cq, GRPC_FINISHED, &finished, &status_set, status, nullptr);
  }
}

}  // namespace

// TODO(yangg) more error handling
Status Channel::StartBlockingRpc(const RpcMethod& method,
                                 ClientContext* context,
                                 const google::protobuf::Message& request,
                                 google::protobuf::Message* result) {
  Status status;
  bool status_set = false;
  bool finished = false;
  gpr_timespec absolute_deadline;
  AbsoluteDeadlineTimepoint2Timespec(context->absolute_deadline(),
                                     &absolute_deadline);
  grpc_call* call = grpc_channel_create_call(c_channel_, method.name(),
                                             // FIXME(yangg)
                                             "localhost", absolute_deadline);
  context->set_call(call);
  grpc_completion_queue* cq = grpc_completion_queue_create();
  context->set_cq(cq);
  // add_metadata from context
  //
  // invoke
  GPR_ASSERT(grpc_call_start_invoke(call, cq, call, call, call,
                                    GRPC_WRITE_BUFFER_HINT) == GRPC_CALL_OK);
  if (!NextEvent(cq, GRPC_INVOKE_ACCEPTED, &status_set, &finished, &status,
                 nullptr)) {
    GetFinalStatus(cq, finished, status_set, &status);
    return status;
  }
  // write request
  grpc_byte_buffer* write_buffer = nullptr;
  bool success = SerializeProto(request, &write_buffer);
  if (!success) {
    grpc_call_cancel(call);
    status_set = true;
    status =
        Status(StatusCode::DATA_LOSS, "Failed to serialize request proto.");
    GetFinalStatus(cq, finished, status_set, &status);
    return status;
  }
  GPR_ASSERT(grpc_call_start_write(call, write_buffer, call,
                                   GRPC_WRITE_BUFFER_HINT) == GRPC_CALL_OK);
  grpc_byte_buffer_destroy(write_buffer);
  if (!NextEvent(cq, GRPC_WRITE_ACCEPTED, &finished, &status_set, &status,
                 nullptr)) {
    GetFinalStatus(cq, finished, status_set, &status);
    return status;
  }
  // writes done
  GPR_ASSERT(grpc_call_writes_done(call, call) == GRPC_CALL_OK);
  if (!NextEvent(cq, GRPC_FINISH_ACCEPTED, &finished, &status_set, &status,
                 nullptr)) {
    GetFinalStatus(cq, finished, status_set, &status);
    return status;
  }
  // start read metadata
  //
  if (!NextEvent(cq, GRPC_CLIENT_METADATA_READ, &finished, &status_set, &status,
                 nullptr)) {
    GetFinalStatus(cq, finished, status_set, &status);
    return status;
  }
  // start read
  GPR_ASSERT(grpc_call_start_read(call, call) == GRPC_CALL_OK);
  if (!NextEvent(cq, GRPC_READ, &finished, &status_set, &status, result)) {
    GetFinalStatus(cq, finished, status_set, &status);
    return status;
  }
  // wait status
  GetFinalStatus(cq, finished, status_set, &status);
  return status;
}

StreamContextInterface* Channel::CreateStream(const RpcMethod& method,
                                              ClientContext* context,
                                              const google::protobuf::Message* request,
                                              google::protobuf::Message* result) {
  gpr_timespec absolute_deadline;
  AbsoluteDeadlineTimepoint2Timespec(context->absolute_deadline(),
                                     &absolute_deadline);
  grpc_call* call = grpc_channel_create_call(c_channel_, method.name(),
                                             // FIXME(yangg)
                                             "localhost", absolute_deadline);
  context->set_call(call);
  grpc_completion_queue* cq = grpc_completion_queue_create();
  context->set_cq(cq);
  return new StreamContext(method, context, request, result);
}

}  // namespace grpc
