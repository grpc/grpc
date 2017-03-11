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

#include "test/cpp/util/cli_call.h"

#include <iostream>

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/support/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>

namespace grpc {
namespace testing {
namespace {
void* tag(int i) { return (void*)(intptr_t)i; }
}  // namespace

Status CliCall::Call(std::shared_ptr<grpc::Channel> channel,
                     const grpc::string& method, const grpc::string& request,
                     grpc::string* response,
                     const OutgoingMetadataContainer& metadata,
                     IncomingMetadataContainer* server_initial_metadata,
                     IncomingMetadataContainer* server_trailing_metadata) {
  CliCall call(channel, method, metadata);
  call.Write(request);
  call.WritesDone();
  if (!call.Read(response, server_initial_metadata)) {
    fprintf(stderr, "Failed to read response.\n");
  }
  return call.Finish(server_trailing_metadata);
}

CliCall::CliCall(std::shared_ptr<grpc::Channel> channel,
                 const grpc::string& method,
                 const OutgoingMetadataContainer& metadata)
    : stub_(new grpc::GenericStub(channel)) {
  gpr_mu_init(&write_mu_);
  gpr_cv_init(&write_cv_);
  if (!metadata.empty()) {
    for (OutgoingMetadataContainer::const_iterator iter = metadata.begin();
         iter != metadata.end(); ++iter) {
      ctx_.AddMetadata(iter->first, iter->second);
    }
  }
  call_ = stub_->Call(&ctx_, method, &cq_, tag(1));
  void* got_tag;
  bool ok;
  cq_.Next(&got_tag, &ok);
  GPR_ASSERT(ok);
}

CliCall::~CliCall() {
  gpr_cv_destroy(&write_cv_);
  gpr_mu_destroy(&write_mu_);
}

void CliCall::Write(const grpc::string& request) {
  void* got_tag;
  bool ok;

  grpc_slice s = grpc_slice_from_copied_string(request.c_str());
  grpc::Slice req_slice(s, grpc::Slice::STEAL_REF);
  grpc::ByteBuffer send_buffer(&req_slice, 1);
  call_->Write(send_buffer, tag(2));
  cq_.Next(&got_tag, &ok);
  GPR_ASSERT(ok);
}

bool CliCall::Read(grpc::string* response,
                   IncomingMetadataContainer* server_initial_metadata) {
  void* got_tag;
  bool ok;

  grpc::ByteBuffer recv_buffer;
  call_->Read(&recv_buffer, tag(3));

  if (!cq_.Next(&got_tag, &ok) || !ok) {
    return false;
  }
  std::vector<grpc::Slice> slices;
  GPR_ASSERT(recv_buffer.Dump(&slices).ok());

  response->clear();
  for (size_t i = 0; i < slices.size(); i++) {
    response->append(reinterpret_cast<const char*>(slices[i].begin()),
                     slices[i].size());
  }
  if (server_initial_metadata) {
    *server_initial_metadata = ctx_.GetServerInitialMetadata();
  }
  return true;
}

void CliCall::WritesDone() {
  void* got_tag;
  bool ok;

  call_->WritesDone(tag(4));
  cq_.Next(&got_tag, &ok);
  GPR_ASSERT(ok);
}

void CliCall::WriteAndWait(const grpc::string& request) {
  grpc_slice s = grpc_slice_from_copied_string(request.c_str());
  grpc::Slice req_slice(s, grpc::Slice::STEAL_REF);
  grpc::ByteBuffer send_buffer(&req_slice, 1);

  gpr_mu_lock(&write_mu_);
  call_->Write(send_buffer, tag(2));
  write_done_ = false;
  while (!write_done_) {
    gpr_cv_wait(&write_cv_, &write_mu_, gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  gpr_mu_unlock(&write_mu_);
}

void CliCall::WritesDoneAndWait() {
  gpr_mu_lock(&write_mu_);
  call_->WritesDone(tag(4));
  write_done_ = false;
  while (!write_done_) {
    gpr_cv_wait(&write_cv_, &write_mu_, gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  gpr_mu_unlock(&write_mu_);
}

bool CliCall::ReadAndMaybeNotifyWrite(
    grpc::string* response,
    IncomingMetadataContainer* server_initial_metadata) {
  void* got_tag;
  bool ok;
  grpc::ByteBuffer recv_buffer;

  call_->Read(&recv_buffer, tag(3));
  bool cq_result = cq_.Next(&got_tag, &ok);

  while (got_tag != tag(3)) {
    gpr_mu_lock(&write_mu_);
    write_done_ = true;
    gpr_cv_signal(&write_cv_);
    gpr_mu_unlock(&write_mu_);

    cq_result = cq_.Next(&got_tag, &ok);
    if (got_tag == tag(2)) {
      GPR_ASSERT(ok);
    }
  }

  if (!cq_result || !ok) {
    // If the RPC is ended on the server side, we should still wait for the
    // pending write on the client side to be done.
    if (!ok) {
      gpr_mu_lock(&write_mu_);
      if (!write_done_) {
        cq_.Next(&got_tag, &ok);
        GPR_ASSERT(got_tag != tag(2));
        write_done_ = true;
        gpr_cv_signal(&write_cv_);
      }
      gpr_mu_unlock(&write_mu_);
    }
    return false;
  }

  std::vector<grpc::Slice> slices;
  GPR_ASSERT(recv_buffer.Dump(&slices).ok());
  response->clear();
  for (size_t i = 0; i < slices.size(); i++) {
    response->append(reinterpret_cast<const char*>(slices[i].begin()),
                     slices[i].size());
  }
  if (server_initial_metadata) {
    *server_initial_metadata = ctx_.GetServerInitialMetadata();
  }
  return true;
}

Status CliCall::Finish(IncomingMetadataContainer* server_trailing_metadata) {
  void* got_tag;
  bool ok;
  grpc::Status status;

  call_->Finish(&status, tag(5));
  cq_.Next(&got_tag, &ok);
  GPR_ASSERT(ok);
  if (server_trailing_metadata) {
    *server_trailing_metadata = ctx_.GetServerTrailingMetadata();
  }

  return status;
}

}  // namespace testing
}  // namespace grpc
