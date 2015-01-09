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
#include <memory>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/log.h>
#include <grpc/support/slice.h>

#include "src/cpp/rpc_method.h"
#include "src/cpp/proto/proto_utils.h"
#include "src/cpp/stream/stream_context.h"
#include <grpc++/channel_arguments.h>
#include <grpc++/client_context.h>
#include <grpc++/config.h>
#include <grpc++/credentials.h>
#include <grpc++/status.h>
#include <google/protobuf/message.h>

namespace grpc {

Channel::Channel(const grpc::string& target, const ChannelArguments& args)
    : target_(target) {
  grpc_channel_args channel_args;
  args.SetChannelArgs(&channel_args);
  c_channel_ = grpc_channel_create(
      target_.c_str(), channel_args.num_args > 0 ? &channel_args : nullptr);
}

Channel::Channel(const grpc::string& target,
                 const std::unique_ptr<Credentials>& creds,
                 const ChannelArguments& args)
    : target_(args.GetSslTargetNameOverride().empty()
                  ? target
                  : args.GetSslTargetNameOverride()) {
  grpc_channel_args channel_args;
  args.SetChannelArgs(&channel_args);
  grpc_credentials* c_creds = creds ? creds->GetRawCreds() : nullptr;
  c_channel_ = grpc_secure_channel_create(
      c_creds, target.c_str(),
      channel_args.num_args > 0 ? &channel_args : nullptr);
}

Channel::~Channel() { grpc_channel_destroy(c_channel_); }

namespace {
// Pluck the finished event and set to status when it is not nullptr.
void GetFinalStatus(grpc_completion_queue* cq, void* finished_tag,
                    Status* status) {
  grpc_event* ev =
      grpc_completion_queue_pluck(cq, finished_tag, gpr_inf_future);
  if (status) {
    StatusCode error_code = static_cast<StatusCode>(ev->data.finished.status);
    grpc::string details(ev->data.finished.details ? ev->data.finished.details
                                                   : "");
    *status = Status(error_code, details);
  }
  grpc_event_finish(ev);
}
}  // namespace

// TODO(yangg) more error handling
Status Channel::StartBlockingRpc(const RpcMethod& method,
                                 ClientContext* context,
                                 const google::protobuf::Message& request,
                                 google::protobuf::Message* result) {
  Status status;
  grpc_call* call = grpc_channel_create_call(
      c_channel_, method.name(), target_.c_str(), context->RawDeadline());
  context->set_call(call);
  grpc_event* ev;
  void* finished_tag = reinterpret_cast<char*>(call);
  void* invoke_tag = reinterpret_cast<char*>(call) + 1;
  void* metadata_read_tag = reinterpret_cast<char*>(call) + 2;
  void* write_tag = reinterpret_cast<char*>(call) + 3;
  void* halfclose_tag = reinterpret_cast<char*>(call) + 4;
  void* read_tag = reinterpret_cast<char*>(call) + 5;

  grpc_completion_queue* cq = grpc_completion_queue_create();
  context->set_cq(cq);
  // add_metadata from context
  //
  // invoke
  GPR_ASSERT(grpc_call_start_invoke(call, cq, invoke_tag, metadata_read_tag,
                                    finished_tag,
                                    GRPC_WRITE_BUFFER_HINT) == GRPC_CALL_OK);
  ev = grpc_completion_queue_pluck(cq, invoke_tag, gpr_inf_future);
  bool success = ev->data.invoke_accepted == GRPC_OP_OK;
  grpc_event_finish(ev);
  if (!success) {
    GetFinalStatus(cq, finished_tag, &status);
    return status;
  }
  // write request
  grpc_byte_buffer* write_buffer = nullptr;
  success = SerializeProto(request, &write_buffer);
  if (!success) {
    grpc_call_cancel(call);
    status =
        Status(StatusCode::DATA_LOSS, "Failed to serialize request proto.");
    GetFinalStatus(cq, finished_tag, nullptr);
    return status;
  }
  GPR_ASSERT(grpc_call_start_write(call, write_buffer, write_tag,
                                   GRPC_WRITE_BUFFER_HINT) == GRPC_CALL_OK);
  grpc_byte_buffer_destroy(write_buffer);
  ev = grpc_completion_queue_pluck(cq, write_tag, gpr_inf_future);

  success = ev->data.write_accepted == GRPC_OP_OK;
  grpc_event_finish(ev);
  if (!success) {
    GetFinalStatus(cq, finished_tag, &status);
    return status;
  }
  // writes done
  GPR_ASSERT(grpc_call_writes_done(call, halfclose_tag) == GRPC_CALL_OK);
  ev = grpc_completion_queue_pluck(cq, halfclose_tag, gpr_inf_future);
  grpc_event_finish(ev);
  // start read metadata
  //
  ev = grpc_completion_queue_pluck(cq, metadata_read_tag, gpr_inf_future);
  grpc_event_finish(ev);
  // start read
  GPR_ASSERT(grpc_call_start_read(call, read_tag) == GRPC_CALL_OK);
  ev = grpc_completion_queue_pluck(cq, read_tag, gpr_inf_future);
  if (ev->data.read) {
    if (!DeserializeProto(ev->data.read, result)) {
      grpc_event_finish(ev);
      status = Status(StatusCode::DATA_LOSS, "Failed to parse response proto.");
      GetFinalStatus(cq, finished_tag, nullptr);
      return status;
    }
  }
  grpc_event_finish(ev);

  // wait status
  GetFinalStatus(cq, finished_tag, &status);
  return status;
}

StreamContextInterface* Channel::CreateStream(const RpcMethod& method,
                                              ClientContext* context,
                                              const google::protobuf::Message* request,
                                              google::protobuf::Message* result) {
  grpc_call* call = grpc_channel_create_call(
      c_channel_, method.name(), target_.c_str(), context->RawDeadline());
  context->set_call(call);
  grpc_completion_queue* cq = grpc_completion_queue_create();
  context->set_cq(cq);
  return new StreamContext(method, context, request, result);
}

}  // namespace grpc
