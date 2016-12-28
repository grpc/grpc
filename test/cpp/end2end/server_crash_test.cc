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

class ServiceImpl final : public ::grpc::testing::EchoTestService::Service {
 public:
  ServiceImpl() : bidi_stream_count_(0), response_stream_count_(0) {}

  Status BidiStream(
      ServerContext* context,
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

  Status ResponseStream(ServerContext* context, const EchoRequest* request,
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
    client_.reset(new SubProcess({g_root + "/server_crash_test_client",
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

  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
