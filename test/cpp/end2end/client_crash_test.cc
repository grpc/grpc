/*
 *
 * Copyright 2015, Google Inc.
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

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <gtest/gtest.h>

#include "src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/subprocess.h"

using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;
using std::chrono::system_clock;

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
    server_.reset(new SubProcess({
        g_root + "/client_crash_test_server", "--address=" + addr,
    }));
    GPR_ASSERT(server_);
    return grpc::testing::EchoTestService::NewStub(
        CreateChannel(addr, InsecureChannelCredentials()));
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

  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  // Order seems to matter on these tests: run three times to eliminate that
  for (int i = 0; i < 3; i++) {
    if (RUN_ALL_TESTS() != 0) {
      return 1;
    }
  }
  return 0;
}
