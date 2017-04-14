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

#include <string>
#include <thread>

#include <grpc++/security/credentials.h>
#include <grpc++/server_context.h>
#include <grpc/support/log.h>

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

void CheckServerAuthContext(
    const ServerContext* context,
    const grpc::string& expected_transport_security_type,
    const grpc::string& expected_client_identity) {
  std::shared_ptr<const AuthContext> auth_ctx = context->auth_context();
  std::vector<grpc::string_ref> tst =
      auth_ctx->FindPropertyValues("transport_security_type");
  EXPECT_EQ(1u, tst.size());
  EXPECT_EQ(expected_transport_security_type, ToString(tst[0]));
  if (expected_client_identity.empty()) {
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
  if (request->has_param() && request->param().server_die()) {
    gpr_log(GPR_ERROR, "The request should not reach application handler.");
    GPR_ASSERT(0);
  }
  if (request->has_param() && request->param().has_expected_error()) {
    const auto& error = request->param().expected_error();
    return Status(static_cast<StatusCode>(error.code()), error.error_message(),
                  error.binary_error_details());
  }
  int server_try_cancel = GetIntValueFromMetadata(
      kServerTryCancelRequest, context->client_metadata(), DO_NOT_CANCEL);
  if (server_try_cancel > DO_NOT_CANCEL) {
    // Since this is a unary RPC, by the time this server handler is called,
    // the 'request' message is already read from the client. So the scenarios
    // in server_try_cancel don't make much sense. Just cancel the RPC as long
    // as server_try_cancel is not DO_NOT_CANCEL
    ServerTryCancel(context);
    return Status::CANCELLED;
  }

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
  } else if (!request->has_param() ||
             !request->param().skip_cancelled_check()) {
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
    // Terminate rpc with error and debug info in trailer.
    if (request->param().debug_info().stack_entries_size() ||
        !request->param().debug_info().detail().empty()) {
      grpc::string serialized_debug_info =
          request->param().debug_info().SerializeAsString();
      context->AddTrailingMetadata(kDebugInfoTrailerKey, serialized_debug_info);
      return Status::CANCELLED;
    }
  }
  if (request->has_param() &&
      (request->param().expected_client_identity().length() > 0 ||
       request->param().check_auth_context())) {
    CheckServerAuthContext(context,
                           request->param().expected_transport_security_type(),
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
  // If 'server_try_cancel' is set in the metadata, the RPC is cancelled by
  // the server by calling ServerContext::TryCancel() depending on the value:
  //   CANCEL_BEFORE_PROCESSING: The RPC is cancelled before the server reads
  //   any message from the client
  //   CANCEL_DURING_PROCESSING: The RPC is cancelled while the server is
  //   reading messages from the client
  //   CANCEL_AFTER_PROCESSING: The RPC is cancelled after the server reads
  //   all the messages from the client
  int server_try_cancel = GetIntValueFromMetadata(
      kServerTryCancelRequest, context->client_metadata(), DO_NOT_CANCEL);

  // If 'cancel_after_reads' is set in the metadata AND non-zero, the server
  // will cancel the RPC (by just returning Status::CANCELLED - doesn't call
  // ServerContext::TryCancel()) after reading the number of records specified
  // by the 'cancel_after_reads' value set in the metadata.
  int cancel_after_reads = GetIntValueFromMetadata(
      kServerCancelAfterReads, context->client_metadata(), 0);

  EchoRequest request;
  response->set_message("");

  if (server_try_cancel == CANCEL_BEFORE_PROCESSING) {
    ServerTryCancel(context);
    return Status::CANCELLED;
  }

  std::thread* server_try_cancel_thd = nullptr;
  if (server_try_cancel == CANCEL_DURING_PROCESSING) {
    server_try_cancel_thd =
        new std::thread(&TestServiceImpl::ServerTryCancel, this, context);
  }

  int num_msgs_read = 0;
  while (reader->Read(&request)) {
    if (cancel_after_reads == 1) {
      gpr_log(GPR_INFO, "return cancel status");
      return Status::CANCELLED;
    } else if (cancel_after_reads > 0) {
      cancel_after_reads--;
    }
    response->mutable_message()->append(request.message());
  }
  gpr_log(GPR_INFO, "Read: %d messages", num_msgs_read);

  if (server_try_cancel_thd != nullptr) {
    server_try_cancel_thd->join();
    delete server_try_cancel_thd;
    return Status::CANCELLED;
  }

  if (server_try_cancel == CANCEL_AFTER_PROCESSING) {
    ServerTryCancel(context);
    return Status::CANCELLED;
  }

  return Status::OK;
}

// Return 'kNumResponseStreamMsgs' messages.
// TODO(yangg) make it generic by adding a parameter into EchoRequest
Status TestServiceImpl::ResponseStream(ServerContext* context,
                                       const EchoRequest* request,
                                       ServerWriter<EchoResponse>* writer) {
  // If server_try_cancel is set in the metadata, the RPC is cancelled by the
  // server by calling ServerContext::TryCancel() depending on the value:
  //   CANCEL_BEFORE_PROCESSING: The RPC is cancelled before the server writes
  //   any messages to the client
  //   CANCEL_DURING_PROCESSING: The RPC is cancelled while the server is
  //   writing messages to the client
  //   CANCEL_AFTER_PROCESSING: The RPC is cancelled after the server writes
  //   all the messages to the client
  int server_try_cancel = GetIntValueFromMetadata(
      kServerTryCancelRequest, context->client_metadata(), DO_NOT_CANCEL);

  int server_coalescing_api = GetIntValueFromMetadata(
      kServerUseCoalescingApi, context->client_metadata(), 0);

  if (server_try_cancel == CANCEL_BEFORE_PROCESSING) {
    ServerTryCancel(context);
    return Status::CANCELLED;
  }

  EchoResponse response;
  std::thread* server_try_cancel_thd = nullptr;
  if (server_try_cancel == CANCEL_DURING_PROCESSING) {
    server_try_cancel_thd =
        new std::thread(&TestServiceImpl::ServerTryCancel, this, context);
  }

  for (int i = 0; i < kNumResponseStreamsMsgs; i++) {
    response.set_message(request->message() + grpc::to_string(i));
    if (i == kNumResponseStreamsMsgs - 1 && server_coalescing_api != 0) {
      writer->WriteLast(response, WriteOptions());
    } else {
      writer->Write(response);
    }
  }

  if (server_try_cancel_thd != nullptr) {
    server_try_cancel_thd->join();
    delete server_try_cancel_thd;
    return Status::CANCELLED;
  }

  if (server_try_cancel == CANCEL_AFTER_PROCESSING) {
    ServerTryCancel(context);
    return Status::CANCELLED;
  }

  return Status::OK;
}

Status TestServiceImpl::BidiStream(
    ServerContext* context,
    ServerReaderWriter<EchoResponse, EchoRequest>* stream) {
  // If server_try_cancel is set in the metadata, the RPC is cancelled by the
  // server by calling ServerContext::TryCancel() depending on the value:
  //   CANCEL_BEFORE_PROCESSING: The RPC is cancelled before the server reads/
  //   writes any messages from/to the client
  //   CANCEL_DURING_PROCESSING: The RPC is cancelled while the server is
  //   reading/writing messages from/to the client
  //   CANCEL_AFTER_PROCESSING: The RPC is cancelled after the server
  //   reads/writes all messages from/to the client
  int server_try_cancel = GetIntValueFromMetadata(
      kServerTryCancelRequest, context->client_metadata(), DO_NOT_CANCEL);

  EchoRequest request;
  EchoResponse response;

  if (server_try_cancel == CANCEL_BEFORE_PROCESSING) {
    ServerTryCancel(context);
    return Status::CANCELLED;
  }

  std::thread* server_try_cancel_thd = nullptr;
  if (server_try_cancel == CANCEL_DURING_PROCESSING) {
    server_try_cancel_thd =
        new std::thread(&TestServiceImpl::ServerTryCancel, this, context);
  }

  // kServerFinishAfterNReads suggests after how many reads, the server should
  // write the last message and send status (coalesced using WriteLast)
  int server_write_last = GetIntValueFromMetadata(
      kServerFinishAfterNReads, context->client_metadata(), 0);

  int read_counts = 0;
  while (stream->Read(&request)) {
    read_counts++;
    gpr_log(GPR_INFO, "recv msg %s", request.message().c_str());
    response.set_message(request.message());
    if (read_counts == server_write_last) {
      stream->WriteLast(response, WriteOptions());
    } else {
      stream->Write(response);
    }
  }

  if (server_try_cancel_thd != nullptr) {
    server_try_cancel_thd->join();
    delete server_try_cancel_thd;
    return Status::CANCELLED;
  }

  if (server_try_cancel == CANCEL_AFTER_PROCESSING) {
    ServerTryCancel(context);
    return Status::CANCELLED;
  }

  return Status::OK;
}

int TestServiceImpl::GetIntValueFromMetadata(
    const char* key,
    const std::multimap<grpc::string_ref, grpc::string_ref>& metadata,
    int default_value) {
  if (metadata.find(key) != metadata.end()) {
    std::istringstream iss(ToString(metadata.find(key)->second));
    iss >> default_value;
    gpr_log(GPR_INFO, "%s : %d", key, default_value);
  }

  return default_value;
}

void TestServiceImpl::ServerTryCancel(ServerContext* context) {
  EXPECT_FALSE(context->IsCancelled());
  context->TryCancel();
  gpr_log(GPR_INFO, "Server called TryCancel() to cancel the request");
  // Now wait until it's really canceled
  while (!context->IsCancelled()) {
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                 gpr_time_from_micros(1000, GPR_TIMESPAN)));
  }
}

}  // namespace testing
}  // namespace grpc
