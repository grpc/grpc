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

#include <memory>
#include <mutex>

#include <grpc/grpc.h>
#include <grpcpp/alarm.h>
#include <grpcpp/server_context.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"

namespace grpc {
namespace testing {

const int kServerDefaultResponseStreamsToSend = 3;
const char* const kServerResponseStreamsToSend = "server_responses_to_send";
const char* const kServerTryCancelRequest = "server_try_cancel";
const char* const kServerUseCancelCallback = "server_use_cancel_callback";
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

typedef enum {
  DO_NOT_USE_CALLBACK = 0,
  MAYBE_USE_CALLBACK_EARLY_CANCEL,
  MAYBE_USE_CALLBACK_LATE_CANCEL,
  MAYBE_USE_CALLBACK_NO_CANCEL,
} ServerUseCancelCallback;

class TestServiceImpl : public ::grpc::testing::EchoTestService::Service {
 public:
  TestServiceImpl() : signal_client_(false), host_() {}
  explicit TestServiceImpl(const grpc::string& host)
      : signal_client_(false), host_(new grpc::string(host)) {}

  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override;

  Status CheckClientInitialMetadata(ServerContext* context,
                                    const SimpleRequest* request,
                                    SimpleResponse* response) override;

  // Unimplemented is left unimplemented to test the returned error.

  Status RequestStream(ServerContext* context,
                       ServerReader<EchoRequest>* reader,
                       EchoResponse* response) override;

  Status ResponseStream(ServerContext* context, const EchoRequest* request,
                        ServerWriter<EchoResponse>* writer) override;

  Status BidiStream(
      ServerContext* context,
      ServerReaderWriter<EchoResponse, EchoRequest>* stream) override;

  bool signal_client() {
    std::unique_lock<std::mutex> lock(mu_);
    return signal_client_;
  }

 private:
  bool signal_client_;
  std::mutex mu_;
  std::unique_ptr<grpc::string> host_;
};

class CallbackTestServiceImpl
    : public ::grpc::testing::EchoTestService::ExperimentalCallbackService {
 public:
  CallbackTestServiceImpl() : signal_client_(false), host_() {}
  explicit CallbackTestServiceImpl(const grpc::string& host)
      : signal_client_(false), host_(new grpc::string(host)) {}

  void Echo(ServerContext* context, const EchoRequest* request,
            EchoResponse* response,
            experimental::ServerCallbackRpcController* controller) override;

  void CheckClientInitialMetadata(
      ServerContext* context, const SimpleRequest* request,
      SimpleResponse* response,
      experimental::ServerCallbackRpcController* controller) override;

  experimental::ServerReadReactor<EchoRequest, EchoResponse>* RequestStream()
      override;

  experimental::ServerWriteReactor<EchoRequest, EchoResponse>* ResponseStream()
      override;

  experimental::ServerBidiReactor<EchoRequest, EchoResponse>* BidiStream()
      override;

  // Unimplemented is left unimplemented to test the returned error.
  bool signal_client() {
    std::unique_lock<std::mutex> lock(mu_);
    return signal_client_;
  }

 private:
  struct CancelState {
    std::atomic_bool callback_invoked{false};
  };
  void EchoNonDelayed(ServerContext* context, const EchoRequest* request,
                      EchoResponse* response,
                      experimental::ServerCallbackRpcController* controller,
                      CancelState* cancel_state);

  Alarm alarm_;
  bool signal_client_;
  std::mutex mu_;
  std::unique_ptr<grpc::string> host_;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_END2END_TEST_SERVICE_IMPL_H
