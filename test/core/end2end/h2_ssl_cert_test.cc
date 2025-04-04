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

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/time.h>
#include <stdio.h>
#include <string.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "gtest/gtest.h"
#include "src/core/config/config_vars.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/time.h"
#include "src/core/util/tmpfile.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/secure_fixture.h"
#include "test/core/test_util/test_config.h"

static std::string test_server1_key_id;

namespace grpc {
namespace testing {

static void process_auth_failure(void* state, grpc_auth_context* /*ctx*/,
                                 const grpc_metadata* /*md*/,
                                 size_t /*md_count*/,
                                 grpc_process_auth_metadata_done_cb cb,
                                 void* user_data) {
  CHECK_EQ(state, nullptr);
  cb(user_data, nullptr, 0, nullptr, 0, GRPC_STATUS_UNAUTHENTICATED, nullptr);
}

typedef enum { NONE, SELF_SIGNED, SIGNED, BAD_CERT_PAIR } certtype;

class TestFixture : public SecureFixture {
 public:
  TestFixture(grpc_ssl_client_certificate_request_type request_type,
              certtype cert_type)
      : request_type_(request_type), cert_type_(cert_type) {}

  static auto MakeFactory(grpc_ssl_client_certificate_request_type request_type,
                          certtype cert_type) {
    return [request_type, cert_type](const grpc_core::ChannelArgs&,
                                     const grpc_core::ChannelArgs&) {
      return std::make_unique<TestFixture>(request_type, cert_type);
    };
  }

 private:
  grpc_core::ChannelArgs MutateClientArgs(
      grpc_core::ChannelArgs args) override {
    return args.Set(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG, "foo.test.google.fr");
  }

  grpc_server_credentials* MakeServerCreds(
      const grpc_core::ChannelArgs& args) override {
    grpc_ssl_pem_key_cert_pair pem_cert_key_pair;
    if (!test_server1_key_id.empty()) {
      pem_cert_key_pair.private_key = test_server1_key_id.c_str();
      pem_cert_key_pair.cert_chain = test_server1_cert;
    } else {
      pem_cert_key_pair.private_key = test_server1_key;
      pem_cert_key_pair.cert_chain = test_server1_cert;
    }
    grpc_server_credentials* ssl_creds = grpc_ssl_server_credentials_create_ex(
        test_root_cert, &pem_cert_key_pair, 1, request_type_, nullptr);
    if (args.Contains(FAIL_AUTH_CHECK_SERVER_ARG_NAME)) {
      grpc_auth_metadata_processor processor = {process_auth_failure, nullptr,
                                                nullptr};
      grpc_server_credentials_set_auth_metadata_processor(ssl_creds, processor);
    }
    return ssl_creds;
  }

  grpc_channel_credentials* MakeClientCreds(
      const grpc_core::ChannelArgs&) override {
    grpc_ssl_pem_key_cert_pair self_signed_client_key_cert_pair = {
        test_self_signed_client_key, test_self_signed_client_cert};
    grpc_ssl_pem_key_cert_pair signed_client_key_cert_pair = {
        test_signed_client_key, test_signed_client_cert};
    grpc_ssl_pem_key_cert_pair bad_client_key_cert_pair = {
        test_self_signed_client_key, test_signed_client_cert};
    grpc_ssl_pem_key_cert_pair* key_cert_pair = nullptr;
    switch (cert_type_) {
      case SELF_SIGNED:
        key_cert_pair = &self_signed_client_key_cert_pair;
        break;
      case SIGNED:
        key_cert_pair = &signed_client_key_cert_pair;
        break;
      case BAD_CERT_PAIR:
        key_cert_pair = &bad_client_key_cert_pair;
        break;
      default:
        break;
    }
    return grpc_ssl_credentials_create(test_root_cert, key_cert_pair, nullptr,
                                       nullptr);
  }

