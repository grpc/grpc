/*
 *
 * Copyright 2016 gRPC authors.
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

#include "test/cpp/util/grpc_tool.h"

#include <sstream>

#include <gflags/gflags.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <gtest/gtest.h>

#include "src/core/lib/gpr/env.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "src/proto/grpc/testing/echo.pb.h"
#include "test/core/end2end/data/ssl_test_data.h"
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

#define ECHO_RESPONSE_MESSAGE \
  "message: \"echo\"\n"       \
  "param {\n"                 \
  "  host: \"localhost\"\n"   \
  "  peer: \"peer\"\n"        \
  "}\n\n"

DECLARE_string(channel_creds_type);
DECLARE_string(ssl_target);

namespace grpc {
namespace testing {

DECLARE_bool(binary_input);
DECLARE_bool(binary_output);
DECLARE_bool(l);
DECLARE_bool(batch);
DECLARE_string(metadata);
DECLARE_string(protofiles);
DECLARE_string(proto_path);

namespace {

const int kServerDefaultResponseStreamsToSend = 3;

class TestCliCredentials final : public grpc::testing::CliCredentials {
 public:
  TestCliCredentials(bool secure = false) : secure_(secure) {}
  std::shared_ptr<grpc::ChannelCredentials> GetChannelCredentials()
      const override {
    if (!secure_) {
      return InsecureChannelCredentials();
    }
    SslCredentialsOptions ssl_opts = {test_root_cert, "", ""};
    return SslCredentials(grpc::SslCredentialsOptions(ssl_opts));
  }
  const grpc::string GetCredentialUsage() const override { return ""; }

 private:
  const bool secure_;
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

class TestServiceImpl : public ::grpc::testing::EchoTestService::Service {
 public:
  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
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

  Status RequestStream(ServerContext* context,
                       ServerReader<EchoRequest>* reader,
                       EchoResponse* response) override {
    EchoRequest request;
    response->set_message("");
    if (!context->client_metadata().empty()) {
      for (std::multimap<grpc::string_ref, grpc::string_ref>::const_iterator
               iter = context->client_metadata().begin();
           iter != context->client_metadata().end(); ++iter) {
        context->AddInitialMetadata(ToString(iter->first),
                                    ToString(iter->second));
      }
    }
    context->AddTrailingMetadata("trailing_key", "trailing_value");
    while (reader->Read(&request)) {
      response->mutable_message()->append(request.message());
    }

    return Status::OK;
  }

  Status ResponseStream(ServerContext* context, const EchoRequest* request,
                        ServerWriter<EchoResponse>* writer) override {
    if (!context->client_metadata().empty()) {
      for (std::multimap<grpc::string_ref, grpc::string_ref>::const_iterator
               iter = context->client_metadata().begin();
           iter != context->client_metadata().end(); ++iter) {
        context->AddInitialMetadata(ToString(iter->first),
                                    ToString(iter->second));
      }
    }
    context->AddTrailingMetadata("trailing_key", "trailing_value");

    EchoResponse response;
    for (int i = 0; i < kServerDefaultResponseStreamsToSend; i++) {
      response.set_message(request->message() + grpc::to_string(i));
      writer->Write(response);
    }

    return Status::OK;
  }

  Status BidiStream(
      ServerContext* context,
      ServerReaderWriter<EchoResponse, EchoRequest>* stream) override {
    EchoRequest request;
    EchoResponse response;
    if (!context->client_metadata().empty()) {
      for (std::multimap<grpc::string_ref, grpc::string_ref>::const_iterator
               iter = context->client_metadata().begin();
           iter != context->client_metadata().end(); ++iter) {
        context->AddInitialMetadata(ToString(iter->first),
                                    ToString(iter->second));
      }
    }
    context->AddTrailingMetadata("trailing_key", "trailing_value");

    while (stream->Read(&request)) {
      response.set_message(request.message());
      stream->Write(response);
    }

    return Status::OK;
  }
};

}  // namespace

class GrpcToolTest : public ::testing::Test {
 protected:
  GrpcToolTest() {}

  // SetUpServer cannot be used with EXPECT_EXIT. grpc_pick_unused_port_or_die()
  // uses atexit() to free chosen ports, and it will spawn a new thread in
  // resolve_address_posix.c:192 at exit time.
  const grpc::string SetUpServer(bool secure = false) {
    std::ostringstream server_address;
    int port = grpc_pick_unused_port_or_die();
    server_address << "localhost:" << port;
    // Setup server
    ServerBuilder builder;
    std::shared_ptr<grpc::ServerCredentials> creds;
    if (secure) {
      SslServerCredentialsOptions::PemKeyCertPair pkcp = {test_server1_key,
                                                          test_server1_cert};
      SslServerCredentialsOptions ssl_opts;
      ssl_opts.pem_root_certs = "";
      ssl_opts.pem_key_cert_pairs.push_back(pkcp);
      creds = SslServerCredentials(ssl_opts);
    } else {
      creds = InsecureServerCredentials();
    }
    builder.AddListeningPort(server_address.str(), creds);
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
  EXPECT_TRUE(nullptr !=
              strstr(output_stream.str().c_str(), "message: \"Hello\""));
  ShutdownServer();
}

TEST_F(GrpcToolTest, CallCommandBatch) {
  // Test input "grpc_cli call Echo"
  std::stringstream output_stream;

  const grpc::string server_address = SetUpServer();
  const char* argv[] = {"grpc_cli", "call", server_address.c_str(), "Echo",
                        "message: 'Hello0'"};

  // Mock std::cin input "message: 'Hello1'\n\n message: 'Hello2'\n\n"
  std::streambuf* orig = std::cin.rdbuf();
  std::istringstream ss("message: 'Hello1'\n\n message: 'Hello2'\n\n");
  std::cin.rdbuf(ss.rdbuf());

  FLAGS_batch = true;
  EXPECT_TRUE(0 == GrpcToolMainLib(ArraySize(argv), argv, TestCliCredentials(),
                                   std::bind(PrintStream, &output_stream,
                                             std::placeholders::_1)));
  FLAGS_batch = false;

  // Expected output: "message: "Hello0"\nmessage: "Hello1"\nmessage:
  // "Hello2"\n"
  EXPECT_TRUE(nullptr != strstr(output_stream.str().c_str(),
                                "message: \"Hello0\"\nmessage: "
                                "\"Hello1\"\nmessage: \"Hello2\"\n"));
  std::cin.rdbuf(orig);
  ShutdownServer();
}

TEST_F(GrpcToolTest, CallCommandBatchWithBadRequest) {
  // Test input "grpc_cli call Echo"
  std::stringstream output_stream;

  const grpc::string server_address = SetUpServer();
  const char* argv[] = {"grpc_cli", "call", server_address.c_str(), "Echo",
                        "message: 'Hello0'"};

  // Mock std::cin input "message: 1\n\n message: 'Hello2'\n\n"
  std::streambuf* orig = std::cin.rdbuf();
  std::istringstream ss("message: 1\n\n message: 'Hello2'\n\n");
  std::cin.rdbuf(ss.rdbuf());

  FLAGS_batch = true;
  EXPECT_TRUE(0 == GrpcToolMainLib(ArraySize(argv), argv, TestCliCredentials(),
                                   std::bind(PrintStream, &output_stream,
                                             std::placeholders::_1)));
  FLAGS_batch = false;

  // Expected output: "message: "Hello0"\nmessage: "Hello2"\n"
  EXPECT_TRUE(nullptr != strstr(output_stream.str().c_str(),
                                "message: \"Hello0\"\nmessage: \"Hello2\"\n"));
  std::cin.rdbuf(orig);
  ShutdownServer();
}

TEST_F(GrpcToolTest, CallCommandRequestStream) {
  // Test input: grpc_cli call localhost:<port> RequestStream "message:
  // 'Hello0'"
  std::stringstream output_stream;

  const grpc::string server_address = SetUpServer();
  const char* argv[] = {"grpc_cli", "call", server_address.c_str(),
                        "RequestStream", "message: 'Hello0'"};

  // Mock std::cin input "message: 'Hello1'\n\n message: 'Hello2'\n\n"
  std::streambuf* orig = std::cin.rdbuf();
  std::istringstream ss("message: 'Hello1'\n\n message: 'Hello2'\n\n");
  std::cin.rdbuf(ss.rdbuf());

  EXPECT_TRUE(0 == GrpcToolMainLib(ArraySize(argv), argv, TestCliCredentials(),
                                   std::bind(PrintStream, &output_stream,
                                             std::placeholders::_1)));

  // Expected output: "message: \"Hello0Hello1Hello2\""
  EXPECT_TRUE(nullptr != strstr(output_stream.str().c_str(),
                                "message: \"Hello0Hello1Hello2\""));
  std::cin.rdbuf(orig);
  ShutdownServer();
}

TEST_F(GrpcToolTest, CallCommandRequestStreamWithBadRequest) {
  // Test input: grpc_cli call localhost:<port> RequestStream "message:
  // 'Hello0'"
  std::stringstream output_stream;

  const grpc::string server_address = SetUpServer();
  const char* argv[] = {"grpc_cli", "call", server_address.c_str(),
                        "RequestStream", "message: 'Hello0'"};

  // Mock std::cin input "bad_field: 'Hello1'\n\n message: 'Hello2'\n\n"
  std::streambuf* orig = std::cin.rdbuf();
  std::istringstream ss("bad_field: 'Hello1'\n\n message: 'Hello2'\n\n");
  std::cin.rdbuf(ss.rdbuf());

  EXPECT_TRUE(0 == GrpcToolMainLib(ArraySize(argv), argv, TestCliCredentials(),
                                   std::bind(PrintStream, &output_stream,
                                             std::placeholders::_1)));

  // Expected output: "message: \"Hello0Hello2\""
  EXPECT_TRUE(nullptr !=
              strstr(output_stream.str().c_str(), "message: \"Hello0Hello2\""));
  std::cin.rdbuf(orig);
  ShutdownServer();
}

TEST_F(GrpcToolTest, CallCommandResponseStream) {
  // Test input: grpc_cli call localhost:<port> ResponseStream "message:
  // 'Hello'"
  std::stringstream output_stream;

  const grpc::string server_address = SetUpServer();
  const char* argv[] = {"grpc_cli", "call", server_address.c_str(),
                        "ResponseStream", "message: 'Hello'"};

  EXPECT_TRUE(0 == GrpcToolMainLib(ArraySize(argv), argv, TestCliCredentials(),
                                   std::bind(PrintStream, &output_stream,
                                             std::placeholders::_1)));

  // Expected output: "message: \"Hello{n}\""
  for (int i = 0; i < kServerDefaultResponseStreamsToSend; i++) {
    grpc::string expected_response_text =
        "message: \"Hello" + grpc::to_string(i) + "\"\n";
    EXPECT_TRUE(nullptr != strstr(output_stream.str().c_str(),
                                  expected_response_text.c_str()));
  }

  ShutdownServer();
}

TEST_F(GrpcToolTest, CallCommandBidiStream) {
  // Test input: grpc_cli call localhost:<port> BidiStream "message: 'Hello0'"
  std::stringstream output_stream;

  const grpc::string server_address = SetUpServer();
  const char* argv[] = {"grpc_cli", "call", server_address.c_str(),
                        "BidiStream", "message: 'Hello0'"};

  // Mock std::cin input "message: 'Hello1'\n\n message: 'Hello2'\n\n"
  std::streambuf* orig = std::cin.rdbuf();
  std::istringstream ss("message: 'Hello1'\n\n message: 'Hello2'\n\n");
  std::cin.rdbuf(ss.rdbuf());

  EXPECT_TRUE(0 == GrpcToolMainLib(ArraySize(argv), argv, TestCliCredentials(),
                                   std::bind(PrintStream, &output_stream,
                                             std::placeholders::_1)));

  // Expected output: "message: \"Hello0\"\nmessage: \"Hello1\"\nmessage:
  // \"Hello2\"\n\n"
  EXPECT_TRUE(nullptr != strstr(output_stream.str().c_str(),
                                "message: \"Hello0\"\nmessage: "
                                "\"Hello1\"\nmessage: \"Hello2\"\n"));
  std::cin.rdbuf(orig);
  ShutdownServer();
}

TEST_F(GrpcToolTest, CallCommandBidiStreamWithBadRequest) {
  // Test input: grpc_cli call localhost:<port> BidiStream "message: 'Hello0'"
  std::stringstream output_stream;

  const grpc::string server_address = SetUpServer();
  const char* argv[] = {"grpc_cli", "call", server_address.c_str(),
                        "BidiStream", "message: 'Hello0'"};

  // Mock std::cin input "message: 'Hello1'\n\n message: 'Hello2'\n\n"
  std::streambuf* orig = std::cin.rdbuf();
  std::istringstream ss("message: 1.0\n\n message: 'Hello2'\n\n");
  std::cin.rdbuf(ss.rdbuf());

  EXPECT_TRUE(0 == GrpcToolMainLib(ArraySize(argv), argv, TestCliCredentials(),
                                   std::bind(PrintStream, &output_stream,
                                             std::placeholders::_1)));

  // Expected output: "message: \"Hello0\"\nmessage: \"Hello1\"\nmessage:
  // \"Hello2\"\n\n"
  EXPECT_TRUE(nullptr != strstr(output_stream.str().c_str(),
                                "message: \"Hello0\"\nmessage: \"Hello2\"\n"));
  std::cin.rdbuf(orig);

  ShutdownServer();
}

TEST_F(GrpcToolTest, ParseCommand) {
  // Test input "grpc_cli parse localhost:<port> grpc.testing.EchoResponse
  // ECHO_RESPONSE_MESSAGE"
  std::stringstream output_stream;
  std::stringstream binary_output_stream;

  const grpc::string server_address = SetUpServer();
  const char* argv[] = {"grpc_cli", "parse", server_address.c_str(),
                        "grpc.testing.EchoResponse", ECHO_RESPONSE_MESSAGE};

  FLAGS_binary_input = false;
  FLAGS_binary_output = false;
  EXPECT_TRUE(0 == GrpcToolMainLib(ArraySize(argv), argv, TestCliCredentials(),
                                   std::bind(PrintStream, &output_stream,
                                             std::placeholders::_1)));
  // Expected output: ECHO_RESPONSE_MESSAGE
  EXPECT_TRUE(0 == strcmp(output_stream.str().c_str(), ECHO_RESPONSE_MESSAGE));

  // Parse text message to binary message and then parse it back to text message
  output_stream.str(grpc::string());
  output_stream.clear();
  FLAGS_binary_output = true;
  EXPECT_TRUE(0 == GrpcToolMainLib(ArraySize(argv), argv, TestCliCredentials(),
                                   std::bind(PrintStream, &output_stream,
                                             std::placeholders::_1)));
  grpc::string binary_data = output_stream.str();
  output_stream.str(grpc::string());
  output_stream.clear();
  argv[4] = binary_data.c_str();
  FLAGS_binary_input = true;
  FLAGS_binary_output = false;
  EXPECT_TRUE(0 == GrpcToolMainLib(5, argv, TestCliCredentials(),
                                   std::bind(PrintStream, &output_stream,
                                             std::placeholders::_1)));

  // Expected output: ECHO_RESPONSE_MESSAGE
  EXPECT_TRUE(0 == strcmp(output_stream.str().c_str(), ECHO_RESPONSE_MESSAGE));

  FLAGS_binary_input = false;
  FLAGS_binary_output = false;
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

TEST_F(GrpcToolTest, CallCommandWithMetadata) {
  // Test input "grpc_cli call localhost:<port> Echo "message: 'Hello'"
  const grpc::string server_address = SetUpServer();
  const char* argv[] = {"grpc_cli", "call", server_address.c_str(), "Echo",
                        "message: 'Hello'"};

  {
    std::stringstream output_stream;
    FLAGS_metadata = "key0:val0:key1:valq:key2:val2";
    EXPECT_TRUE(0 == GrpcToolMainLib(ArraySize(argv), argv,
                                     TestCliCredentials(),
                                     std::bind(PrintStream, &output_stream,
                                               std::placeholders::_1)));
    // Expected output: "message: \"Hello\""
    EXPECT_TRUE(nullptr !=
                strstr(output_stream.str().c_str(), "message: \"Hello\""));
  }

  {
    std::stringstream output_stream;
    FLAGS_metadata = "key:val\\:val";
    EXPECT_TRUE(0 == GrpcToolMainLib(ArraySize(argv), argv,
                                     TestCliCredentials(),
                                     std::bind(PrintStream, &output_stream,
                                               std::placeholders::_1)));
    // Expected output: "message: \"Hello\""
    EXPECT_TRUE(nullptr !=
                strstr(output_stream.str().c_str(), "message: \"Hello\""));
  }

  {
    std::stringstream output_stream;
    FLAGS_metadata = "key:val\\\\val";
    EXPECT_TRUE(0 == GrpcToolMainLib(ArraySize(argv), argv,
                                     TestCliCredentials(),
                                     std::bind(PrintStream, &output_stream,
                                               std::placeholders::_1)));
    // Expected output: "message: \"Hello\""
    EXPECT_TRUE(nullptr !=
                strstr(output_stream.str().c_str(), "message: \"Hello\""));
  }

  FLAGS_metadata = "";
  ShutdownServer();
}

TEST_F(GrpcToolTest, CallCommandWithBadMetadata) {
  // Test input "grpc_cli call localhost:10000 Echo "message: 'Hello'"
  const char* argv[] = {"grpc_cli", "call", "localhost:10000", "Echo",
                        "message: 'Hello'"};
  FLAGS_protofiles = "src/proto/grpc/testing/echo.proto";
  char* test_srcdir = gpr_getenv("TEST_SRCDIR");
  if (test_srcdir != nullptr) {
    FLAGS_proto_path = test_srcdir + std::string("/com_github_grpc_grpc");
  }

  {
    std::stringstream output_stream;
    FLAGS_metadata = "key0:val0:key1";
    // Exit with 1
    EXPECT_EXIT(
        GrpcToolMainLib(
            ArraySize(argv), argv, TestCliCredentials(),
            std::bind(PrintStream, &output_stream, std::placeholders::_1)),
        ::testing::ExitedWithCode(1), ".*Failed to parse metadata flag.*");
  }

  {
    std::stringstream output_stream;
    FLAGS_metadata = "key:val\\val";
    // Exit with 1
    EXPECT_EXIT(
        GrpcToolMainLib(
            ArraySize(argv), argv, TestCliCredentials(),
            std::bind(PrintStream, &output_stream, std::placeholders::_1)),
        ::testing::ExitedWithCode(1), ".*Failed to parse metadata flag.*");
  }

  FLAGS_metadata = "";
  FLAGS_protofiles = "";

  gpr_free(test_srcdir);
}

TEST_F(GrpcToolTest, ListCommand_OverrideSslHostName) {
  const grpc::string server_address = SetUpServer(true);

  // Test input "grpc_cli ls localhost:<port> --channel_creds_type=ssl
  // --ssl_target=z.test.google.fr"
  std::stringstream output_stream;
  const char* argv[] = {"grpc_cli", "ls", server_address.c_str()};
  FLAGS_l = false;
  FLAGS_channel_creds_type = "ssl";
  FLAGS_ssl_target = "z.test.google.fr";
  EXPECT_TRUE(
      0 == GrpcToolMainLib(
               ArraySize(argv), argv, TestCliCredentials(true),
               std::bind(PrintStream, &output_stream, std::placeholders::_1)));
  EXPECT_TRUE(0 == strcmp(output_stream.str().c_str(),
                          "grpc.testing.EchoTestService\n"
                          "grpc.reflection.v1alpha.ServerReflection\n"));

  FLAGS_channel_creds_type = "";
  FLAGS_ssl_target = "";
  ShutdownServer();
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  return RUN_ALL_TESTS();
}
