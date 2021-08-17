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

#ifndef TEST_CORE_TRANSPORT_BINDER_END2END_ECHO_SERVICE_H
#define TEST_CORE_TRANSPORT_BINDER_END2END_ECHO_SERVICE_H

#include "absl/strings/string_view.h"
#include "test/core/transport/binder/end2end/echo.grpc.pb.h"

namespace grpc_binder {
namespace end2end_testing {

// TODO(waynetu): Replace this with TestServiceImpl declared in
// test/cpp/end2end/test_service_impl.h
class EchoServer final : public EchoService::Service {
 public:
  static const absl::string_view kCancelledText;
  static const absl::string_view kTimeoutText;

  grpc::Status EchoUnaryCall(grpc::ServerContext* context,
                             const EchoRequest* request,
                             EchoResponse* response) override;

  static const size_t kServerStreamingCounts;

  grpc::Status EchoServerStreamingCall(
      grpc::ServerContext* context, const EchoRequest* request,
      grpc::ServerWriter<EchoResponse>* writer) override;
  grpc::Status EchoClientStreamingCall(grpc::ServerContext* context,
                                       grpc::ServerReader<EchoRequest>* reader,
                                       EchoResponse* response) override;
  grpc::Status EchoBiDirStreamingCall(
      grpc::ServerContext* context,
      grpc::ServerReaderWriter<EchoResponse, EchoRequest>* stream) override;
};

}  // namespace end2end_testing
}  // namespace grpc_binder

#endif  // TEST_CORE_TRANSPORT_BINDER_END2END_ECHO_SERVICE_H_
