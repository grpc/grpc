//
//
// Copyright 2015-2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/grpc.h>
#include <grpc/support/time.h>
#include <grpcpp/ext/call_metric_recorder.h>
#include <grpcpp/ext/orca_service.h>
#include <grpcpp/ext/server_metric_recorder.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include <fstream>
#include <memory>
#include <sstream>
#include <thread>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/util/crash.h"
#include "src/core/util/string.h"
#include "src/core/util/sync.h"
#include "src/proto/grpc/testing/empty.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "src/proto/grpc/testing/test.grpc.pb.h"
#include "test/cpp/interop/server_helper.h"
#include "test/cpp/util/test_config.h"

ABSL_FLAG(bool, use_alts, false,
          "Whether to use alts. Enable alts will disable tls.");
ABSL_FLAG(bool, use_tls, false, "Whether to use tls.");
ABSL_FLAG(std::string, custom_credentials_type, "",
          "User provided credentials type.");
ABSL_FLAG(int32_t, port, 0, "Server port.");
ABSL_FLAG(int32_t, max_send_message_size, -1, "The maximum send message size.");

using grpc::Server;
using grpc::ServerContext;
using grpc::ServerCredentials;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using grpc::WriteOptions;
using grpc::testing::InteropServerContextInspector;
using grpc::testing::Payload;
using grpc::testing::SimpleRequest;
using grpc::testing::SimpleResponse;
using grpc::testing::StreamingInputCallRequest;
using grpc::testing::StreamingInputCallResponse;
using grpc::testing::StreamingOutputCallRequest;
using grpc::testing::StreamingOutputCallResponse;
using grpc::testing::TestService;

const char kEchoInitialMetadataKey[] = "x-grpc-test-echo-initial";
const char kEchoTrailingBinMetadataKey[] = "x-grpc-test-echo-trailing-bin";
const char kEchoUserAgentKey[] = "x-grpc-test-echo-useragent";

void MaybeEchoMetadata(ServerContext* context) {
  const auto& client_metadata = context->client_metadata();
  CHECK_LE(client_metadata.count(kEchoInitialMetadataKey), 1u);
  CHECK_LE(client_metadata.count(kEchoTrailingBinMetadataKey), 1u);

  auto iter = client_metadata.find(kEchoInitialMetadataKey);
  if (iter != client_metadata.end()) {
    context->AddInitialMetadata(
        kEchoInitialMetadataKey,
        std::string(iter->second.begin(), iter->second.end()));
  }
  iter = client_metadata.find(kEchoTrailingBinMetadataKey);
  if (iter != client_metadata.end()) {
    context->AddTrailingMetadata(
        kEchoTrailingBinMetadataKey,
        std::string(iter->second.begin(), iter->second.end()));
  }
  // Check if client sent a magic key in the header that makes us echo
  // back the user-agent (for testing purpose)
  iter = client_metadata.find(kEchoUserAgentKey);
  if (iter != client_metadata.end()) {
    iter = client_metadata.find("user-agent");
    if (iter != client_metadata.end()) {
      context->AddInitialMetadata(
          kEchoUserAgentKey,
          std::string(iter->second.begin(), iter->second.end()));
    }
  }
}

bool SetPayload(int size, Payload* payload) {
  std::unique_ptr<char[]> body(new char[size]());
  payload->set_body(body.get(), size);
  return true;
}

bool CheckExpectedCompression(const ServerContext& context,
                              const bool compression_expected) {
  const InteropServerContextInspector inspector(context);
  const grpc_compression_algorithm received_compression =
      inspector.GetCallCompressionAlgorithm();

  if (compression_expected) {
    if (received_compression == GRPC_COMPRESS_NONE) {
      // Expected some compression, got NONE. This is an error.
      LOG(ERROR)
          << "Expected compression but got uncompressed request from client.";
      return false;
    }
    if (!(inspector.WasCompressed())) {
      LOG(ERROR) << "Failure: Requested compression in a compressable request, "
                    "but compression bit in message flags not set.";
      return false;
    }
  } else {
    // Didn't expect compression -> make sure the request is uncompressed
    if (inspector.WasCompressed()) {
      LOG(ERROR) << "Failure: Didn't requested compression, but compression "
                    "bit in message flags set.";
      return false;
    }
  }
  return true;
}

