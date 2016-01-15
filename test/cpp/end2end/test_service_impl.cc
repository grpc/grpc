/*
 *
 * Copyright 2016, Google Inc.
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

#include "test/cpp/end2end/test_service_impl.h"

#include <grpc++/security/credentials.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <gtest/gtest.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/cpp/util/string_ref_helper.h"

using std::chrono::system_clock;

namespace grpc {
namespace testing {
namespace {

// When echo_deadline is requested, deadline seen in the ServerContext is set in
// the response in seconds.
void MaybeEchoDeadline(ServerContext* context, const EchoRequest* request,
                       EchoResponse* response) {
  if (request->has_param() && request->param().echo_deadline()) {
    gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
    if (context->deadline() != system_clock::time_point::max()) {
      Timepoint2Timespec(context->deadline(), &deadline);
    }
    response->mutable_param()->set_request_deadline(deadline.tv_sec);
  }
}

void CheckServerAuthContext(const ServerContext* context,
                            const grpc::string& expected_client_identity) {
  std::shared_ptr<const AuthContext> auth_ctx = context->auth_context();
  std::vector<grpc::string_ref> ssl =
      auth_ctx->FindPropertyValues("transport_security_type");
  EXPECT_EQ(1u, ssl.size());
  EXPECT_EQ("ssl", ToString(ssl[0]));
  if (expected_client_identity.length() == 0) {
    EXPECT_TRUE(auth_ctx->GetPeerIdentityPropertyName().empty());
    EXPECT_TRUE(auth_ctx->GetPeerIdentity().empty());
    EXPECT_FALSE(auth_ctx->IsPeerAuthenticated());
  } else {
    auto identity = auth_ctx->GetPeerIdentity();
    EXPECT_TRUE(auth_ctx->IsPeerAuthenticated());
    EXPECT_EQ(1u, identity.size());
    EXPECT_EQ(expected_client_identity, identity[0]);
  }
}
}  // namespace

Status TestServiceImpl::Echo(ServerContext* context, const EchoRequest* request,
                             EchoResponse* response) {
  response->set_message(request->message());
  MaybeEchoDeadline(context, request, response);
  if (host_) {
    response->mutable_param()->set_host(*host_);
  }
  if (request->has_param() && request->param().client_cancel_after_us()) {
    {
      std::unique_lock<std::mutex> lock(mu_);
      signal_client_ = true;
    }
    while (!context->IsCancelled()) {
      gpr_sleep_until(gpr_time_add(
          gpr_now(GPR_CLOCK_REALTIME),
          gpr_time_from_micros(request->param().client_cancel_after_us(),
                               GPR_TIMESPAN)));
    }
    return Status::CANCELLED;
  } else if (request->has_param() &&
             request->param().server_cancel_after_us()) {
    gpr_sleep_until(gpr_time_add(
        gpr_now(GPR_CLOCK_REALTIME),
        gpr_time_from_micros(request->param().server_cancel_after_us(),
                             GPR_TIMESPAN)));
    return Status::CANCELLED;
  } else {
    EXPECT_FALSE(context->IsCancelled());
  }

  if (request->has_param() && request->param().echo_metadata()) {
    const std::multimap<grpc::string_ref, grpc::string_ref>& client_metadata =
        context->client_metadata();
    for (std::multimap<grpc::string_ref, grpc::string_ref>::const_iterator
             iter = client_metadata.begin();
         iter != client_metadata.end(); ++iter) {
      context->AddTrailingMetadata(ToString(iter->first),
                                   ToString(iter->second));
    }
  }
  if (request->has_param() &&
      (request->param().expected_client_identity().length() > 0 ||
       request->param().check_auth_context())) {
    CheckServerAuthContext(context,
                           request->param().expected_client_identity());
  }
  if (request->has_param() && request->param().response_message_length() > 0) {
    response->set_message(
        grpc::string(request->param().response_message_length(), '\0'));
  }
  if (request->has_param() && request->param().echo_peer()) {
    response->mutable_param()->set_peer(context->peer());
  }
  return Status::OK;
}

// Unimplemented is left unimplemented to test the returned error.

Status TestServiceImpl::RequestStream(ServerContext* context,
                                      ServerReader<EchoRequest>* reader,
                                      EchoResponse* response) {
  EchoRequest request;
  response->set_message("");
  int cancel_after_reads = 0;
  const std::multimap<grpc::string_ref, grpc::string_ref>&
      client_initial_metadata = context->client_metadata();
  if (client_initial_metadata.find(kServerCancelAfterReads) !=
      client_initial_metadata.end()) {
    std::istringstream iss(ToString(
        client_initial_metadata.find(kServerCancelAfterReads)->second));
    iss >> cancel_after_reads;
    gpr_log(GPR_INFO, "cancel_after_reads %d", cancel_after_reads);
  }
  while (reader->Read(&request)) {
    if (cancel_after_reads == 1) {
      gpr_log(GPR_INFO, "return cancel status");
      return Status::CANCELLED;
    } else if (cancel_after_reads > 0) {
      cancel_after_reads--;
    }
    response->mutable_message()->append(request.message());
  }
  return Status::OK;
}

// Return 3 messages.
// TODO(yangg) make it generic by adding a parameter into EchoRequest
Status TestServiceImpl::ResponseStream(ServerContext* context,
                                       const EchoRequest* request,
                                       ServerWriter<EchoResponse>* writer) {
  EchoResponse response;
  response.set_message(request->message() + "0");
  writer->Write(response);
  response.set_message(request->message() + "1");
  writer->Write(response);
  response.set_message(request->message() + "2");
  writer->Write(response);

  return Status::OK;
}

Status TestServiceImpl::BidiStream(
    ServerContext* context,
    ServerReaderWriter<EchoResponse, EchoRequest>* stream) {
  EchoRequest request;
  EchoResponse response;
  while (stream->Read(&request)) {
    gpr_log(GPR_INFO, "recv msg %s", request.message().c_str());
    response.set_message(request.message());
    stream->Write(response);
  }
  return Status::OK;
}

}  // namespace testing
}  // namespace grpc
