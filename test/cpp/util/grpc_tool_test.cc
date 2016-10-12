/*
 *
 * Copyright 2016, Google Inc.
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

#include "test/cpp/util/grpc_tool.h"

#include <sstream>

#include <gflags/gflags.h>
#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/ext/proto_server_reflection_plugin.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <gtest/gtest.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "src/proto/grpc/testing/echo.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/cli_credentials.h"
#include "test/cpp/util/string_ref_helper.h"

using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;

#define USAGE_REGEX "(  grpc_cli .+\n){2,10}"

#define ECHO_TEST_SERVICE_SUMMARY \
  "Echo\n"                        \
  "RequestStream\n"               \
  "ResponseStream\n"              \
  "BidiStream\n"                  \
  "Unimplemented\n"

#define ECHO_TEST_SERVICE_DESCRIPTION                                         \
  "filename: src/proto/grpc/testing/echo.proto\n"                             \
  "package: grpc.testing;\n"                                                  \
  "service EchoTestService {\n"                                               \
  "  rpc Echo(grpc.testing.EchoRequest) returns (grpc.testing.EchoResponse) " \
  "{}\n"                                                                      \
  "  rpc RequestStream(stream grpc.testing.EchoRequest) returns "             \
  "(grpc.testing.EchoResponse) {}\n"                                          \
  "  rpc ResponseStream(grpc.testing.EchoRequest) returns (stream "           \
  "grpc.testing.EchoResponse) {}\n"                                           \
  "  rpc BidiStream(stream grpc.testing.EchoRequest) returns (stream "        \
  "grpc.testing.EchoResponse) {}\n"                                           \
  "  rpc Unimplemented(grpc.testing.EchoRequest) returns "                    \
  "(grpc.testing.EchoResponse) {}\n"                                          \
  "}\n"                                                                       \
  "\n"

#define ECHO_METHOD_DESCRIPTION                                               \
  "  rpc Echo(grpc.testing.EchoRequest) returns (grpc.testing.EchoResponse) " \
  "{}\n"

namespace grpc {
namespace testing {

DECLARE_bool(l);

namespace {

class TestCliCredentials GRPC_FINAL : public grpc::testing::CliCredentials {
 public:
  std::shared_ptr<grpc::ChannelCredentials> GetCredentials() const
      GRPC_OVERRIDE {
    return InsecureChannelCredentials();
  }
  const grpc::string GetCredentialUsage() const GRPC_OVERRIDE { return ""; }
};

bool PrintStream(std::stringstream* ss, const grpc::string& output) {
  (*ss) << output;
  return true;
}

template <typename T>
size_t ArraySize(T& a) {
  return ((sizeof(a) / sizeof(*(a))) /
          static_cast<size_t>(!(sizeof(a) % sizeof(*(a)))));
}

}  // namespame

class TestServiceImpl : public ::grpc::testing::EchoTestService::Service {
 public:
  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) GRPC_OVERRIDE {
    if (!context->client_metadata().empty()) {
      for (std::multimap<grpc::string_ref, grpc::string_ref>::const_iterator
               iter = context->client_metadata().begin();
           iter != context->client_metadata().end(); ++iter) {
        context->AddInitialMetadata(ToString(iter->first),
                                    ToString(iter->second));
      }
    }
    context->AddTrailingMetadata("trailing_key", "trailing_value");
    response->set_message(request->message());
    return Status::OK;
  }
};

class GrpcToolTest : public ::testing::Test {
 protected:
  GrpcToolTest() {}

  // SetUpServer cannot be used with EXPECT_EXIT. grpc_pick_unused_port_or_die()
  // uses atexit() to free chosen ports, and it will spawn a new thread in
  // resolve_address_posix.c:192 at exit time.
  const grpc::string SetUpServer() {
    std::ostringstream server_address;
    int port = grpc_pick_unused_port_or_die();
    server_address << "localhost:" << port;
    // Setup server
    ServerBuilder builder;
    builder.AddListeningPort(server_address.str(), InsecureServerCredentials());
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
    return server_address.str();
  }

  void ShutdownServer() { server_->Shutdown(); }

  void ExitWhenError(int argc, const char** argv, const CliCredentials& cred,
                     GrpcToolOutputCallback callback) {
    int result = GrpcToolMainLib(argc, argv, cred, callback);
    if (result) {
      exit(result);
    }
  }

  std::unique_ptr<Server> server_;
  TestServiceImpl service_;
  reflection::ProtoServerReflectionPlugin plugin_;
};

TEST_F(GrpcToolTest, NoCommand) {
  // Test input "grpc_cli"
  std::stringstream output_stream;
  const char* argv[] = {"grpc_cli"};
  // Exit with 1, print usage instruction in stderr
  EXPECT_EXIT(
      GrpcToolMainLib(
          ArraySize(argv), argv, TestCliCredentials(),
          std::bind(PrintStream, &output_stream, std::placeholders::_1)),
      ::testing::ExitedWithCode(1), "No command specified\n" USAGE_REGEX);
  // No output
  EXPECT_TRUE(0 == output_stream.tellp());
}

TEST_F(GrpcToolTest, InvalidCommand) {
  // Test input "grpc_cli"
  std::stringstream output_stream;
  const char* argv[] = {"grpc_cli", "abc"};
  // Exit with 1, print usage instruction in stderr
  EXPECT_EXIT(
      GrpcToolMainLib(
          ArraySize(argv), argv, TestCliCredentials(),
          std::bind(PrintStream, &output_stream, std::placeholders::_1)),
      ::testing::ExitedWithCode(1), "Invalid command 'abc'\n" USAGE_REGEX);
  // No output
  EXPECT_TRUE(0 == output_stream.tellp());
}

TEST_F(GrpcToolTest, HelpCommand) {
  // Test input "grpc_cli help"
  std::stringstream output_stream;
  const char* argv[] = {"grpc_cli", "help"};
  // Exit with 1, print usage instruction in stderr
  EXPECT_EXIT(GrpcToolMainLib(ArraySize(argv), argv, TestCliCredentials(),
                              std::bind(PrintStream, &output_stream,
                                        std::placeholders::_1)),
              ::testing::ExitedWithCode(1), USAGE_REGEX);
  // No output
  EXPECT_TRUE(0 == output_stream.tellp());
}

TEST_F(GrpcToolTest, ListCommand) {
  // Test input "grpc_cli list localhost:<port>"
  std::stringstream output_stream;

  const grpc::string server_address = SetUpServer();
  const char* argv[] = {"grpc_cli", "ls", server_address.c_str()};

  FLAGS_l = false;
  EXPECT_TRUE(0 == GrpcToolMainLib(ArraySize(argv), argv, TestCliCredentials(),
                                   std::bind(PrintStream, &output_stream,
                                             std::placeholders::_1)));
  EXPECT_TRUE(0 == strcmp(output_stream.str().c_str(),
                          "grpc.testing.EchoTestService\n"
                          "grpc.reflection.v1alpha.ServerReflection\n"));

  ShutdownServer();
}

TEST_F(GrpcToolTest, ListOneService) {
  // Test input "grpc_cli list localhost:<port> grpc.testing.EchoTestService"
  std::stringstream output_stream;

  const grpc::string server_address = SetUpServer();
  const char* argv[] = {"grpc_cli", "ls", server_address.c_str(),
                        "grpc.testing.EchoTestService"};
  // without -l flag
  FLAGS_l = false;
  EXPECT_TRUE(0 == GrpcToolMainLib(ArraySize(argv), argv, TestCliCredentials(),
                                   std::bind(PrintStream, &output_stream,
                                             std::placeholders::_1)));
  // Expected output: ECHO_TEST_SERVICE_SUMMARY
  EXPECT_TRUE(0 ==
              strcmp(output_stream.str().c_str(), ECHO_TEST_SERVICE_SUMMARY));

  // with -l flag
  output_stream.str(grpc::string());
  output_stream.clear();
  FLAGS_l = true;
  EXPECT_TRUE(0 == GrpcToolMainLib(ArraySize(argv), argv, TestCliCredentials(),
                                   std::bind(PrintStream, &output_stream,
                                             std::placeholders::_1)));
  // Expected output: ECHO_TEST_SERVICE_DESCRIPTION
  EXPECT_TRUE(
      0 == strcmp(output_stream.str().c_str(), ECHO_TEST_SERVICE_DESCRIPTION));

  ShutdownServer();
}

TEST_F(GrpcToolTest, TypeCommand) {
  // Test input "grpc_cli type localhost:<port> grpc.testing.EchoRequest"
  std::stringstream output_stream;

  const grpc::string server_address = SetUpServer();
  const char* argv[] = {"grpc_cli", "type", server_address.c_str(),
                        "grpc.testing.EchoRequest"};

  EXPECT_TRUE(0 == GrpcToolMainLib(ArraySize(argv), argv, TestCliCredentials(),
                                   std::bind(PrintStream, &output_stream,
                                             std::placeholders::_1)));
  const grpc::protobuf::Descriptor* desc =
      grpc::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "grpc.testing.EchoRequest");
  // Expected output: the DebugString of grpc.testing.EchoRequest
  EXPECT_TRUE(0 ==
              strcmp(output_stream.str().c_str(), desc->DebugString().c_str()));

  ShutdownServer();
}

TEST_F(GrpcToolTest, ListOneMethod) {
  // Test input "grpc_cli list localhost:<port> grpc.testing.EchoTestService"
  std::stringstream output_stream;

  const grpc::string server_address = SetUpServer();
  const char* argv[] = {"grpc_cli", "ls", server_address.c_str(),
                        "grpc.testing.EchoTestService.Echo"};
  // without -l flag
  FLAGS_l = false;
  EXPECT_TRUE(0 == GrpcToolMainLib(ArraySize(argv), argv, TestCliCredentials(),
                                   std::bind(PrintStream, &output_stream,
                                             std::placeholders::_1)));
  // Expected output: "Echo"
  EXPECT_TRUE(0 == strcmp(output_stream.str().c_str(), "Echo\n"));

  // with -l flag
  output_stream.str(grpc::string());
  output_stream.clear();
  FLAGS_l = true;
  EXPECT_TRUE(0 == GrpcToolMainLib(ArraySize(argv), argv, TestCliCredentials(),
                                   std::bind(PrintStream, &output_stream,
                                             std::placeholders::_1)));
  // Expected output: ECHO_METHOD_DESCRIPTION
  EXPECT_TRUE(0 ==
              strcmp(output_stream.str().c_str(), ECHO_METHOD_DESCRIPTION));

  ShutdownServer();
}

TEST_F(GrpcToolTest, TypeNotFound) {
  // Test input "grpc_cli type localhost:<port> grpc.testing.DummyRequest"
  std::stringstream output_stream;

  const grpc::string server_address = SetUpServer();
  const char* argv[] = {"grpc_cli", "type", server_address.c_str(),
                        "grpc.testing.DummyRequest"};

  EXPECT_DEATH(ExitWhenError(ArraySize(argv), argv, TestCliCredentials(),
                             std::bind(PrintStream, &output_stream,
                                       std::placeholders::_1)),
               ".*Type grpc.testing.DummyRequest not found.*");

  ShutdownServer();
}

TEST_F(GrpcToolTest, CallCommand) {
  // Test input "grpc_cli call localhost:<port> Echo "message: 'Hello'"
  std::stringstream output_stream;

  const grpc::string server_address = SetUpServer();
  const char* argv[] = {"grpc_cli", "call", server_address.c_str(), "Echo",
                        "message: 'Hello'"};

  EXPECT_TRUE(0 == GrpcToolMainLib(ArraySize(argv), argv, TestCliCredentials(),
                                   std::bind(PrintStream, &output_stream,
                                             std::placeholders::_1)));
  // Expected output: "message: \"Hello\""
  EXPECT_TRUE(NULL !=
              strstr(output_stream.str().c_str(), "message: \"Hello\""));
  ShutdownServer();
}

TEST_F(GrpcToolTest, TooFewArguments) {
  // Test input "grpc_cli call Echo"
  std::stringstream output_stream;
  const char* argv[] = {"grpc_cli", "call", "Echo"};

  // Exit with 1
  EXPECT_EXIT(
      GrpcToolMainLib(
          ArraySize(argv), argv, TestCliCredentials(),
          std::bind(PrintStream, &output_stream, std::placeholders::_1)),
      ::testing::ExitedWithCode(1), ".*Wrong number of arguments for call.*");
  // No output
  EXPECT_TRUE(0 == output_stream.tellp());
}

TEST_F(GrpcToolTest, TooManyArguments) {
  // Test input "grpc_cli call localhost:<port> Echo Echo "message: 'Hello'"
  std::stringstream output_stream;
  const char* argv[] = {"grpc_cli", "call", "localhost:10000",
                        "Echo",     "Echo", "message: 'Hello'"};

  // Exit with 1
  EXPECT_EXIT(
      GrpcToolMainLib(
          ArraySize(argv), argv, TestCliCredentials(),
          std::bind(PrintStream, &output_stream, std::placeholders::_1)),
      ::testing::ExitedWithCode(1), ".*Wrong number of arguments for call.*");
  // No output
  EXPECT_TRUE(0 == output_stream.tellp());
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  return RUN_ALL_TESTS();
}