void RecordCallMetrics(ServerContext* context,
                       const grpc::testing::TestOrcaReport& request_metrics) {
  auto recorder = context->ExperimentalGetCallMetricRecorder();
  // Do not record when zero since it indicates no test per-call report.
  if (request_metrics.cpu_utilization() > 0) {
    recorder->RecordCpuUtilizationMetric(request_metrics.cpu_utilization());
  }
  if (request_metrics.memory_utilization() > 0) {
    recorder->RecordMemoryUtilizationMetric(
        request_metrics.memory_utilization());
  }
  for (const auto& p : request_metrics.request_cost()) {
    char* key = static_cast<char*>(
        grpc_call_arena_alloc(context->c_call(), p.first.size() + 1));
    strncpy(key, p.first.data(), p.first.size());
    key[p.first.size()] = '\0';
    recorder->RecordRequestCostMetric(key, p.second);
  }
  for (const auto& p : request_metrics.utilization()) {
    char* key = static_cast<char*>(
        grpc_call_arena_alloc(context->c_call(), p.first.size() + 1));
    strncpy(key, p.first.data(), p.first.size());
    key[p.first.size()] = '\0';
    recorder->RecordUtilizationMetric(key, p.second);
  }
}

class TestServiceImpl : public TestService::Service {
 public:
  explicit TestServiceImpl(
      grpc::experimental::ServerMetricRecorder* server_metric_recorder)
      : server_metric_recorder_(server_metric_recorder) {}

  Status EmptyCall(ServerContext* context,
                   const grpc::testing::Empty* /*request*/,
                   grpc::testing::Empty* /*response*/) override {
    MaybeEchoMetadata(context);
    return Status::OK;
  }

  // Response contains current timestamp. We ignore everything in the request.
  Status CacheableUnaryCall(ServerContext* context,
                            const SimpleRequest* /*request*/,
                            SimpleResponse* response) override {
    gpr_timespec ts = gpr_now(GPR_CLOCK_MONOTONIC);
    std::string timestamp = std::to_string(ts.tv_nsec);
    response->mutable_payload()->set_body(timestamp.c_str(), timestamp.size());
    context->AddInitialMetadata("cache-control", "max-age=60, public");
    return Status::OK;
  }

  Status UnaryCall(ServerContext* context, const SimpleRequest* request,
                   SimpleResponse* response) override {
    MaybeEchoMetadata(context);
    if (request->has_response_compressed()) {
      const bool compression_requested = request->response_compressed().value();
      VLOG(2) << "Request for compression ("
              << (compression_requested ? "enabled" : "disabled")
              << ") present for " << __func__;
      if (compression_requested) {
        // Any level would do, let's go for HIGH because we are overachievers.
        context->set_compression_level(GRPC_COMPRESS_LEVEL_HIGH);
      } else {
        context->set_compression_level(GRPC_COMPRESS_LEVEL_NONE);
      }
    }
    if (!CheckExpectedCompression(*context,
                                  request->expect_compressed().value())) {
      return Status(grpc::StatusCode::INVALID_ARGUMENT,
                    "Compressed request expectation not met.");
    }
    if (request->response_size() > 0) {
      if (!SetPayload(request->response_size(), response->mutable_payload())) {
        return Status(grpc::StatusCode::INVALID_ARGUMENT,
                      "Error creating payload.");
      }
    }

    if (request->has_response_status()) {
      return Status(
          static_cast<grpc::StatusCode>(request->response_status().code()),
          request->response_status().message());
    }
    if (request->has_orca_per_query_report()) {
      RecordCallMetrics(context, request->orca_per_query_report());
    }
    return Status::OK;
  }