  grpc_ssl_client_certificate_request_type request_type_;
  certtype cert_type_;
};

#define TEST_NAME(enum_name, cert_type, result) \
  "chttp2/ssl_" #enum_name "_" #cert_type "_" #result "_"

typedef enum { SUCCESS, FAIL } test_result;

#define SSL_TEST(request_type, cert_type, result)                             \
  {{TEST_NAME(request_type, cert_type, result),                               \
    FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |                              \
        FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL,                                 \
    "foo.test.google.fr", TestFixture::MakeFactory(request_type, cert_type)}, \
   result}

// All test configurations
struct CoreTestConfigWrapper {
  grpc_core::CoreTestConfiguration config;
  test_result result;
};

static CoreTestConfigWrapper configs[] = {
    SSL_TEST(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE, NONE, SUCCESS),
    SSL_TEST(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE, SELF_SIGNED, SUCCESS),
    SSL_TEST(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE, SIGNED, SUCCESS),
    SSL_TEST(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE, BAD_CERT_PAIR, FAIL),

    SSL_TEST(GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_BUT_DONT_VERIFY, NONE,
             SUCCESS),
    SSL_TEST(GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_BUT_DONT_VERIFY, SELF_SIGNED,
             SUCCESS),
    SSL_TEST(GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_BUT_DONT_VERIFY, SIGNED,
             SUCCESS),
    SSL_TEST(GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_BUT_DONT_VERIFY, BAD_CERT_PAIR,
             FAIL),

    SSL_TEST(GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY, NONE, SUCCESS),
    SSL_TEST(GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY, SELF_SIGNED, FAIL),
    SSL_TEST(GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY, SIGNED, SUCCESS),
    SSL_TEST(GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY, BAD_CERT_PAIR,
             FAIL),

    SSL_TEST(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY,
             NONE, FAIL),
    SSL_TEST(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY,
             SELF_SIGNED, SUCCESS),
    SSL_TEST(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY,
             SIGNED, SUCCESS),
    SSL_TEST(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY,
             BAD_CERT_PAIR, FAIL),

    SSL_TEST(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY, NONE,
             FAIL),
    SSL_TEST(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY,
             SELF_SIGNED, FAIL),
    SSL_TEST(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY, SIGNED,
             SUCCESS),
    SSL_TEST(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY,
             BAD_CERT_PAIR, FAIL),
};

static void simple_request_body(grpc_core::CoreTestFixture* f,
                                test_result expected_result) {
  grpc_call* c;
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(30);
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  grpc_core::CqVerifier cqv(cq);
  grpc_op ops[6];
  grpc_op* op;
  grpc_call_error error;

  grpc_channel* client = f->MakeClient(grpc_core::ChannelArgs(), cq);
  absl::AnyInvocable<void(grpc_server*)> pre_start_server = [](grpc_server*) {};
  grpc_server* server =
      f->MakeServer(grpc_core::ChannelArgs(), cq, pre_start_server);

  grpc_slice host = grpc_slice_from_static_string("foo.test.google.fr:1234");
  c = grpc_channel_create_call(client, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
                               grpc_slice_from_static_string("/foo"), &host,
                               deadline, nullptr);
  CHECK(c);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = GRPC_INITIAL_METADATA_WAIT_FOR_READY;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops),
                                grpc_core::CqVerifier::tag(1), nullptr);
  CHECK_EQ(error, GRPC_CALL_OK);

  cqv.Expect(grpc_core::CqVerifier::tag(1), expected_result == SUCCESS);
  cqv.Verify(grpc_core::Duration::Seconds(60));

  grpc_call_unref(c);
  grpc_channel_destroy(client);
  grpc_server_shutdown_and_notify(server, cq, nullptr);
  cqv.Expect(nullptr, true);
  cqv.Verify(grpc_core::Duration::Seconds(60));
  grpc_server_destroy(server);
  grpc_completion_queue_shutdown(cq);
  CHECK(grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                   nullptr)
            .type == GRPC_QUEUE_SHUTDOWN);
  grpc_completion_queue_destroy(cq);
}

class H2SslCertTest : public ::testing::TestWithParam<CoreTestConfigWrapper> {
 protected:
  H2SslCertTest() { LOG(INFO) << "SSL_CERT_tests/" << GetParam().config.name; }
  void SetUp() override {
    fixture_ = GetParam().config.create_fixture(grpc_core::ChannelArgs(),
                                                grpc_core::ChannelArgs());
  }
  void TearDown() override { fixture_.reset(); }

  std::unique_ptr<grpc_core::CoreTestFixture> fixture_;
};

TEST_P(H2SslCertTest, SimpleRequestBody) {
  simple_request_body(fixture_.get(), GetParam().result);
}

// TODO(b/283304471) SimpleRequestBodyUseEngineTest was failing on OpenSSL3.0
// and 1.1.1 and removed. Investigate and rewrite a better test.

INSTANTIATE_TEST_SUITE_P(H2SslCert, H2SslCertTest,
                         ::testing::ValuesIn(configs));

}  // namespace testing
}  // namespace grpc

namespace grpc_core {
std::vector<CoreTestConfiguration> End2endTestConfigs() { return {}; }
}  // namespace grpc_core

int main(int argc, char** argv) {
  FILE* roots_file;
  size_t roots_size = strlen(test_root_cert);
  char* roots_filename;

  grpc::testing::TestEnvironment env(&argc, argv);
  // Set the SSL roots env var.
  roots_file =
      gpr_tmpfile("chttp2_simple_ssl_cert_fullstack_test", &roots_filename);
  CHECK_NE(roots_filename, nullptr);
  CHECK_NE(roots_file, nullptr);
  CHECK(fwrite(test_root_cert, 1, roots_size, roots_file) == roots_size);
  fclose(roots_file);
  grpc_core::ConfigVars::Overrides config_overrides;
  config_overrides.default_ssl_roots_file_path = roots_filename;
  grpc_core::ConfigVars::SetOverrides(config_overrides);

  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();

  // Cleanup.
  remove(roots_filename);
  gpr_free(roots_filename);

  return ret;
}
