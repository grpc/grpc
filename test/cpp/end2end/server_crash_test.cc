/*
 *
 * Copyright 2015 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "absl/memory/memory.h"

#include "src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/subprocess.h"

#include <gtest/gtest.h>

using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;

static std::string g_root;

namespace grpc {
namespace testing {

namespace {

class ServiceImpl final : public ::grpc::testing::EchoTestService::Service {
 public:
  ServiceImpl() : bidi_stream_count_(0), response_stream_count_(0) {}

  Status BidiStream(
      ServerContext* /*context*/,
      ServerReaderWriter<EchoResponse, EchoRequest>* stream) override {
    bidi_stream_count_++;
    EchoRequest request;
    EchoResponse response;
    while (stream->Read(&request)) {
      gpr_log(GPR_INFO, "recv msg %s", request.message().c_str());
      response.set_message(request.message());
      stream->Write(response);
      gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                   gpr_time_from_seconds(1, GPR_TIMESPAN)));
    }
    return Status::OK;
  }

  Status ResponseStream(ServerContext* /*context*/,
                        const EchoRequest* /*request*/,
                        ServerWriter<EchoResponse>* writer) override {
    EchoResponse response;
    response_stream_count_++;
    for (int i = 0;; i++) {
      std::ostringstream msg;
      msg << "Hello " << i;
      response.set_message(msg.str());
      if (!writer->Write(response)) break;
      gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                   gpr_time_from_seconds(1, GPR_TIMESPAN)));
    }
    return Status::OK;
  }

  int bidi_stream_count() { return bidi_stream_count_; }

  int response_stream_count() { return response_stream_count_; }

 private:
  int bidi_stream_count_;
  int response_stream_count_;
};

class CrashTest : public ::testing::Test {
 protected:
  CrashTest() {}

  std::unique_ptr<Server> CreateServerAndClient(const std::string& mode) {
    auto port = grpc_pick_unused_port_or_die();
    std::ostringstream addr_stream;
    addr_stream << "localhost:" << port;
    auto addr = addr_stream.str();
    client_ = absl::make_unique<SubProcess>(
        std::vector<std::string>({g_root + "/server_crash_test_client",
                                  "--address=" + addr, "--mode=" + mode}));
    GPR_ASSERT(client_);

    ServerBuilder builder;
    builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&service_);
    return builder.BuildAndStart();
  }

  void KillClient() { client_.reset(); }

  bool HadOneBidiStream() { return service_.bidi_stream_count() == 1; }

  bool HadOneResponseStream() { return service_.response_stream_count() == 1; }

 private:
  std::unique_ptr<SubProcess> client_;
  ServiceImpl service_;
};

TEST_F(CrashTest, ResponseStream) {
  auto server = CreateServerAndClient("response");

  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                               gpr_time_from_seconds(60, GPR_TIMESPAN)));
  KillClient();
  server->Shutdown();
  GPR_ASSERT(HadOneResponseStream());
}

TEST_F(CrashTest, BidiStream) {
  auto server = CreateServerAndClient("bidi");

  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                               gpr_time_from_seconds(60, GPR_TIMESPAN)));
  KillClient();
  server->Shutdown();
  GPR_ASSERT(HadOneBidiStream());
}

}  // namespace

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  std::string me = argv[0];
  auto lslash = me.rfind('/');
  if (lslash != std::string::npos) {
    g_root = me.substr(0, lslash);
  } else {
    g_root = ".";
  }

  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
