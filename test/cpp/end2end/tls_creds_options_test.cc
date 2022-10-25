// Copyright 2022 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/slice.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <grpcpp/server_builder.h>

#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

namespace grpc {
namespace testing {
namespace {

using grpc::experimental::TlsChannelCredentialsOptions;
using grpc::experimental::TlsServerCredentialsOptions;

constexpr char kCaCertPath[] = "src/core/tsi/test_creds/ca.pem";
constexpr char kServerCertPath[] = "src/core/tsi/test_creds/server1.pem";
constexpr char kServerKeyPath[] = "src/core/tsi/test_creds/server1.key";
constexpr char kClientCertPath[] = "src/core/tsi/test_creds/client.pem";
constexpr char kClientKeyPath[] = "src/core/tsi/test_creds/client.key";

constexpr char kMessage[] = "Hello";

std::string ReadFile(const char* file_path) {
  grpc_slice slice;
  GPR_ASSERT(
      GRPC_LOG_IF_ERROR("load_file", grpc_load_file(file_path, 0, &slice)));
  std::string file_contents(grpc_core::StringViewFromSlice(slice));
  grpc_slice_unref(slice);
  return file_contents;
}

class TlsCredentialsOptionsEnd2EndTest : public ::testing::Test {
 protected:
  TlsCredentialsOptionsEnd2EndTest()
      : server_address_(
            absl::StrCat("localhost:", grpc_pick_unused_port_or_die())) {}

  ~TlsCredentialsOptionsEnd2EndTest() override { server_->Shutdown(); }

  void InitServerAndChannel(
      const TlsServerCredentialsOptions& server_options,
      const TlsChannelCredentialsOptions& channel_options) {
    auto server_creds =
        grpc::experimental::TlsServerCredentials(server_options);
    auto channel_creds = grpc::experimental::TlsCredentials(channel_options);
    ServerBuilder builder;
    builder.AddListeningPort(server_address_, std::move(server_creds));
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();

    ChannelArguments args;
    // Override target name for host name check
    args.SetSslTargetNameOverride("foo.test.google.fr");
    channel_ = grpc::CreateCustomChannel(server_address_, channel_creds, args);
  }

  grpc::Status SendRpc(ClientContext* context,
                       grpc::testing::EchoResponse* response = nullptr) {
    auto stub = grpc::testing::EchoTestService::NewStub(channel_);
    grpc::testing::EchoRequest request;
    request.set_message(kMessage);
    return stub->Echo(context, request, response);
  }

  std::string server_address_;
  TestServiceImpl service_;
  std::unique_ptr<Server> server_;
  std::shared_ptr<Channel> channel_;
};

TEST_F(TlsCredentialsOptionsEnd2EndTest, MutualTlsWithTls1_2) {
  std::string identity_cert = ReadFile(kServerCertPath);
  std::string private_key = ReadFile(kServerKeyPath);
  std::vector<experimental::IdentityKeyCertPair>
      server_identity_key_cert_pairs = {{private_key, identity_cert}};
  grpc::experimental::TlsServerCredentialsOptions server_options(
      std::make_shared<grpc::experimental::StaticDataCertificateProvider>(
          ReadFile(kCaCertPath), server_identity_key_cert_pairs));
  server_options.watch_root_certs();
  server_options.watch_identity_key_cert_pairs();
  server_options.set_cert_request_type(
      GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY);
  server_options.set_min_tls_version(grpc_tls_version::TLS1_2);
  server_options.set_max_tls_version(grpc_tls_version::TLS1_2);
  std::vector<experimental::IdentityKeyCertPair>
      channel_identity_key_cert_pairs = {
          {ReadFile(kClientKeyPath), ReadFile(kClientCertPath)}};
  TlsChannelCredentialsOptions channel_options;
  channel_options.set_certificate_provider(
      std::make_shared<grpc::experimental::StaticDataCertificateProvider>(
          ReadFile(kCaCertPath), channel_identity_key_cert_pairs));
  channel_options.watch_identity_key_cert_pairs();
  channel_options.watch_root_certs();
  channel_options.set_min_tls_version(grpc_tls_version::TLS1_2);
  channel_options.set_max_tls_version(grpc_tls_version::TLS1_2);
  InitServerAndChannel(server_options, channel_options);
  ClientContext context;
  grpc::testing::EchoResponse resp;
  grpc::Status status = SendRpc(&context, &resp);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(resp.message(), kMessage);
}

TEST_F(TlsCredentialsOptionsEnd2EndTest, ServerSideTlsWithTls1_3) {
  std::string identity_cert = ReadFile(kServerCertPath);
  std::string private_key = ReadFile(kServerKeyPath);
  std::vector<experimental::IdentityKeyCertPair>
      server_identity_key_cert_pairs = {{private_key, identity_cert}};
  grpc::experimental::TlsServerCredentialsOptions server_options(
      std::make_shared<grpc::experimental::StaticDataCertificateProvider>(
          "", server_identity_key_cert_pairs));
  server_options.watch_identity_key_cert_pairs();
  server_options.set_cert_request_type(
      GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY);
  server_options.set_min_tls_version(grpc_tls_version::TLS1_3);
  server_options.set_max_tls_version(grpc_tls_version::TLS1_3);
  TlsChannelCredentialsOptions channel_options;
  channel_options.set_certificate_provider(
      std::make_shared<grpc::experimental::StaticDataCertificateProvider>(
          ReadFile(kCaCertPath),
          std::vector<experimental::IdentityKeyCertPair>()));
  channel_options.watch_root_certs();
  channel_options.set_min_tls_version(grpc_tls_version::TLS1_3);
  channel_options.set_max_tls_version(grpc_tls_version::TLS1_3);
  InitServerAndChannel(server_options, channel_options);
  ClientContext context;
  grpc::testing::EchoResponse resp;
  grpc::Status status = SendRpc(&context, &resp);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(resp.message(), kMessage);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
