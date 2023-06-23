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

#include <stdio.h>
#include <string.h>

#include <functional>
#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <openssl/crypto.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpcpp/support/string_ref.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/global_config_generic.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/ssl_utils_config.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/secure_fixture.h"
#include "test/core/util/test_config.h"

static std::string test_server1_key_id;

namespace grpc {
namespace testing {

struct fullstack_secure_fixture_data {
  std::string localaddr;
};

static grpc_end2end_test_fixture chttp2_create_fixture_secure_fullstack(
    grpc_channel_args* /*client_args*/, grpc_channel_args* /*server_args*/) {
  grpc_end2end_tests;
  f;
  int port = grpc_pick_unused_port_or_die();
  fullstack_secure_fixture_data* ffd = new fullstack_secure_fixture_data();
  memset(&f, 0, sizeof(f));

  ffd->localaddr = grpc_core::JoinHostPort("localhost", port);

  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create_for_next(nullptr);
  return f;
}

static void process_auth_failure(void* state, grpc_auth_context* /*ctx*/,
                                 const grpc_metadata* /*md*/,
                                 size_t /*md_count*/,
                                 grpc_process_auth_metadata_done_cb cb,
                                 void* user_data) {
  GPR_ASSERT(state == nullptr);
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

#define SSL_TEST(request_type, cert_type, result)                              \
  {                                                                            \
    {TEST_NAME(request_type, cert_type, result),                               \
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |                                \
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |                          \
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL,                                 \
     "foo.test.google.fr", TestFixture::MakeFactory(request_type, cert_type)}, \
        result                                                                 \
  }

// All test configurations
struct CoreTestConfigWrapper {
  CoreTestConfiguration config;
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

static void simple_request_body(CoreTestFixture* f,
                                test_result expected_result) {
  grpc_call* c;
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(5);
  grpc_core::CqVerifier cqv(f->cq());
  grpc_op ops[6];
  grpc_op* op;
  grpc_call_error error;

  grpc_slice host = grpc_slice_from_static_string("foo.test.google.fr:1234");
  c = grpc_channel_create_call(f->client(), nullptr, GRPC_PROPAGATE_DEFAULTS,
                               f->cq(), grpc_slice_from_static_string("/foo"),
                               &host, deadline, nullptr);
  GPR_ASSERT(c);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = GRPC_INITIAL_METADATA_WAIT_FOR_READY;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops),
                                grpc_core::CqVerifier::tag(1), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  cqv.Expect(grpc_core::CqVerifier::tag(1), expected_result == SUCCESS);
  cqv.Verify();

  grpc_call_unref(c);
}

class H2SslCertTest : public ::testing::TestWithParam<CoreTestConfigWrapper> {
 protected:
  H2SslCertTest() {
    gpr_log(GPR_INFO, "SSL_CERT_tests/%s", GetParam().config.name);
  }
  void SetUp() override {
    fixture_ = GetParam().config.create_fixture(grpc_core::ChannelArgs(),
                                                grpc_core::ChannelArgs());
    fixture_->InitServer(grpc_core::ChannelArgs());
    fixture_->InitClient(grpc_core::ChannelArgs());
  }
  void TearDown() override { fixture_.reset(); }

  std::unique_ptr<CoreTestFixture> fixture_;
};

TEST_P(H2SslCertTest, SimpleRequestBody) {
  simple_request_body(fixture_.get(), GetParam().result);
}

#ifndef OPENSSL_IS_BORINGSSL
#if GPR_LINUX
TEST_P(H2SslCertTest, SimpleRequestBodyUseEngine) {
  test_server1_key_id.clear();
  test_server1_key_id.append("engine:libengine_passthrough:");
  test_server1_key_id.append(test_server1_key);
  simple_request_body(fixture_.get(), GetParam().result);
}
#endif
#endif

INSTANTIATE_TEST_SUITE_P(H2SslCert, H2SslCertTest,
                         ::testing::ValuesIn(configs));

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  FILE* roots_file;
  size_t roots_size = strlen(test_root_cert);
  char* roots_filename;

  grpc::testing::TestEnvironment env(&argc, argv);
  // Set the SSL roots env var.
  roots_file =
      gpr_tmpfile("chttp2_simple_ssl_cert_fullstack_test", &roots_filename);
  GPR_ASSERT(roots_filename != nullptr);
  GPR_ASSERT(roots_file != nullptr);
  GPR_ASSERT(fwrite(test_root_cert, 1, roots_size, roots_file) == roots_size);
  fclose(roots_file);
  GPR_GLOBAL_CONFIG_SET(grpc_default_ssl_roots_file_path, roots_filename);

  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();

  // Cleanup.
  remove(roots_filename);
  gpr_free(roots_filename);

  return ret;
}