  Status StreamingOutputCall(
      ServerContext* context, const StreamingOutputCallRequest* request,
      ServerWriter<StreamingOutputCallResponse>* writer) override {
    StreamingOutputCallResponse response;
    bool write_success = true;
    for (int i = 0; write_success && i < request->response_parameters_size();
         i++) {
      if (!SetPayload(request->response_parameters(i).size(),
                      response.mutable_payload())) {
        return Status(grpc::StatusCode::INVALID_ARGUMENT,
                      "Error creating payload.");
      }
      WriteOptions wopts;
      if (request->response_parameters(i).has_compressed()) {
        // Compress by default. Disabled on a per-message basis.
        context->set_compression_level(GRPC_COMPRESS_LEVEL_HIGH);
        const bool compression_requested =
            request->response_parameters(i).compressed().value();
        VLOG(2) << "Request for compression ("
                << (compression_requested ? "enabled" : "disabled")
                << ") present for " << __func__;
        if (!compression_requested) {
          wopts.set_no_compression();
        }  // else, compression is already enabled via the context.
      }
      int time_us;
      if ((time_us = request->response_parameters(i).interval_us()) > 0) {
        // Sleep before response if needed
        gpr_timespec sleep_time =
            gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                         gpr_time_from_micros(time_us, GPR_TIMESPAN));
        gpr_sleep_until(sleep_time);
      }
      write_success = writer->Write(response, wopts);
    }
    if (write_success) {
      return Status::OK;
    } else {
      return Status(grpc::StatusCode::INTERNAL, "Error writing response.");
    }
  }

  Status StreamingInputCall(ServerContext* context,
                            ServerReader<StreamingInputCallRequest>* reader,
                            StreamingInputCallResponse* response) override {
    StreamingInputCallRequest request;
    int aggregated_payload_size = 0;
    while (reader->Read(&request)) {
      if (!CheckExpectedCompression(*context,
                                    request.expect_compressed().value())) {
        return Status(grpc::StatusCode::INVALID_ARGUMENT,
                      "Compressed request expectation not met.");
      }
      if (request.has_payload()) {
        aggregated_payload_size += request.payload().body().size();
      }
    }
    response->set_aggregated_payload_size(aggregated_payload_size);
    return Status::OK;
  }

  Status FullDuplexCall(
      ServerContext* context,
      ServerReaderWriter<StreamingOutputCallResponse,
                         StreamingOutputCallRequest>* stream) override {
    MaybeEchoMetadata(context);
    StreamingOutputCallRequest request;
    StreamingOutputCallResponse response;
    bool write_success = true;
    std::unique_ptr<grpc_core::MutexLock> orca_oob_lock;
    while (write_success && stream->Read(&request)) {
      if (request.has_response_status()) {
        return Status(
            static_cast<grpc::StatusCode>(request.response_status().code()),
            request.response_status().message());
      }
      if (request.response_parameters_size() != 0) {
        response.mutable_payload()->set_type(request.payload().type());
        response.mutable_payload()->set_body(
            std::string(request.response_parameters(0).size(), '\0'));
        int time_us;
        if ((time_us = request.response_parameters(0).interval_us()) > 0) {
          // Sleep before response if needed
          gpr_timespec sleep_time =
              gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                           gpr_time_from_micros(time_us, GPR_TIMESPAN));
          gpr_sleep_until(sleep_time);
        }
        write_success = stream->Write(response);
      }
      if (request.has_orca_oob_report()) {
        if (orca_oob_lock == nullptr) {
          orca_oob_lock =
              std::make_unique<grpc_core::MutexLock>(&orca_oob_server_mu_);
          server_metric_recorder_->ClearCpuUtilization();
          server_metric_recorder_->ClearEps();
          server_metric_recorder_->ClearMemoryUtilization();
          server_metric_recorder_->SetAllNamedUtilization({});
          server_metric_recorder_->ClearQps();
        }
        RecordServerMetrics(request.orca_oob_report());
      }
    }
    if (write_success) {
      return Status::OK;
    } else {
      return Status(grpc::StatusCode::INTERNAL, "Error writing response.");
    }
  }

  Status HalfDuplexCall(
      ServerContext* /*context*/,
      ServerReaderWriter<StreamingOutputCallResponse,
                         StreamingOutputCallRequest>* stream) override {
    std::vector<StreamingOutputCallRequest> requests;
    StreamingOutputCallRequest request;
    while (stream->Read(&request)) {
      requests.push_back(request);
    }

    StreamingOutputCallResponse response;
    bool write_success = true;
    for (unsigned int i = 0; write_success && i < requests.size(); i++) {
      response.mutable_payload()->set_type(requests[i].payload().type());
      if (requests[i].response_parameters_size() == 0) {
        return Status(grpc::StatusCode::INTERNAL,
                      "Request does not have response parameters.");
      }
      response.mutable_payload()->set_body(
          std::string(requests[i].response_parameters(0).size(), '\0'));
      write_success = stream->Write(response);
    }
    if (write_success) {
      return Status::OK;
    } else {
      return Status(grpc::StatusCode::INTERNAL, "Error writing response.");
    }
  }

 private:
  void RecordServerMetrics(
      const grpc::testing::TestOrcaReport& request_metrics) {
    // Do not record when zero since it indicates no test per-call report.
    if (request_metrics.cpu_utilization() > 0) {
      server_metric_recorder_->SetCpuUtilization(
          request_metrics.cpu_utilization());
    }
    if (request_metrics.memory_utilization() > 0) {
      server_metric_recorder_->SetMemoryUtilization(
          request_metrics.memory_utilization());
    }
    grpc_core::MutexLock lock(&retained_utilization_names_mu_);
    std::map<grpc::string_ref, double> named_utilizations;
    for (const auto& p : request_metrics.utilization()) {
      const auto& key = *retained_utilization_names_.insert(p.first).first;
      named_utilizations.emplace(key, p.second);
    }
    server_metric_recorder_->SetAllNamedUtilization(named_utilizations);
  }

  grpc::experimental::ServerMetricRecorder* server_metric_recorder_;
  std::set<std::string> retained_utilization_names_
      ABSL_GUARDED_BY(retained_utilization_names_mu_);
  grpc_core::Mutex retained_utilization_names_mu_;
  // Only a single client requesting Orca OOB reports is allowed at a time
  grpc_core::Mutex orca_oob_server_mu_;
};

