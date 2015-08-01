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

#include <mutex>
#include <thread>

#include "src/core/security/credentials.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include <grpc++/channel_arguments.h>
#include <grpc++/channel_interface.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/credentials.h>
#include <grpc++/fixed_size_thread_pool.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/server_credentials.h>
#include <grpc++/status.h>
#include <grpc++/stream.h>
#include <grpc++/time.h>
#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>

#ifdef GRPC_ENDOSCOPE_PROFILER

#include "src/cpp/profiling/endoscope_service.h"

using perftools::endoscope::EndoRequestPB;
using perftools::endoscope::EndoSnapshotPB;
using perftools::endoscope::Endoscope;
using grpc::endoscope::EndoscopeService;

namespace grpc {
namespace testing {

class EndoscopeServerTest : public ::testing::Test {
 protected:
  void SetUp() GRPC_OVERRIDE {
    int port = grpc_pick_unused_port_or_die();
    server_address_ << "localhost:" << port;
    // Setup server
    ServerBuilder builder;
    SslServerCredentialsOptions::PemKeyCertPair pkcp = {test_server1_key,
      test_server1_cert};
    SslServerCredentialsOptions ssl_opts;
    ssl_opts.pem_root_certs = "";
    ssl_opts.pem_key_cert_pairs.push_back(pkcp);
    builder.AddListeningPort(server_address_.str(),
                             SslServerCredentials(ssl_opts));
    builder.RegisterService(&endoscopeservice_);
    server_ = builder.BuildAndStart();
  }

  void TearDown() GRPC_OVERRIDE { server_->Shutdown(); }

  void ResetStub() {
    SslCredentialsOptions ssl_opts = {test_root_cert, "", ""};
    ChannelArguments args;
    args.SetSslTargetNameOverride("foo.test.google.fr");
    args.SetString(GRPC_ARG_PRIMARY_USER_AGENT_STRING, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/44.0.2403.125 Safari/537.36");
    args.SetString(GRPC_ARG_SECONDARY_USER_AGENT_STRING, "");
    channel_ = CreateChannel(server_address_.str(), SslCredentials(ssl_opts),
                             args);
    stub_ = std::move(Endoscope::NewStub(channel_));
  }

  std::shared_ptr<ChannelInterface> channel_;
  std::unique_ptr<Endoscope::Stub> stub_;
  std::unique_ptr<Server> server_;
  std::ostringstream server_address_;
  EndoscopeService endoscopeservice_;
};

TEST_F(EndoscopeServerTest, BuiltInTagTest) {
  ResetStub();
  EndoRequestPB request;
  EndoSnapshotPB snapshot;

  ClientContext context;
  context.AddMetadata("accept", "*/*");
  context.AddMetadata("accept-language", "en-US,en;q=0.8");
  context.AddMetadata("content-length", "5");
  context.AddMetadata("origin", "https://foo.test.google.fr");
  context.AddMetadata("referer", "https://foo.test.google.fr/endo_console.html");
  Status s = stub_->Action(&context, request, &snapshot);
  // EXPECT_EQ(0, s.error_code());  // error_code == 2 if grpc_status is removed

  EXPECT_GE(snapshot.marker_size(), 2);
  EXPECT_EQ("GRPC_PTAG_CPP_CALL_CREATE", snapshot.marker(0).name());
  EXPECT_EQ("GRPC_PTAG_CPP_CALL_CREATED", snapshot.marker(1).name());

  EXPECT_GE(snapshot.tasks_history_size(), 1);
  EXPECT_EQ(0, snapshot.tasks_history(0).marker_id());
  EXPECT_EQ(1, snapshot.tasks_history(0).log(0).param());

  EXPECT_GE(snapshot.thread_size(), 1);
  EXPECT_EQ(snapshot.thread(0).thread_id(), snapshot.tasks_history(0).thread_id());
}

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_ENDOSCOPE_PROFILER

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
