// Copyright 2021 gRPC authors.
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

#include "test/core/transport/binder/end2end/echo_service.h"

#include <string>

#include "absl/strings/str_format.h"
#include "absl/time/time.h"

namespace grpc_binder {
namespace end2end_testing {

const absl::string_view EchoServer::kCancelledText = "cancel";
const absl::string_view EchoServer::kTimeoutText = "timeout";
const size_t EchoServer::kServerStreamingCounts = 100;

grpc::Status EchoServer::EchoUnaryCall(grpc::ServerContext* /*context*/,
                                       const EchoRequest* request,
                                       EchoResponse* response) {
  const std::string& data = request->text();
  if (data == kCancelledText) {
    return grpc::Status::CANCELLED;
  }
  if (data == kTimeoutText) {
    absl::SleepFor(absl::Seconds(5));
  }
  response->set_text(data);
  return grpc::Status::OK;
}

grpc::Status EchoServer::EchoServerStreamingCall(
    grpc::ServerContext* /*context*/, const EchoRequest* request,
    grpc::ServerWriter<EchoResponse>* writer) {
  const std::string& data = request->text();
  if (data == kTimeoutText) {
    absl::SleepFor(absl::Seconds(5));
  }
  for (size_t i = 0; i < kServerStreamingCounts; ++i) {
    EchoResponse response;
    response.set_text(absl::StrFormat("%s(%d)", data, i));
    writer->Write(response);
  }
  return grpc::Status::OK;
}

grpc::Status EchoServer::EchoClientStreamingCall(
    grpc::ServerContext* /*context*/, grpc::ServerReader<EchoRequest>* reader,
    EchoResponse* response) {
  EchoRequest request;
  std::string result = "";
  while (reader->Read(&request)) {
    result += request.text();
  }
  response->set_text(result);
  return grpc::Status::OK;
}

grpc::Status EchoServer::EchoBiDirStreamingCall(
    grpc::ServerContext* /*context*/,
    grpc::ServerReaderWriter<EchoResponse, EchoRequest>* stream) {
  EchoRequest request;
  while (stream->Read(&request)) {
    EchoResponse response;
    response.set_text(request.text());
    stream->Write(response);
  }
  return grpc::Status::OK;
}

}  // namespace end2end_testing
}  // namespace grpc_binder
