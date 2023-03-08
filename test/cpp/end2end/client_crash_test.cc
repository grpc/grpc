//
//
// Copyright 2015 gRPC authors.
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

#include <gtest/gtest.h>

#include "absl/memory/memory.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/subprocess.h"

static std::string g_root;

namespace grpc {
namespace testing {

namespace {

class CrashTest : public ::testing::Test {
 protected:
  CrashTest() {}

  std::unique_ptr<grpc::testing::EchoTestService::Stub> CreateServerAndStub() {
    auto port = grpc_pick_unused_port_or_die();
    std::ostringstream addr_stream;
    addr_stream << "localhost:" << port;
    auto addr = addr_stream.str();
    server_ = std::make_unique<SubProcess>(std::vector<std::string>({
        g_root + "/client_crash_test_server",
        "--address=" + addr,
    }));
    GPR_ASSERT(server_);
    return grpc::testing::EchoTestService::NewStub(
        grpc::CreateChannel(addr, InsecureChannelCredentials()));
  }

  void KillServer() { server_.reset(); }

 private:
  std::unique_ptr<SubProcess> server_;
};

TEST_F(CrashTest, KillBeforeWrite) {
  auto stub = CreateServerAndStub();

  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  context.set_wait_for_ready(true);

  auto stream = stub->BidiStream(&context);

  request.set_message("Hello");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), request.message());

  KillServer();

  request.set_message("You should be dead");
  // This may succeed or fail depending on the state of the TCP connection
  stream->Write(request);
  // But the read will definitely fail
  EXPECT_FALSE(stream->Read(&response));

  EXPECT_FALSE(stream->Finish().ok());
}

TEST_F(CrashTest, KillAfterWrite) {
  auto stub = CreateServerAndStub();

  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  context.set_wait_for_ready(true);

  auto stream = stub->BidiStream(&context);

  request.set_message("Hello");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), request.message());

  request.set_message("I'm going to kill you");
  EXPECT_TRUE(stream->Write(request));

  KillServer();

  // This may succeed or fail depending on how quick the server was
  stream->Read(&response);

  EXPECT_FALSE(stream->Finish().ok());
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

  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  // Order seems to matter on these tests: run three times to eliminate that
  for (int i = 0; i < 3; i++) {
    if (RUN_ALL_TESTS() != 0) {
      return 1;
    }
  }
  return 0;
}
