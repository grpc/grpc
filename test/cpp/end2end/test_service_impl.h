/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_TEST_CPP_END2END_TEST_SERVICE_IMPL_H
#define GRPC_TEST_CPP_END2END_TEST_SERVICE_IMPL_H

#include <condition_variable>
#include <memory>
#include <mutex>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpcpp/alarm.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/server_context.h>
#include <gtest/gtest.h>

#include <string>
#include <thread>

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/cpp/util/string_ref_helper.h"

namespace grpc {
namespace testing {

const int kServerDefaultResponseStreamsToSend = 3;
const char* const kServerResponseStreamsToSend = "server_responses_to_send";
const char* const kServerTryCancelRequest = "server_try_cancel";
const char* const kClientTryCancelRequest = "client_try_cancel";
const char* const kDebugInfoTrailerKey = "debug-info-bin";
const char* const kServerFinishAfterNReads = "server_finish_after_n_reads";
const char* const kServerUseCoalescingApi = "server_use_coalescing_api";
const char* const kCheckClientInitialMetadataKey = "custom_client_metadata";
const char* const kCheckClientInitialMetadataVal = "Value for client metadata";

typedef enum {
  DO_NOT_CANCEL = 0,
  CANCEL_BEFORE_PROCESSING,
  CANCEL_DURING_PROCESSING,
  CANCEL_AFTER_PROCESSING
} ServerTryCancelRequestPhase;

namespace internal {
// When echo_deadline is requested, deadline seen in the ServerContext is set in
// the response in seconds.
void MaybeEchoDeadline(ServerContextBase* context, const EchoRequest* request,
                       EchoResponse* response);

void CheckServerAuthContext(const ServerContextBase* context,
                            const std::string& expected_transport_security_type,
                            const std::string& expected_client_identity);

// Returns the number of pairs in metadata that exactly match the given
// key-value pair. Returns -1 if the pair wasn't found.
int MetadataMatchCount(
    const std::multimap<grpc::string_ref, grpc::string_ref>& metadata,
    const std::string& key, const std::string& value);

int GetIntValueFromMetadataHelper(
    const char* key,
    const std::multimap<grpc::string_ref, grpc::string_ref>& metadata,
    int default_value);

int GetIntValueFromMetadata(
    const char* key,
    const std::multimap<grpc::string_ref, grpc::string_ref>& metadata,
    int default_value);

void ServerTryCancel(ServerContext* context);
}  // namespace internal

class TestServiceSignaller {
 public:
  void ClientWaitUntilRpcStarted() {
    std::unique_lock<std::mutex> lock(mu_);
    cv_rpc_started_.wait(lock, [this] { return rpc_started_; });
  }
  void ServerWaitToContinue() {
    std::unique_lock<std::mutex> lock(mu_);
    cv_server_continue_.wait(lock, [this] { return server_should_continue_; });
  }
  void SignalClientThatRpcStarted() {
    std::unique_lock<std::mutex> lock(mu_);
    rpc_started_ = true;
    cv_rpc_started_.notify_one();
  }
  void SignalServerToContinue() {
    std::unique_lock<std::mutex> lock(mu_);
    server_should_continue_ = true;
    cv_server_continue_.notify_one();
  }

 private:
  std::mutex mu_;
  std::condition_variable cv_rpc_started_;
  bool rpc_started_ /* GUARDED_BY(mu_) */ = false;
  std::condition_variable cv_server_continue_;
  bool server_should_continue_ /* GUARDED_BY(mu_) */ = false;
};

template <typename RpcService>
class TestMultipleServiceImpl : public RpcService {
 public:
  TestMultipleServiceImpl() : signal_client_(false), host_() {}
  explicit TestMultipleServiceImpl(const std::string& host)
      : signal_client_(false), host_(new std::string(host)) {}

  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) {
    if (request->has_param() &&
        request->param().server_notify_client_when_started()) {
      signaller_.SignalClientThatRpcStarted();
      signaller_.ServerWaitToContinue();
    }

    // A bit of sleep to make sure that short deadline tests fail
    if (request->has_param() && request->param().server_sleep_us() > 0) {
      gpr_sleep_until(
          gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                       gpr_time_from_micros(request->param().server_sleep_us(),
                                            GPR_TIMESPAN)));
    }