void grpc::testing::interop::RunServer(
    const std::shared_ptr<ServerCredentials>& creds) {
  RunServer(creds, absl::GetFlag(FLAGS_port), nullptr, nullptr);
}

void grpc::testing::interop::RunServer(
    const std::shared_ptr<ServerCredentials>& creds,
    std::unique_ptr<std::vector<std::unique_ptr<ServerBuilderOption>>>
        server_options) {
  RunServer(creds, absl::GetFlag(FLAGS_port), nullptr,
            std::move(server_options));
}

void grpc::testing::interop::RunServer(
    const std::shared_ptr<ServerCredentials>& creds, const int port,
    ServerStartedCondition* server_started_condition) {
  RunServer(creds, port, server_started_condition, nullptr);
}

void grpc::testing::interop::RunServer(
    const std::shared_ptr<ServerCredentials>& creds, const int port,
    ServerStartedCondition* server_started_condition,
    std::unique_ptr<std::vector<std::unique_ptr<ServerBuilderOption>>>
        server_options) {
  CHECK_NE(port, 0);
  std::ostringstream server_address;
  server_address << "0.0.0.0:" << port;
  auto server_metric_recorder =
      grpc::experimental::ServerMetricRecorder::Create();
  TestServiceImpl service(server_metric_recorder.get());
  grpc::experimental::OrcaService orca_service(
      server_metric_recorder.get(),
      experimental::OrcaService::Options().set_min_report_duration(
          absl::Seconds(0.1)));
  ServerBuilder builder;
  builder.RegisterService(&service);
  builder.RegisterService(&orca_service);
  builder.AddListeningPort(server_address.str(), creds);
  if (server_options != nullptr) {
    for (size_t i = 0; i < server_options->size(); i++) {
      builder.SetOption(std::move((*server_options)[i]));
    }
  }
  if (absl::GetFlag(FLAGS_max_send_message_size) >= 0) {
    builder.SetMaxSendMessageSize(absl::GetFlag(FLAGS_max_send_message_size));
  }
  grpc::ServerBuilder::experimental_type(&builder).EnableCallMetricRecording(
      nullptr);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  LOG(INFO) << "Server listening on " << server_address.str();

  // Signal that the server has started.
  if (server_started_condition) {
    std::unique_lock<std::mutex> lock(server_started_condition->mutex);
    server_started_condition->server_started = true;
    server_started_condition->condition.notify_all();
  }

  while (!gpr_atm_no_barrier_load(&g_got_sigint)) {
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                 gpr_time_from_seconds(5, GPR_TIMESPAN)));
  }
}
