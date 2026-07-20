//
//
// Copyright 2026 gRPC authors.
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

#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"

#include <google/protobuf/json/json.h>
#include <grpcpp/grpcpp.h>
#include "opentelemetry/proto/collector/trace/v1/trace_service.grpc.pb.h"

ABSL_FLAG(int, port, 0, "Port to listen on");
ABSL_FLAG(std::string, file, "", "File to write JSON spans to");

class TraceServiceServiceImpl final
    : public opentelemetry::proto::collector::trace::v1::TraceService::Service {
 public:
  explicit TraceServiceServiceImpl(std::string file_path)
      : file_path_(std::move(file_path)) {}

  grpc::Status Export(
      grpc::ServerContext* /*context*/,
      const opentelemetry::proto::collector::trace::v1::
          ExportTraceServiceRequest* request,
      opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse*
      /*response*/) override {
    std::string json_string;
    google::protobuf::json::PrintOptions options;
    options.add_whitespace = true;
    options.always_print_fields_with_no_presence = true;
    options.preserve_proto_field_names = true;
    auto status = google::protobuf::json::MessageToJsonString(
        *request, &json_string, options);
    if (!status.ok()) {
      LOG(ERROR) << "Failed to serialize ExportTraceServiceRequest to JSON: "
                 << status.ToString();
      return grpc::Status(grpc::StatusCode::INTERNAL,
                          "Failed to serialize to JSON");
    }

    std::lock_guard<std::mutex> lock(mu_);
    requests_json_.push_back(json_string);

    // Write to a temporary file first and rename to avoid read-during-write data race
    std::string tmp_file = file_path_ + ".tmp";
    std::ofstream out(tmp_file, std::ios::trunc);
    if (!out) {
      LOG(ERROR) << "Failed to open file for writing: " << tmp_file;
      return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to open file");
    }
    out << "[\n";
    for (size_t i = 0; i < requests_json_.size(); ++i) {
      out << requests_json_[i];
      if (i + 1 < requests_json_.size()) {
        out << ",\n";
      }
    }
    out << "\n]\n";
    if (out.fail() || out.bad()) {
      LOG(ERROR) << "Failed to write JSON spans to file: " << tmp_file;
      return grpc::Status(grpc::StatusCode::INTERNAL,
                          "Failed to write trace output");
    }
    out.close();

    if (std::rename(tmp_file.c_str(), file_path_.c_str()) != 0) {
      LOG(ERROR) << "Failed to rename temporary file " << tmp_file << " to "
                 << file_path_;
      return grpc::Status(grpc::StatusCode::INTERNAL,
                          "Failed to update trace output file");
    }

    return grpc::Status::OK;
  }

 private:
  std::string file_path_;
  std::vector<std::string> requests_json_;
  std::mutex mu_;
};

static std::atomic<bool> g_got_sigint{false};

static void sig_handler(int /*sig*/) {
  g_got_sigint.store(true, std::memory_order_relaxed);
}

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  int port = absl::GetFlag(FLAGS_port);
  std::string file_path = absl::GetFlag(FLAGS_file);

  if (port == 0) {
    LOG(ERROR) << "--port is required";
    return 1;
  }
  if (file_path.empty()) {
    LOG(ERROR) << "--file is required";
    return 1;
  }

  TraceServiceServiceImpl service(file_path);

  grpc::ServerBuilder builder;
  builder.AddListeningPort("0.0.0.0:" + std::to_string(port),
                           grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  signal(SIGINT, sig_handler);
  signal(SIGTERM, sig_handler);

  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  LOG(INFO) << "OTLP Collector listening on port " << port << "...";
  while (!g_got_sigint.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  server->Shutdown();
  server.reset();

  return 0;
}
