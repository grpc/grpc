//
//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_TEST_CORE_END2END_FIXTURES_HTTP_COMMON_SECURE_FIXTURES_H
#define GRPC_TEST_CORE_END2END_FIXTURES_HTTP_COMMON_SECURE_FIXTURES_H

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/status.h>

#include <cstddef>
#include <string>

#include "src/core/credentials/transport/fake/fake_credentials.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/util/grpc_check.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/proxy.h"
#include "test/core/end2end/fixtures/secure_fixture.h"
#include "test/core/test_util/tls_utils.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

constexpr absl::string_view kCaCertPath = "src/core/tsi/test_creds/ca.pem";
constexpr absl::string_view kServerCertPath =
    "src/core/tsi/test_creds/server1.pem";
constexpr absl::string_view kServerKeyPath =
    "src/core/tsi/test_creds/server1.key";

namespace http_common_secure_fixtures_internal {
inline void ProcessAuthFailure(void* state, grpc_auth_context* /*ctx*/,
                               const grpc_metadata* /*md*/, size_t /*md_count*/,
                               grpc_process_auth_metadata_done_cb cb,
                               void* user_data) {
  GRPC_CHECK_EQ(state, nullptr);
  cb(user_data, nullptr, 0, nullptr, 0, GRPC_STATUS_UNAUTHENTICATED, nullptr);
}

inline void AddFailAuthCheckIfNeeded(const ChannelArgs& args,
                                     grpc_server_credentials* creds) {
  if (args.Contains(FAIL_AUTH_CHECK_SERVER_ARG_NAME)) {
    grpc_auth_metadata_processor processor = {ProcessAuthFailure, nullptr,
                                              nullptr};
    grpc_server_credentials_set_auth_metadata_processor(creds, processor);
  }
}
}  // namespace http_common_secure_fixtures_internal

class FakesecFixture : public SecureFixture {
 private:
  grpc_channel_credentials* MakeClientCreds(const ChannelArgs&) override {
    return grpc_fake_transport_security_credentials_create();
  }
  grpc_server_credentials* MakeServerCreds(const ChannelArgs& args) override {
    grpc_server_credentials* fake_ts_creds =
        grpc_fake_transport_security_server_credentials_create();
    http_common_secure_fixtures_internal::AddFailAuthCheckIfNeeded(
        args, fake_ts_creds);
    return fake_ts_creds;
  }
};

class InsecureCredsFixture : public InsecureFixture {
 private:
  grpc_server_credentials* MakeServerCreds(const ChannelArgs& args) override {
    auto* creds = grpc_insecure_server_credentials_create();
    http_common_secure_fixtures_internal::AddFailAuthCheckIfNeeded(args, creds);
    return creds;
  }
};

class SslProxyFixture : public CoreTestFixture {
 public:
  SslProxyFixture(const ChannelArgs& client_args,
                  const ChannelArgs& server_args)
      : proxy_def_({CreateProxyServer, CreateProxyClient}),
        proxy_(grpc_end2end_proxy_create(&proxy_def_, client_args.ToC().get(),
                                         server_args.ToC().get())) {}
  ~SslProxyFixture() override { grpc_end2end_proxy_destroy(proxy_); }

 private:
  static grpc_server* CreateProxyServer(const char* port,
                                        const grpc_channel_args* server_args) {
    grpc_server* s = grpc_server_create(server_args, nullptr);
    std::string server_cert =
        testing::GetFileContents(std::string(kServerCertPath));
    std::string server_key =
        testing::GetFileContents(std::string(kServerKeyPath));
    grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {server_key.c_str(),
                                                    server_cert.c_str()};
    grpc_server_credentials* ssl_creds = grpc_ssl_server_credentials_create(
        nullptr, &pem_key_cert_pair, 1, 0, nullptr);
    GRPC_CHECK(grpc_server_add_http2_port(s, port, ssl_creds));
    grpc_server_credentials_release(ssl_creds);
    return s;
  }

  static grpc_channel* CreateProxyClient(const char* target,
                                         const grpc_channel_args* client_args) {
    grpc_channel* channel;
    grpc_channel_credentials* ssl_creds =
        grpc_ssl_credentials_create(nullptr, nullptr, nullptr, nullptr);
    grpc_arg ssl_name_override = {
        GRPC_ARG_STRING,
        const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
        {const_cast<char*>("foo.test.google.fr")}};
    const grpc_channel_args* new_client_args =
        grpc_channel_args_copy_and_add(client_args, &ssl_name_override, 1);
    channel = grpc_channel_create(target, ssl_creds, new_client_args);
    grpc_channel_credentials_release(ssl_creds);
    {
      ExecCtx exec_ctx;
      grpc_channel_args_destroy(new_client_args);
    }
    return channel;
  }

  grpc_server* MakeServer(
      const ChannelArgs& args, grpc_completion_queue* cq,
      absl::AnyInvocable<void(grpc_server*)>& pre_server_start) override {
    std::string server_cert =
        testing::GetFileContents(std::string(kServerCertPath));
    std::string server_key =
        testing::GetFileContents(std::string(kServerKeyPath));
    grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {server_key.c_str(),
                                                    server_cert.c_str()};
    grpc_server_credentials* ssl_creds = grpc_ssl_server_credentials_create(
        nullptr, &pem_key_cert_pair, 1, 0, nullptr);
    if (args.Contains(FAIL_AUTH_CHECK_SERVER_ARG_NAME)) {
      grpc_auth_metadata_processor processor = {
          http_common_secure_fixtures_internal::ProcessAuthFailure, nullptr,
          nullptr};
      grpc_server_credentials_set_auth_metadata_processor(ssl_creds, processor);
    }

    auto* server = grpc_server_create(args.ToC().get(), nullptr);
    grpc_server_register_completion_queue(server, cq, nullptr);
    GRPC_CHECK(grpc_server_add_http2_port(
        server, grpc_end2end_proxy_get_server_port(proxy_), ssl_creds));
    grpc_server_credentials_release(ssl_creds);
    pre_server_start(server);
    grpc_server_start(server);
    return server;
  }

  grpc_channel* MakeClient(const ChannelArgs& args,
                           grpc_completion_queue* cq) override {
    grpc_channel_credentials* ssl_creds =
        grpc_ssl_credentials_create(nullptr, nullptr, nullptr, nullptr);
    auto* client = grpc_channel_create(
        grpc_end2end_proxy_get_client_target(proxy_), ssl_creds,
        args.Set(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG, "foo.test.google.fr")
            .ToC()
            .get());
    GRPC_CHECK_NE(client, nullptr);
    grpc_channel_credentials_release(ssl_creds);
    return client;
  }

  const grpc_end2end_proxy_def proxy_def_;
  grpc_end2end_proxy* proxy_;
};

}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_END2END_FIXTURES_HTTP_COMMON_SECURE_FIXTURES_H