    if (request->has_param() && request->param().server_die()) {
      gpr_log(GPR_ERROR, "The request should not reach application handler.");
      GPR_ASSERT(0);
    }
    if (request->has_param() && request->param().has_expected_error()) {
      const auto& error = request->param().expected_error();
      return Status(static_cast<StatusCode>(error.code()),
                    error.error_message(), error.binary_error_details());
    }
    int server_try_cancel = internal::GetIntValueFromMetadata(
        kServerTryCancelRequest, context->client_metadata(), DO_NOT_CANCEL);
    if (server_try_cancel > DO_NOT_CANCEL) {
      // Since this is a unary RPC, by the time this server handler is called,
      // the 'request' message is already read from the client. So the scenarios
      // in server_try_cancel don't make much sense. Just cancel the RPC as long
      // as server_try_cancel is not DO_NOT_CANCEL
      internal::ServerTryCancel(context);
      return Status::CANCELLED;
    }

    response->set_message(request->message());
    internal::MaybeEchoDeadline(context, request, response);
    if (host_) {
      response->mutable_param()->set_host(*host_);
    }
    if (request->has_param() && request->param().client_cancel_after_us()) {
      {
        std::unique_lock<std::mutex> lock(mu_);
        signal_client_ = true;
        ++rpcs_waiting_for_client_cancel_;
      }
      while (!context->IsCancelled()) {
        gpr_sleep_until(gpr_time_add(
            gpr_now(GPR_CLOCK_REALTIME),
            gpr_time_from_micros(request->param().client_cancel_after_us(),
                                 GPR_TIMESPAN)));
      }
      {
        std::unique_lock<std::mutex> lock(mu_);
        --rpcs_waiting_for_client_cancel_;
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

    if (request->has_param() && request->param().echo_metadata_initially()) {
      const std::multimap<grpc::string_ref, grpc::string_ref>& client_metadata =
          context->client_metadata();
      for (const auto& metadatum : client_metadata) {
        context->AddInitialMetadata(ToString(metadatum.first),
                                    ToString(metadatum.second));
      }
    }

    if (request->has_param() && request->param().echo_metadata()) {
      const std::multimap<grpc::string_ref, grpc::string_ref>& client_metadata =
          context->client_metadata();
      for (const auto& metadatum : client_metadata) {
        context->AddTrailingMetadata(ToString(metadatum.first),
                                     ToString(metadatum.second));
      }
      // Terminate rpc with error and debug info in trailer.
      if (request->param().debug_info().stack_entries_size() ||
          !request->param().debug_info().detail().empty()) {
        std::string serialized_debug_info =
            request->param().debug_info().SerializeAsString();
        context->AddTrailingMetadata(kDebugInfoTrailerKey,
                                     serialized_debug_info);
        return Status::CANCELLED;
      }
    }
    if (request->has_param() &&
        (request->param().expected_client_identity().length() > 0 ||
         request->param().check_auth_context())) {
      internal::CheckServerAuthContext(
          context, request->param().expected_transport_security_type(),
          request->param().expected_client_identity());
    }
    if (request->has_param() &&
        request->param().response_message_length() > 0) {
      response->set_message(
          std::string(request->param().response_message_length(), '\0'));
    }
    if (request->has_param() && request->param().echo_peer()) {
      response->mutable_param()->set_peer(context->peer());
    }
    return Status::OK;
  }

  Status Echo1(ServerContext* context, const EchoRequest* request,
               EchoResponse* response) {
    return Echo(context, request, response);
  }

  Status Echo2(ServerContext* context, const EchoRequest* request,
               EchoResponse* response) {
    return Echo(context, request, response);
  }

  Status CheckClientInitialMetadata(ServerContext* context,
                                    const SimpleRequest* /*request*/,
                                    SimpleResponse* /*response*/) {
    EXPECT_EQ(internal::MetadataMatchCount(context->client_metadata(),
                                           kCheckClientInitialMetadataKey,
                                           kCheckClientInitialMetadataVal),
              1);
    EXPECT_EQ(1u,
              context->client_metadata().count(kCheckClientInitialMetadataKey));
    return Status::OK;
  }

  // Unimplemented is left unimplemented to test the returned error.

  Status RequestStream(ServerContext* context,
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
    int server_try_cancel = internal::GetIntValueFromMetadata(
        kServerTryCancelRequest, context->client_metadata(), DO_NOT_CANCEL);

    EchoRequest request;
    response->set_message("");

    if (server_try_cancel == CANCEL_BEFORE_PROCESSING) {
      internal::ServerTryCancel(context);
      return Status::CANCELLED;
    }

    std::thread* server_try_cancel_thd = nullptr;
    if (server_try_cancel == CANCEL_DURING_PROCESSING) {
      server_try_cancel_thd =
          new std::thread([context] { internal::ServerTryCancel(context); });
    }

    int num_msgs_read = 0;
    while (reader->Read(&request)) {
      response->mutable_message()->append(request.message());
    }
    gpr_log(GPR_INFO, "Read: %d messages", num_msgs_read);

    if (server_try_cancel_thd != nullptr) {
      server_try_cancel_thd->join();
      delete server_try_cancel_thd;
      return Status::CANCELLED;
    }

    if (server_try_cancel == CANCEL_AFTER_PROCESSING) {
      internal::ServerTryCancel(context);
      return Status::CANCELLED;
    }

    return Status::OK;
  }

  // Return 'kNumResponseStreamMsgs' messages.
  // TODO(yangg) make it generic by adding a parameter into EchoRequest
  Status ResponseStream(ServerContext* context, const EchoRequest* request,
                        ServerWriter<EchoResponse>* writer) {
    // If server_try_cancel is set in the metadata, the RPC is cancelled by the
    // server by calling ServerContext::TryCancel() depending on the value:
    //   CANCEL_BEFORE_PROCESSING: The RPC is cancelled before the server writes
    //   any messages to the client
    //   CANCEL_DURING_PROCESSING: The RPC is cancelled while the server is
    //   writing messages to the client
    //   CANCEL_AFTER_PROCESSING: The RPC is cancelled after the server writes
    //   all the messages to the client
    int server_try_cancel = internal::GetIntValueFromMetadata(
        kServerTryCancelRequest, context->client_metadata(), DO_NOT_CANCEL);

    int server_coalescing_api = internal::GetIntValueFromMetadata(
        kServerUseCoalescingApi, context->client_metadata(), 0);

    int server_responses_to_send = internal::GetIntValueFromMetadata(
        kServerResponseStreamsToSend, context->client_metadata(),
        kServerDefaultResponseStreamsToSend);

    if (server_try_cancel == CANCEL_BEFORE_PROCESSING) {
      internal::ServerTryCancel(context);
      return Status::CANCELLED;
    }

    EchoResponse response;
    std::thread* server_try_cancel_thd = nullptr;
    if (server_try_cancel == CANCEL_DURING_PROCESSING) {
      server_try_cancel_thd =
          new std::thread([context] { internal::ServerTryCancel(context); });
    }

    for (int i = 0; i < server_responses_to_send; i++) {
      response.set_message(request->message() + std::to_string(i));
      if (i == server_responses_to_send - 1 && server_coalescing_api != 0) {
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
      internal::ServerTryCancel(context);
      return Status::CANCELLED;
    }

    return Status::OK;
  }

  Status BidiStream(ServerContext* context,
                    ServerReaderWriter<EchoResponse, EchoRequest>* stream) {
    // If server_try_cancel is set in the metadata, the RPC is cancelled by the
    // server by calling ServerContext::TryCancel() depending on the value:
    //   CANCEL_BEFORE_PROCESSING: The RPC is cancelled before the server reads/
    //   writes any messages from/to the client
    //   CANCEL_DURING_PROCESSING: The RPC is cancelled while the server is
    //   reading/writing messages from/to the client
    //   CANCEL_AFTER_PROCESSING: The RPC is cancelled after the server
    //   reads/writes all messages from/to the client
    int server_try_cancel = internal::GetIntValueFromMetadata(
        kServerTryCancelRequest, context->client_metadata(), DO_NOT_CANCEL);

    EchoRequest request;
    EchoResponse response;

    if (server_try_cancel == CANCEL_BEFORE_PROCESSING) {
      internal::ServerTryCancel(context);
      return Status::CANCELLED;
    }

    std::thread* server_try_cancel_thd = nullptr;
    if (server_try_cancel == CANCEL_DURING_PROCESSING) {
      server_try_cancel_thd =
          new std::thread([context] { internal::ServerTryCancel(context); });
    }

    // kServerFinishAfterNReads suggests after how many reads, the server should
    // write the last message and send status (coalesced using WriteLast)
    int server_write_last = internal::GetIntValueFromMetadata(
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
      internal::ServerTryCancel(context);
      return Status::CANCELLED;
    }

    return Status::OK;
  }

  // Unimplemented is left unimplemented to test the returned error.
  bool signal_client() {
    std::unique_lock<std::mutex> lock(mu_);
    return signal_client_;
  }
  void ClientWaitUntilRpcStarted() { signaller_.ClientWaitUntilRpcStarted(); }
  void SignalServerToContinue() { signaller_.SignalServerToContinue(); }
  uint64_t RpcsWaitingForClientCancel() {
    std::unique_lock<std::mutex> lock(mu_);
    return rpcs_waiting_for_client_cancel_;
  }

 private:
  bool signal_client_;
  std::mutex mu_;
  TestServiceSignaller signaller_;
  std::unique_ptr<std::string> host_;
  uint64_t rpcs_waiting_for_client_cancel_ = 0;
};

class CallbackTestServiceImpl
    : public ::grpc::testing::EchoTestService::CallbackService {
 public:
  CallbackTestServiceImpl() : signal_client_(false), host_() {}
  explicit CallbackTestServiceImpl(const std::string& host)
      : signal_client_(false), host_(new std::string(host)) {}

  ServerUnaryReactor* Echo(CallbackServerContext* context,
                           const EchoRequest* request,
                           EchoResponse* response) override;

  ServerUnaryReactor* CheckClientInitialMetadata(CallbackServerContext* context,
                                                 const SimpleRequest*,
                                                 SimpleResponse*) override;

  ServerReadReactor<EchoRequest>* RequestStream(
      CallbackServerContext* context, EchoResponse* response) override;

  ServerWriteReactor<EchoResponse>* ResponseStream(
      CallbackServerContext* context, const EchoRequest* request) override;

  ServerBidiReactor<EchoRequest, EchoResponse>* BidiStream(
      CallbackServerContext* context) override;

  // Unimplemented is left unimplemented to test the returned error.
  bool signal_client() {
    std::unique_lock<std::mutex> lock(mu_);
    return signal_client_;
  }
  void ClientWaitUntilRpcStarted() { signaller_.ClientWaitUntilRpcStarted(); }
  void SignalServerToContinue() { signaller_.SignalServerToContinue(); }

 private:
  bool signal_client_;
  std::mutex mu_;
  TestServiceSignaller signaller_;
  std::unique_ptr<std::string> host_;
};

using TestServiceImpl =
    TestMultipleServiceImpl<::grpc::testing::EchoTestService::Service>;

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_END2END_TEST_SERVICE_IMPL_H
