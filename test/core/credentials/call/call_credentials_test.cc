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

#include "src/core/credentials/call/call_credentials.h"

#include <grpc/credentials.h>
#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <openssl/rsa.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "gmock/gmock.h"
#include "src/core/credentials/call/composite/composite_call_credentials.h"
#include "src/core/credentials/call/external/aws_external_account_credentials.h"
#include "src/core/credentials/call/external/external_account_credentials.h"
#include "src/core/credentials/call/external/file_external_account_credentials.h"
#include "src/core/credentials/call/external/url_external_account_credentials.h"
#include "src/core/credentials/call/gcp_service_account_identity/gcp_service_account_identity_credentials.h"
#include "src/core/credentials/call/iam/iam_credentials.h"
#include "src/core/credentials/call/jwt/jwt_credentials.h"
#include "src/core/credentials/call/oauth2/oauth2_credentials.h"
#include "src/core/credentials/transport/composite/composite_channel_credentials.h"
#include "src/core/credentials/transport/fake/fake_credentials.h"
#include "src/core/credentials/transport/google_default/google_default_credentials.h"
#include "src/core/credentials/transport/tls/grpc_tls_credentials_options.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/credentials/transport/xds/xds_credentials.h"
#include "src/core/filter/auth/auth_filters.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/promise/exec_ctx_wakeup_scheduler.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/transport/auth_context.h"
#include "src/core/util/crash.h"
#include "src/core/util/env.h"
#include "src/core/util/host_port.h"
#include "src/core/util/http_client/httpcli.h"
#include "src/core/util/http_client/httpcli_ssl_credentials.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/string.h"
#include "src/core/util/time.h"
#include "src/core/util/tmpfile.h"
#include "src/core/util/unique_type_name.h"
#include "src/core/util/uri.h"
#include "src/core/util/wait_for_single_owner.h"
#include "test/core/event_engine/event_engine_test_utils.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/test_util/test_call_creds.h"
#include "test/core/test_util/test_config.h"

// TODO(roth): Refactor this so that we can split up the individual call
// creds tests into their own files.

namespace grpc_core {

using grpc_event_engine::experimental::FuzzingEventEngine;
using internal::grpc_flush_cached_google_default_credentials;
using internal::set_gce_tenancy_checker_for_testing;

namespace {

// -- Constants. --

const char test_google_iam_authorization_token[] = "blahblahblhahb";
const char test_google_iam_authority_selector[] = "respectmyauthoritah";
const char test_oauth2_bearer_token[] = "Bearer blaaslkdjfaslkdfasdsfasf";

// This JSON key was generated with the GCE console and revoked immediately.
// The identifiers have been changed as well.
// Maximum size for a string literal is 509 chars in C89, yay!
const char test_json_key_str_part1[] =
    "{ \"private_key\": \"-----BEGIN PRIVATE KEY-----"
    "\\nMIICeAIBADANBgkqhkiG9w0BAQEFAASCAmIwggJeAgEAAoGBAOEvJsnoHnyHkXcp\\n7mJE"
    "qg"
    "WGjiw71NfXByguekSKho65FxaGbsnSM9SMQAqVk7Q2rG+I0OpsT0LrWQtZ\\nyjSeg/"
    "rWBQvS4hle4LfijkP3J5BG+"
    "IXDMP8RfziNRQsenAXDNPkY4kJCvKux2xdD\\nOnVF6N7dL3nTYZg+"
    "uQrNsMTz9UxVAgMBAAECgYEAzbLewe1xe9vy+2GoSsfib+28\\nDZgSE6Bu/"
    "zuFoPrRc6qL9p2SsnV7txrunTyJkkOnPLND9ABAXybRTlcVKP/sGgza\\n/"
    "8HpCqFYM9V8f34SBWfD4fRFT+n/"
    "73cfRUtGXdXpseva2lh8RilIQfPhNZAncenU\\ngqXjDvpkypEusgXAykECQQD+";
const char test_json_key_str_part2[] =
    "53XxNVnxBHsYb+AYEfklR96yVi8HywjVHP34+OQZ\\nCslxoHQM8s+"
    "dBnjfScLu22JqkPv04xyxmt0QAKm9+vTdAkEA4ib7YvEAn2jXzcCI\\nEkoy2L/"
    "XydR1GCHoacdfdAwiL2npOdnbvi4ZmdYRPY1LSTO058tQHKVXV7NLeCa3\\nAARh2QJBAMKeDA"
    "G"
    "W303SQv2cZTdbeaLKJbB5drz3eo3j7dDKjrTD9JupixFbzcGw\\n8FZi5c8idxiwC36kbAL6Hz"
    "A"
    "ZoX+ofI0CQE6KCzPJTtYNqyShgKAZdJ8hwOcvCZtf\\n6z8RJm0+"
    "6YBd38lfh5j8mZd7aHFf6I17j5AQY7oPEc47TjJj/"
    "5nZ68ECQQDvYuI3\\nLyK5fS8g0SYbmPOL9TlcHDOqwG0mrX9qpg5DC2fniXNSrrZ64GTDKdzZ"
    "Y"
    "Ap6LI9W\\nIqv4vr6y38N79TTC\\n-----END PRIVATE KEY-----\\n\", ";
const char test_json_key_str_part3[] =
    "\"private_key_id\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\", "
    "\"client_email\": "
    "\"777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount."
    "com\", \"client_id\": "
    "\"777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent."
    "com\", \"type\": \"service_account\" }";

// Test refresh token.
const char test_refresh_token_str[] =
    "{ \"client_id\": \"32555999999.apps.googleusercontent.com\","
    "  \"client_secret\": \"EmssLNjJy1332hD4KFsecret\","
    "  \"refresh_token\": \"1/Blahblasj424jladJDSGNf-u4Sua3HDA2ngjd42\","
    "  \"type\": \"authorized_user\"}";

const char test_external_account_credentials_psc_sts_str[] =
    "{\"type\":\"external_account\",\"audience\":\"audience\",\"subject_"
    "token_type\":\"subject_token_type\",\"service_account_impersonation_"
    "url\":\"https://sts-xyz.p.googleapis.com:5555/"
    "service_account_impersonation_url\",\"token_url\":\"https://"
    "sts-xyz-123.p.googleapis.com:5555/token\",\"token_info_url\":\"https://"
    "sts-xyz.p.googleapis.com:5555/introspect"
    "token_info\",\"credential_source\":{\"file\":\"credentials_file_path\"},"
    "\"quota_project_id\":\"quota_"
    "project_id\",\"client_id\":\"client_id\",\"client_secret\":\"client_"
    "secret\"}";

const char test_external_account_credentials_psc_iam_str[] =
    "{\"type\":\"external_account\",\"audience\":\"audience\",\"subject_"
    "token_type\":\"subject_token_type\",\"service_account_impersonation_"
    "url\":\"https://iamcredentials-xyz.p.googleapis.com:5555/"
    "service_account_impersonation_url\",\"token_url\":\"https://"
    "iamcredentials-xyz-123.p.googleapis.com:5555/"
    "token\",\"token_info_url\":\"https://"
    "iamcredentials-xyz-123.p.googleapis.com:5555/introspect"
    "token_info\",\"credential_source\":{\"file\":\"credentials_file_path\"},"
    "\"quota_project_id\":\"quota_"
    "project_id\",\"client_id\":\"client_id\",\"client_secret\":\"client_"
    "secret\"}";

const char valid_oauth2_json_response[] =
    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
    " \"expires_in\":3599, "
    " \"token_type\":\"Bearer\"}";

const char valid_sts_json_response[] =
    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
    " \"expires_in\":3599, "
    " \"issued_token_type\":\"urn:ietf:params:oauth:token-type:access_token\", "
    " \"token_type\":\"Bearer\"}";

const char test_scope[] = "perm1 perm2";

const char test_signed_jwt[] =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6ImY0OTRkN2M1YWU2MGRmOTcyNmM4YW"
    "U0MDcyZTViYTdmZDkwODg2YzcifQ";
const char test_signed_jwt_token_type[] =
    "urn:ietf:params:oauth:token-type:id_token";
const char test_signed_jwt2[] =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6ImY0OTRkN2M1YWU2MGRmOTcyNmM5YW"
    "U2MDcyZTViYTdnZDkwODg5YzcifQ";
const char test_signed_jwt_token_type2[] =
    "urn:ietf:params:oauth:token-type:jwt";
const char test_signed_jwt_path_prefix[] = "test_sign_jwt";

const char test_service_url[] = "https://foo.com/foo.v1";
const char test_service_url_no_service_name[] = "https://foo.com/";
const char other_test_service_url_no_service_name[] = "https://bar.com/";
const char test_method[] = "ThisIsNotAMethod";

const char kTestUrlScheme[] = "https";
const char kTestAuthority[] = "foo.com";
const char kTestPath[] = "/foo.v1/ThisIsNotAMethod";
const char kTestOtherAuthority[] = "bar.com";
const char kTestOtherPath[] = "/bar.v1/ThisIsNotAMethod";

const char test_sts_endpoint_url[] = "https://foo.com:5555/v1/token-exchange";

const char valid_external_account_creds_token_exchange_response[] =
    "{\"access_token\":\"token_exchange_access_token\","
    " \"expires_in\":3599,"
    " \"token_type\":\"Bearer\"}";

const char
    valid_external_account_creds_service_account_impersonation_response[] =
        "{\"accessToken\":\"service_account_impersonation_access_token\","
        " \"expireTime\":\"2050-01-01T00:00:00Z\"}";

const char
    valid_url_external_account_creds_options_credential_source_format_text[] =
        "{\"url\":\"https://foo.com:5555/generate_subject_token_format_text\","
        "\"headers\":{\"Metadata-Flavor\":\"Google\"}}";

const char
    valid_url_external_account_creds_options_credential_source_with_query_params_format_text
        [] = "{\"url\":\"https://foo.com:5555/"
             "path/to/url/creds?p1=v1&p2=v2\","
             "\"headers\":{\"Metadata-Flavor\":\"Google\"}}";

const char
    valid_url_external_account_creds_retrieve_subject_token_response_format_text
        [] = "test_subject_token";

const char
    valid_url_external_account_creds_options_credential_source_format_json[] =
        "{\"url\":\"https://foo.com:5555/generate_subject_token_format_json\","
        "\"headers\":{\"Metadata-Flavor\":\"Google\"},"
        "\"format\":{\"type\":\"json\",\"subject_token_field_name\":\"access_"
        "token\"}}";

const char
    valid_url_external_account_creds_retrieve_subject_token_response_format_json
        [] = "{\"access_token\":\"test_subject_token\"}";

const char invalid_url_external_account_creds_options_credential_source[] =
    "{\"url\":\"invalid_credential_source_url\","
    "\"headers\":{\"Metadata-Flavor\":\"Google\"}}";

const char valid_aws_external_account_creds_retrieve_signing_keys_response[] =
    "{\"AccessKeyId\":\"test_access_key_id\",\"SecretAccessKey\":"
    "\"test_secret_access_key\",\"Token\":\"test_token\"}";

const char aws_imdsv2_session_token[] = "imdsv2_session_token";

const char valid_aws_external_account_creds_options_credential_source[] =
    "{\"environment_id\":\"aws1\","
    "\"region_url\":\"https://169.254.169.254:5555/region_url\","
    "\"url\":\"https://169.254.169.254:5555/url\","
    "\"regional_cred_verification_url\":\"https://foo.com:5555/"
    "regional_cred_verification_url_{region}\"}";

const char valid_aws_imdsv2_external_account_creds_options_credential_source[] =
    "{\"environment_id\":\"aws1\","
    "\"region_url\":\"http://169.254.169.254:5555/region_url\","
    "\"url\":\"https://169.254.169.254:5555/url\","
    "\"imdsv2_session_token_url\":\"https://169.254.169.254/"
    "imdsv2_session_token_url\","
    "\"regional_cred_verification_url\":\"https://foo.com:5555/"
    "regional_cred_verification_url_{region}\"}";

const char valid_aws_external_account_creds_options_credential_source_ipv6[] =
    "{\"environment_id\":\"aws1\","
    "\"region_url\":\"https://[fd00:ec2::254]:5555/region_url\","
    "\"url\":\"http://[fd00:ec2::254]:5555/url\","
    "\"imdsv2_session_token_url\":\"https://[fd00:ec2::254]/"
    "imdsv2_session_token_url\","
    "\"regional_cred_verification_url\":\"https://foo.com:5555/"
    "regional_cred_verification_url_{region}\"}";

const char
    invalid_aws_external_account_creds_options_credential_source_unmatched_environment_id
        [] = "{\"environment_id\":\"unsupported_aws_version\","
             "\"region_url\":\"https://169.254.169.254:5555/region_url\","
             "\"url\":\"https://169.254.169.254:5555/url\","
             "\"regional_cred_verification_url\":\"https://foo.com:5555/"
             "regional_cred_verification_url_{region}\"}";

const char
    invalid_aws_external_account_creds_options_credential_source_invalid_regional_cred_verification_url
        [] = "{\"environment_id\":\"aws1\","
             "\"region_url\":\"https://169.254.169.254:5555/region_url\","
             "\"url\":\"https://169.254.169.254:5555/url\","
             "\"regional_cred_verification_url\":\"invalid_regional_cred_"
             "verification_url\"}";

const char
    invalid_aws_external_account_creds_options_credential_source_missing_role_name
        [] = "{\"environment_id\":\"aws1\","
             "\"region_url\":\"https://169.254.169.254:5555/region_url\","
             "\"url\":\"https://169.254.169.254:5555/url_no_role_name\","
             "\"regional_cred_verification_url\":\"https://foo.com:5555/"
             "regional_cred_verification_url_{region}\"}";

//  -- Global state flags. --

bool g_test_is_on_gce = false;

bool g_test_gce_tenancy_checker_called = false;

// -- Utils. --

char* test_json_key_str(void) {
  size_t result_len = strlen(test_json_key_str_part1) +
                      strlen(test_json_key_str_part2) +
                      strlen(test_json_key_str_part3);
  char* result = static_cast<char*>(gpr_malloc(result_len + 1));
  char* current = result;
  strcpy(result, test_json_key_str_part1);
  current += strlen(test_json_key_str_part1);
  strcpy(current, test_json_key_str_part2);
  current += strlen(test_json_key_str_part2);
  strcpy(current, test_json_key_str_part3);
  return result;
}

grpc_http_response http_response(int status, const char* body) {
  grpc_http_response response;
  response = {};
  response.status = status;
  response.body = gpr_strdup(const_cast<char*>(body));
  response.body_length = strlen(body);
  return response;
}

// -- Tests. --

class CredentialsTest : public ::testing::Test {
 protected:
  void SetUp() override { grpc_init(); }

  void TearDown() override { grpc_shutdown_blocking(); }
};

TEST_F(CredentialsTest, TestOauth2TokenFetcherCredsParsingOk) {
  ExecCtx exec_ctx;
  std::optional<Slice> token_value;
  Duration token_lifetime;
  grpc_http_response response = http_response(200, valid_oauth2_json_response);
  CHECK(grpc_oauth2_token_fetcher_credentials_parse_server_response(
            &response, &token_value, &token_lifetime) == GRPC_CREDENTIALS_OK);
  CHECK(token_lifetime == Duration::Seconds(3599));
  CHECK(token_value->as_string_view() ==
        "Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_");
  grpc_http_response_destroy(&response);
}

TEST_F(CredentialsTest, TestOauth2TokenFetcherCredsParsingBadHttpStatus) {
  ExecCtx exec_ctx;
  std::optional<Slice> token_value;
  Duration token_lifetime;
  grpc_http_response response = http_response(401, valid_oauth2_json_response);
  CHECK(grpc_oauth2_token_fetcher_credentials_parse_server_response(
            &response, &token_value, &token_lifetime) ==
        GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
}

TEST_F(CredentialsTest, TestOauth2TokenFetcherCredsParsingEmptyHttpBody) {
  ExecCtx exec_ctx;
  std::optional<Slice> token_value;
  Duration token_lifetime;
  grpc_http_response response = http_response(200, "");
  CHECK(grpc_oauth2_token_fetcher_credentials_parse_server_response(
            &response, &token_value, &token_lifetime) ==
        GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
}

TEST_F(CredentialsTest, TestOauth2TokenFetcherCredsParsingInvalidJson) {
  ExecCtx exec_ctx;
  std::optional<Slice> token_value;
  Duration token_lifetime;
  grpc_http_response response =
      http_response(200,
                    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
                    " \"expires_in\":3599, "
                    " \"token_type\":\"Bearer\"");
  CHECK(grpc_oauth2_token_fetcher_credentials_parse_server_response(
            &response, &token_value, &token_lifetime) ==
        GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
}

TEST_F(CredentialsTest, TestOauth2TokenFetcherCredsParsingMissingToken) {
  ExecCtx exec_ctx;
  std::optional<Slice> token_value;
  Duration token_lifetime;
  grpc_http_response response = http_response(200,
                                              "{"
                                              " \"expires_in\":3599, "
                                              " \"token_type\":\"Bearer\"}");
  CHECK(grpc_oauth2_token_fetcher_credentials_parse_server_response(
            &response, &token_value, &token_lifetime) ==
        GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
}

TEST_F(CredentialsTest, TestOauth2TokenFetcherCredsParsingMissingTokenType) {
  ExecCtx exec_ctx;
  std::optional<Slice> token_value;
  Duration token_lifetime;
  grpc_http_response response =
      http_response(200,
                    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
                    " \"expires_in\":3599, "
                    "}");
  CHECK(grpc_oauth2_token_fetcher_credentials_parse_server_response(
            &response, &token_value, &token_lifetime) ==
        GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
}

TEST_F(CredentialsTest,
       TestOauth2TokenFetcherCredsParsingMissingTokenLifetime) {
  ExecCtx exec_ctx;
  std::optional<Slice> token_value;
  Duration token_lifetime;
  grpc_http_response response =
      http_response(200,
                    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
                    " \"token_type\":\"Bearer\"}");
  CHECK(grpc_oauth2_token_fetcher_credentials_parse_server_response(
            &response, &token_value, &token_lifetime) ==
        GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
}

class RequestMetadataState : public RefCounted<RequestMetadataState> {
 public:
  static RefCountedPtr<RequestMetadataState> NewInstance(
      grpc_error_handle expected_error, std::string expected,
      std::optional<bool> expect_delay = std::nullopt) {
    return MakeRefCounted<RequestMetadataState>(
        expected_error, std::move(expected), expect_delay,
        grpc_polling_entity_create_from_pollset_set(grpc_pollset_set_create()));
  }

  RequestMetadataState(grpc_error_handle expected_error, std::string expected,
                       std::optional<bool> expect_delay,
                       grpc_polling_entity pollent)
      : expected_error_(expected_error),
        expected_(std::move(expected)),
        expect_delay_(expect_delay),
        pollent_(pollent) {}

  ~RequestMetadataState() override {
    grpc_pollset_set_destroy(grpc_polling_entity_pollset_set(&pollent_));
  }

  void RunRequestMetadataTest(grpc_call_credentials* creds,
                              const char* url_scheme, const char* authority,
                              const char* path) {
    auto self = Ref();
    get_request_metadata_args_.security_connector =
        MakeRefCounted<BogusSecurityConnector>(url_scheme);
    md_.Set(HttpAuthorityMetadata(), Slice::FromStaticString(authority));
    md_.Set(HttpPathMetadata(), Slice::FromStaticString(path));
    activity_ = MakeActivity(
        [this, creds] {
          return Seq(
              CheckDelayed(creds->GetRequestMetadata(
                  ClientMetadataHandle(&md_, Arena::PooledDeleter(nullptr)),
                  &get_request_metadata_args_)),
              [this](std::tuple<absl::StatusOr<ClientMetadataHandle>, bool>
                         metadata_and_delayed) {
                auto& metadata = std::get<0>(metadata_and_delayed);
                const bool delayed = std::get<1>(metadata_and_delayed);
                if (expect_delay_.has_value()) {
                  EXPECT_EQ(delayed, *expect_delay_);
                }
                if (metadata.ok()) {
                  EXPECT_EQ(metadata->get(), &md_);
                }
                return metadata.status();
              });
        },
        ExecCtxWakeupScheduler(),
        [self](absl::Status status) mutable {
          self->CheckRequestMetadata(
              absl_status_to_grpc_error(std::move(status)));
          self.reset();
        },
        arena_.get(), &pollent_);
  }

 private:
  // No-op security connector, exists only to inject url_scheme.
  class BogusSecurityConnector : public grpc_channel_security_connector {
   public:
    explicit BogusSecurityConnector(absl::string_view url_scheme)
        : grpc_channel_security_connector(url_scheme, nullptr, nullptr) {}

    void check_peer(tsi_peer, grpc_endpoint*, const ChannelArgs&,
                    RefCountedPtr<grpc_auth_context>*, grpc_closure*) override {
      Crash("unreachable");
    }

    void cancel_check_peer(grpc_closure*, grpc_error_handle) override {
      Crash("unreachable");
    }

    int cmp(const grpc_security_connector*) const override {
      GPR_UNREACHABLE_CODE(return 0);
    }

    ArenaPromise<absl::Status> CheckCallHost(absl::string_view,
                                             grpc_auth_context*) override {
      GPR_UNREACHABLE_CODE(
          return Immediate(absl::PermissionDeniedError("should never happen")));
    }

    void add_handshakers(const ChannelArgs&, grpc_pollset_set*,
                         HandshakeManager*) override {
      Crash("unreachable");
    }
  };

  void CheckRequestMetadata(grpc_error_handle error) {
    if (expected_error_.ok()) {
      ASSERT_TRUE(error.ok()) << error;
    } else {
      grpc_status_code actual_code;
      std::string actual_message;
      grpc_error_get_status(error, Timestamp::InfFuture(), &actual_code,
                            &actual_message, nullptr, nullptr);
      EXPECT_EQ(absl::Status(static_cast<absl::StatusCode>(actual_code),
                             actual_message),
                expected_error_);
    }
    md_.Remove(HttpAuthorityMetadata());
    md_.Remove(HttpPathMetadata());
    LOG(INFO) << "expected metadata: " << expected_;
    LOG(INFO) << "actual metadata: " << md_.DebugString();
  }

  grpc_error_handle expected_error_;
  std::string expected_;
  std::optional<bool> expect_delay_;
  RefCountedPtr<Arena> arena_ = SimpleArenaAllocator()->MakeArena();
  grpc_metadata_batch md_;
  grpc_call_credentials::GetRequestMetadataArgs get_request_metadata_args_;
  grpc_polling_entity pollent_;
  ActivityPtr activity_;
};

TEST_F(CredentialsTest, TestGoogleIamCreds) {
  ExecCtx exec_ctx;
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(),
      absl::StrCat(GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY, ": ",
                   test_google_iam_authorization_token, ", ",
                   GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY, ": ",
                   test_google_iam_authority_selector));
  grpc_call_credentials* creds = grpc_google_iam_credentials_create(
      test_google_iam_authorization_token, test_google_iam_authority_selector,
      nullptr);
  // Check security level.
  CHECK_EQ(creds->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  state->RunRequestMetadataTest(creds, kTestUrlScheme, kTestAuthority,
                                kTestPath);
  creds->Unref();
}

TEST_F(CredentialsTest, TestAccessTokenCreds) {
  ExecCtx exec_ctx;
  auto state = RequestMetadataState::NewInstance(absl::OkStatus(),
                                                 "authorization: Bearer blah");
  grpc_call_credentials* creds =
      grpc_access_token_credentials_create("blah", nullptr);
  CHECK(creds->type() == grpc_access_token_credentials::Type());
  // Check security level.
  CHECK_EQ(creds->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  state->RunRequestMetadataTest(creds, kTestUrlScheme, kTestAuthority,
                                kTestPath);
  creds->Unref();
}

class check_channel_oauth2 final : public grpc_channel_credentials {
 public:
  RefCountedPtr<grpc_channel_security_connector> create_security_connector(
      RefCountedPtr<grpc_call_credentials> call_creds, const char* /*target*/,
      ChannelArgs* /*new_args*/) override {
    CHECK(type() == Type());
    CHECK(call_creds != nullptr);
    CHECK(call_creds->type() == grpc_access_token_credentials::Type());
    return nullptr;
  }

  static UniqueTypeName Type() {
    static UniqueTypeName::Factory kFactory("check_channel_oauth2");
    return kFactory.Create();
  }

  UniqueTypeName type() const override { return Type(); }

 private:
  int cmp_impl(const grpc_channel_credentials* other) const override {
    // TODO(yashykt): Check if we can do something better here
    return QsortCompare(static_cast<const grpc_channel_credentials*>(this),
                        other);
  }
};

TEST_F(CredentialsTest, TestChannelOauth2CompositeCreds) {
  ExecCtx exec_ctx;
  ChannelArgs new_args;
  grpc_channel_credentials* channel_creds = new check_channel_oauth2();
  grpc_call_credentials* oauth2_creds =
      grpc_access_token_credentials_create("blah", nullptr);
  grpc_channel_credentials* channel_oauth2_creds =
      grpc_composite_channel_credentials_create(channel_creds, oauth2_creds,
                                                nullptr);
  grpc_channel_credentials_release(channel_creds);
  grpc_call_credentials_release(oauth2_creds);
  channel_oauth2_creds->create_security_connector(nullptr, nullptr, &new_args);
  grpc_channel_credentials_release(channel_oauth2_creds);
}

TEST_F(CredentialsTest, TestOauth2GoogleIamCompositeCreds) {
  ExecCtx exec_ctx;
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(),
      absl::StrCat(GRPC_AUTHORIZATION_METADATA_KEY, ": ",
                   test_oauth2_bearer_token, ", ",
                   GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY, ": ",
                   test_google_iam_authorization_token, ", ",
                   GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY, ": ",
                   test_google_iam_authority_selector));
  grpc_call_credentials* oauth2_creds = grpc_md_only_test_credentials_create(
      "authorization", test_oauth2_bearer_token);

  // Check security level of fake credentials.
  CHECK_EQ(oauth2_creds->min_security_level(), GRPC_SECURITY_NONE);

  grpc_call_credentials* google_iam_creds = grpc_google_iam_credentials_create(
      test_google_iam_authorization_token, test_google_iam_authority_selector,
      nullptr);
  grpc_call_credentials* composite_creds =
      grpc_composite_call_credentials_create(oauth2_creds, google_iam_creds,
                                             nullptr);
  // Check security level of composite credentials.
  CHECK_EQ(composite_creds->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);

  oauth2_creds->Unref();
  google_iam_creds->Unref();
  CHECK(composite_creds->type() == grpc_composite_call_credentials::Type());
  const grpc_composite_call_credentials::CallCredentialsList& creds_list =
      static_cast<const grpc_composite_call_credentials*>(composite_creds)
          ->inner();
  CHECK_EQ(creds_list.size(), 2);
  CHECK(creds_list[0]->type() == grpc_md_only_test_credentials::Type());
  CHECK(creds_list[1]->type() == grpc_google_iam_credentials::Type());
  state->RunRequestMetadataTest(composite_creds, kTestUrlScheme, kTestAuthority,
                                kTestPath);
  composite_creds->Unref();
}

class check_channel_oauth2_google_iam final : public grpc_channel_credentials {
 public:
  RefCountedPtr<grpc_channel_security_connector> create_security_connector(
      RefCountedPtr<grpc_call_credentials> call_creds, const char* /*target*/,
      ChannelArgs* /*new_args*/) override {
    CHECK(type() == Type());
    CHECK(call_creds != nullptr);
    CHECK(call_creds->type() == grpc_composite_call_credentials::Type());
    const grpc_composite_call_credentials::CallCredentialsList& creds_list =
        static_cast<const grpc_composite_call_credentials*>(call_creds.get())
            ->inner();
    CHECK(creds_list[0]->type() == grpc_access_token_credentials::Type());
    CHECK(creds_list[1]->type() == grpc_google_iam_credentials::Type());
    return nullptr;
  }

  static UniqueTypeName Type() {
    static UniqueTypeName::Factory kFactory("check_channel_oauth2_google_iam");
    return kFactory.Create();
  }

  UniqueTypeName type() const override { return Type(); }

 private:
  int cmp_impl(const grpc_channel_credentials* other) const override {
    // TODO(yashykt): Check if we can do something better here
    return QsortCompare(static_cast<const grpc_channel_credentials*>(this),
                        other);
  }
};

TEST_F(CredentialsTest, TestChannelOauth2GoogleIamCompositeCreds) {
  ExecCtx exec_ctx;
  ChannelArgs new_args;
  grpc_channel_credentials* channel_creds =
      new check_channel_oauth2_google_iam();
  grpc_call_credentials* oauth2_creds =
      grpc_access_token_credentials_create("blah", nullptr);
  grpc_channel_credentials* channel_oauth2_creds =
      grpc_composite_channel_credentials_create(channel_creds, oauth2_creds,
                                                nullptr);
  grpc_call_credentials* google_iam_creds = grpc_google_iam_credentials_create(
      test_google_iam_authorization_token, test_google_iam_authority_selector,
      nullptr);

  grpc_channel_credentials* channel_oauth2_iam_creds =
      grpc_composite_channel_credentials_create(channel_oauth2_creds,
                                                google_iam_creds, nullptr);
  grpc_channel_credentials_release(channel_creds);
  grpc_call_credentials_release(oauth2_creds);
  grpc_channel_credentials_release(channel_oauth2_creds);
  grpc_call_credentials_release(google_iam_creds);

  channel_oauth2_iam_creds->create_security_connector(nullptr, nullptr,
                                                      &new_args);

  grpc_channel_credentials_release(channel_oauth2_iam_creds);
}

void validate_compute_engine_http_request(const grpc_http_request* request,
                                          const URI& uri) {
  EXPECT_EQ(uri.authority(), "metadata.google.internal.");
  EXPECT_EQ(uri.path(),
            "/computeMetadata/v1/instance/service-accounts/default/token");
  ASSERT_EQ(request->hdr_count, 1);
  EXPECT_EQ(absl::string_view(request->hdrs[0].key), "Metadata-Flavor");
  EXPECT_EQ(absl::string_view(request->hdrs[0].value), "Google");
}

int compute_engine_httpcli_get_success_override(
    const grpc_http_request* request, const URI& uri, Timestamp /*deadline*/,
    grpc_closure* on_done, grpc_http_response* response) {
  validate_compute_engine_http_request(request, uri);
  *response = http_response(200, valid_oauth2_json_response);
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

int compute_engine_httpcli_get_failure_override(
    const grpc_http_request* request, const URI& uri, Timestamp /*deadline*/,
    grpc_closure* on_done, grpc_http_response* response) {
  validate_compute_engine_http_request(request, uri);
  *response = http_response(403, "Not Authorized.");
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

int httpcli_post_should_not_be_called(const grpc_http_request* /*request*/,
                                      const URI& /*uri*/,
                                      absl::string_view /*body*/,
                                      Timestamp /*deadline*/,
                                      grpc_closure* /*on_done*/,
                                      grpc_http_response* /*response*/) {
  CHECK(false) << "HTTP POST should not be called";
  return 1;
}

int httpcli_get_should_not_be_called(const grpc_http_request* /*request*/,
                                     const URI& /*uri*/, Timestamp /*deadline*/,
                                     grpc_closure* /*on_done*/,
                                     grpc_http_response* /*response*/) {
  CHECK(false) << "HTTP GET should not be called";
  return 1;
}

int httpcli_put_should_not_be_called(const grpc_http_request* /*request*/,
                                     const URI& /*uri*/,
                                     absl::string_view /*body*/,
                                     Timestamp /*deadline*/,
                                     grpc_closure* /*on_done*/,
                                     grpc_http_response* /*response*/) {
  CHECK(false) << "HTTP PUT should not be called";
  return 1;
}

TEST_F(CredentialsTest, TestComputeEngineCredsSuccess) {
  ExecCtx exec_ctx;
  std::string emd = "authorization: Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_";
  const char expected_creds_debug_string[] =
      "GoogleComputeEngineTokenFetcherCredentials{"
      "OAuth2TokenFetcherCredentials}";
  grpc_call_credentials* creds =
      grpc_google_compute_engine_credentials_create(nullptr);
  // Check security level.
  CHECK_EQ(creds->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);

  // First request: http get should be called.
  auto state = RequestMetadataState::NewInstance(absl::OkStatus(), emd);
  HttpRequest::SetOverride(compute_engine_httpcli_get_success_override,
                           httpcli_post_should_not_be_called,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds, kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();

  // Second request: the cached token should be served directly.
  state = RequestMetadataState::NewInstance(absl::OkStatus(), emd);
  HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                           httpcli_post_should_not_be_called,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds, kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();

  CHECK_EQ(strcmp(creds->debug_string().c_str(), expected_creds_debug_string),
           0);
  creds->Unref();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

TEST_F(CredentialsTest, TestComputeEngineCredsFailure) {
  ExecCtx exec_ctx;
  const char expected_creds_debug_string[] =
      "GoogleComputeEngineTokenFetcherCredentials{"
      "OAuth2TokenFetcherCredentials}";
  auto state = RequestMetadataState::NewInstance(
      // TODO(roth): This should return UNAUTHENTICATED.
      absl::UnavailableError("error parsing oauth2 token"), {});
  grpc_call_credentials* creds =
      grpc_google_compute_engine_credentials_create(nullptr);
  HttpRequest::SetOverride(compute_engine_httpcli_get_failure_override,
                           httpcli_post_should_not_be_called,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds, kTestUrlScheme, kTestAuthority,
                                kTestPath);
  CHECK_EQ(strcmp(creds->debug_string().c_str(), expected_creds_debug_string),
           0);
  creds->Unref();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

void validate_refresh_token_http_request(const grpc_http_request* request,
                                         const URI& uri,
                                         absl::string_view body) {
  // The content of the assertion is tested extensively in json_token_test.
  EXPECT_EQ(body, absl::StrFormat(GRPC_REFRESH_TOKEN_POST_BODY_FORMAT_STRING,
                                  "32555999999.apps.googleusercontent.com",
                                  "EmssLNjJy1332hD4KFsecret",
                                  "1/Blahblasj424jladJDSGNf-u4Sua3HDA2ngjd42"));
  EXPECT_EQ(uri.authority(), GRPC_GOOGLE_OAUTH2_SERVICE_HOST);
  EXPECT_EQ(uri.path(), GRPC_GOOGLE_OAUTH2_SERVICE_TOKEN_PATH);
  ASSERT_EQ(request->hdr_count, 1);
  EXPECT_EQ(absl::string_view(request->hdrs[0].key), "Content-Type");
  EXPECT_EQ(absl::string_view(request->hdrs[0].value),
            "application/x-www-form-urlencoded");
}

int refresh_token_httpcli_post_success(const grpc_http_request* request,
                                       const URI& uri, absl::string_view body,
                                       Timestamp /*deadline*/,
                                       grpc_closure* on_done,
                                       grpc_http_response* response) {
  validate_refresh_token_http_request(request, uri, body);
  *response = http_response(200, valid_oauth2_json_response);
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

int token_httpcli_post_failure(const grpc_http_request* /*request*/,
                               const URI& /*uri*/, absl::string_view /*body*/,
                               Timestamp /*deadline*/, grpc_closure* on_done,
                               grpc_http_response* response) {
  *response = http_response(403, "Not Authorized.");
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

TEST_F(CredentialsTest, TestRefreshTokenCredsSuccess) {
  ExecCtx exec_ctx;
  std::string emd = "authorization: Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_";
  const char expected_creds_debug_string[] =
      "GoogleRefreshToken{ClientID:32555999999.apps.googleusercontent.com,"
      "OAuth2TokenFetcherCredentials}";
  grpc_call_credentials* creds = grpc_google_refresh_token_credentials_create(
      test_refresh_token_str, nullptr);

  // Check security level.
  CHECK_EQ(creds->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);

  // First request: http put should be called.
  auto state = RequestMetadataState::NewInstance(absl::OkStatus(), emd);
  HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                           refresh_token_httpcli_post_success,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds, kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();

  // Second request: the cached token should be served directly.
  state = RequestMetadataState::NewInstance(absl::OkStatus(), emd);
  HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                           httpcli_post_should_not_be_called,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds, kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  CHECK_EQ(strcmp(creds->debug_string().c_str(), expected_creds_debug_string),
           0);

  creds->Unref();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

TEST_F(CredentialsTest, TestRefreshTokenCredsFailure) {
  ExecCtx exec_ctx;
  const char expected_creds_debug_string[] =
      "GoogleRefreshToken{ClientID:32555999999.apps.googleusercontent.com,"
      "OAuth2TokenFetcherCredentials}";
  auto state = RequestMetadataState::NewInstance(
      // TODO(roth): This should return UNAUTHENTICATED.
      absl::UnavailableError("error parsing oauth2 token"), {});
  grpc_call_credentials* creds = grpc_google_refresh_token_credentials_create(
      test_refresh_token_str, nullptr);
  HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                           token_httpcli_post_failure,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds, kTestUrlScheme, kTestAuthority,
                                kTestPath);
  CHECK_EQ(strcmp(creds->debug_string().c_str(), expected_creds_debug_string),
           0);

  creds->Unref();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

TEST_F(CredentialsTest, TestValidStsCredsOptions) {
  grpc_sts_credentials_options valid_options = {
      test_sts_endpoint_url,        // sts_endpoint_url
      nullptr,                      // resource
      nullptr,                      // audience
      nullptr,                      // scope
      nullptr,                      // requested_token_type
      test_signed_jwt_path_prefix,  // subject_token_path
      test_signed_jwt_token_type,   // subject_token_type
      nullptr,                      // actor_token_path
      nullptr                       // actor_token_type
  };
  absl::StatusOr<URI> sts_url = ValidateStsCredentialsOptions(&valid_options);
  CHECK_OK(sts_url);
  absl::string_view host;
  absl::string_view port;
  CHECK(SplitHostPort(sts_url->authority(), &host, &port));
  CHECK(host == "foo.com");
  CHECK(port == "5555");
}

TEST_F(CredentialsTest, TestInvalidStsCredsOptions) {
  grpc_sts_credentials_options invalid_options = {
      test_sts_endpoint_url,       // sts_endpoint_url
      nullptr,                     // resource
      nullptr,                     // audience
      nullptr,                     // scope
      nullptr,                     // requested_token_type
      nullptr,                     // subject_token_path (Required)
      test_signed_jwt_token_type,  // subject_token_type
      nullptr,                     // actor_token_path
      nullptr                      // actor_token_type
  };
  absl::StatusOr<URI> url_should_be_invalid =
      ValidateStsCredentialsOptions(&invalid_options);
  CHECK(!url_should_be_invalid.ok());

  invalid_options = {
      test_sts_endpoint_url,        // sts_endpoint_url
      nullptr,                      // resource
      nullptr,                      // audience
      nullptr,                      // scope
      nullptr,                      // requested_token_type
      test_signed_jwt_path_prefix,  // subject_token_path
      nullptr,                      // subject_token_type (Required)
      nullptr,                      // actor_token_path
      nullptr                       // actor_token_type
  };
  url_should_be_invalid = ValidateStsCredentialsOptions(&invalid_options);
  CHECK(!url_should_be_invalid.ok());

  invalid_options = {
      nullptr,                      // sts_endpoint_url (Required)
      nullptr,                      // resource
      nullptr,                      // audience
      nullptr,                      // scope
      nullptr,                      // requested_token_type
      test_signed_jwt_path_prefix,  // subject_token_path
      test_signed_jwt_token_type,   // subject_token_type (Required)
      nullptr,                      // actor_token_path
      nullptr                       // actor_token_type
  };
  url_should_be_invalid = ValidateStsCredentialsOptions(&invalid_options);
  CHECK(!url_should_be_invalid.ok());

  invalid_options = {
      "not_a_valid_uri",            // sts_endpoint_url
      nullptr,                      // resource
      nullptr,                      // audience
      nullptr,                      // scope
      nullptr,                      // requested_token_type
      test_signed_jwt_path_prefix,  // subject_token_path
      test_signed_jwt_token_type,   // subject_token_type (Required)
      nullptr,                      // actor_token_path
      nullptr                       // actor_token_type
  };
  url_should_be_invalid = ValidateStsCredentialsOptions(&invalid_options);
  CHECK(!url_should_be_invalid.ok());

  invalid_options = {
      "ftp://ftp.is.not.a.valid.scheme/bar",  // sts_endpoint_url
      nullptr,                                // resource
      nullptr,                                // audience
      nullptr,                                // scope
      nullptr,                                // requested_token_type
      test_signed_jwt_path_prefix,            // subject_token_path
      test_signed_jwt_token_type,             // subject_token_type (Required)
      nullptr,                                // actor_token_path
      nullptr                                 // actor_token_type
  };
  url_should_be_invalid = ValidateStsCredentialsOptions(&invalid_options);
  CHECK(!url_should_be_invalid.ok());
}

void assert_query_parameters(const URI& uri, absl::string_view expected_key,
                             absl::string_view expected_val) {
  const auto it = uri.query_parameter_map().find(expected_key);
  CHECK(it != uri.query_parameter_map().end());
  if (it->second != expected_val) {
    LOG(ERROR) << it->second << "!=" << expected_val;
  }
  CHECK(it->second == expected_val);
}

void validate_sts_token_http_request(const grpc_http_request* request,
                                     const URI& uri, absl::string_view body,
                                     bool expect_actor_token) {
  // Check that the body is constructed properly.
  std::string get_url_equivalent =
      absl::StrFormat("%s?%s", test_sts_endpoint_url, body);
  absl::StatusOr<URI> url = URI::Parse(get_url_equivalent);
  if (!url.ok()) {
    LOG(ERROR) << url.status();
    CHECK_OK(url);
  }
  assert_query_parameters(*url, "resource", "resource");
  assert_query_parameters(*url, "audience", "audience");
  assert_query_parameters(*url, "scope", "scope");
  assert_query_parameters(*url, "requested_token_type", "requested_token_type");
  assert_query_parameters(*url, "subject_token", test_signed_jwt);
  assert_query_parameters(*url, "subject_token_type",
                          test_signed_jwt_token_type);
  if (expect_actor_token) {
    assert_query_parameters(*url, "actor_token", test_signed_jwt2);
    assert_query_parameters(*url, "actor_token_type",
                            test_signed_jwt_token_type2);
  } else {
    CHECK(url->query_parameter_map().find("actor_token") ==
          url->query_parameter_map().end());
    CHECK(url->query_parameter_map().find("actor_token_type") ==
          url->query_parameter_map().end());
  }

  // Check the rest of the request.
  EXPECT_EQ(uri.authority(), "foo.com:5555");
  EXPECT_EQ(uri.path(), "/v1/token-exchange");
  ASSERT_EQ(request->hdr_count, 1);
  EXPECT_EQ(absl::string_view(request->hdrs[0].key), "Content-Type");
  EXPECT_EQ(absl::string_view(request->hdrs[0].value),
            "application/x-www-form-urlencoded");
}

int sts_token_httpcli_post_success(const grpc_http_request* request,
                                   const URI& uri, absl::string_view body,
                                   Timestamp /*deadline*/,
                                   grpc_closure* on_done,
                                   grpc_http_response* response) {
  validate_sts_token_http_request(request, uri, body, true);
  *response = http_response(200, valid_sts_json_response);
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

int sts_token_httpcli_post_success_no_actor_token(
    const grpc_http_request* request, const URI& uri, absl::string_view body,
    Timestamp /*deadline*/, grpc_closure* on_done,
    grpc_http_response* response) {
  validate_sts_token_http_request(request, uri, body, false);
  *response = http_response(200, valid_sts_json_response);
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

char* write_tmp_jwt_file(const char* jwt_contents) {
  char* path;
  FILE* tmp = gpr_tmpfile(test_signed_jwt_path_prefix, &path);
  CHECK_NE(path, nullptr);
  CHECK_NE(tmp, nullptr);
  size_t jwt_length = strlen(jwt_contents);
  CHECK_EQ(fwrite(jwt_contents, 1, jwt_length, tmp), jwt_length);
  fclose(tmp);
  return path;
}

TEST_F(CredentialsTest, TestStsCredsSuccess) {
  ExecCtx exec_ctx;
  std::string emd = "authorization: Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_";
  const char expected_creds_debug_string[] =
      "StsTokenFetcherCredentials{Path:/v1/"
      "token-exchange,Authority:foo.com:5555,OAuth2TokenFetcherCredentials}";
  char* subject_token_path = write_tmp_jwt_file(test_signed_jwt);
  char* actor_token_path = write_tmp_jwt_file(test_signed_jwt2);
  grpc_sts_credentials_options valid_options = {
      test_sts_endpoint_url,       // sts_endpoint_url
      "resource",                  // resource
      "audience",                  // audience
      "scope",                     // scope
      "requested_token_type",      // requested_token_type
      subject_token_path,          // subject_token_path
      test_signed_jwt_token_type,  // subject_token_type
      actor_token_path,            // actor_token_path
      test_signed_jwt_token_type2  // actor_token_type
  };
  grpc_call_credentials* creds =
      grpc_sts_credentials_create(&valid_options, nullptr);

  // Check security level.
  CHECK_EQ(creds->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);

  // First request: http put should be called.
  auto state = RequestMetadataState::NewInstance(absl::OkStatus(), emd);
  HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                           sts_token_httpcli_post_success,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds, kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();

  // Second request: the cached token should be served directly.
  state = RequestMetadataState::NewInstance(absl::OkStatus(), emd);
  HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                           httpcli_post_should_not_be_called,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds, kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  CHECK_EQ(strcmp(creds->debug_string().c_str(), expected_creds_debug_string),
           0);

  creds->Unref();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  gpr_free(subject_token_path);
  gpr_free(actor_token_path);
}

TEST_F(CredentialsTest, TestStsCredsTokenFileNotFound) {
  ExecCtx exec_ctx;
  grpc_sts_credentials_options valid_options = {
      test_sts_endpoint_url,           // sts_endpoint_url
      "resource",                      // resource
      "audience",                      // audience
      "scope",                         // scope
      "requested_token_type",          // requested_token_type
      "/some/completely/random/path",  // subject_token_path
      test_signed_jwt_token_type,      // subject_token_type
      "",                              // actor_token_path
      ""                               // actor_token_type
  };
  grpc_call_credentials* creds =
      grpc_sts_credentials_create(&valid_options, nullptr);

  // Check security level.
  CHECK_EQ(creds->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);

  auto state = RequestMetadataState::NewInstance(
      // TODO(roth): This should return UNAVAILABLE.
      absl::InternalError(
          "Failed to load file: /some/completely/random/path due to "
          "error(fdopen): No such file or directory"),
      {});
  HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                           httpcli_post_should_not_be_called,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds, kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();

  // Cleanup.
  creds->Unref();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

TEST_F(CredentialsTest, TestStsCredsNoActorTokenSuccess) {
  ExecCtx exec_ctx;
  std::string emd = "authorization: Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_";
  const char expected_creds_debug_string[] =
      "StsTokenFetcherCredentials{Path:/v1/"
      "token-exchange,Authority:foo.com:5555,OAuth2TokenFetcherCredentials}";
  char* subject_token_path = write_tmp_jwt_file(test_signed_jwt);
  grpc_sts_credentials_options valid_options = {
      test_sts_endpoint_url,       // sts_endpoint_url
      "resource",                  // resource
      "audience",                  // audience
      "scope",                     // scope
      "requested_token_type",      // requested_token_type
      subject_token_path,          // subject_token_path
      test_signed_jwt_token_type,  // subject_token_type
      "",                          // actor_token_path
      ""                           // actor_token_type
  };
  grpc_call_credentials* creds =
      grpc_sts_credentials_create(&valid_options, nullptr);

  // Check security level.
  CHECK_EQ(creds->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);

  // First request: http put should be called.
  auto state = RequestMetadataState::NewInstance(absl::OkStatus(), emd);
  HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                           sts_token_httpcli_post_success_no_actor_token,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds, kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();

  // Second request: the cached token should be served directly.
  state = RequestMetadataState::NewInstance(absl::OkStatus(), emd);
  HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                           httpcli_post_should_not_be_called,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds, kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  CHECK_EQ(strcmp(creds->debug_string().c_str(), expected_creds_debug_string),
           0);

  creds->Unref();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  gpr_free(subject_token_path);
}

TEST_F(CredentialsTest, TestStsCredsLoadTokenFailure) {
  const char expected_creds_debug_string[] =
      "StsTokenFetcherCredentials{Path:/v1/"
      "token-exchange,Authority:foo.com:5555,OAuth2TokenFetcherCredentials}";
  ExecCtx exec_ctx;
  auto state = RequestMetadataState::NewInstance(
      // TODO(roth): This should return UNAVAILABLE.
      absl::InternalError("Failed to load file: invalid_path due to "
                          "error(fdopen): No such file or directory"),
      {});
  char* test_signed_jwt_path = write_tmp_jwt_file(test_signed_jwt);
  grpc_sts_credentials_options options = {
      test_sts_endpoint_url,       // sts_endpoint_url
      "resource",                  // resource
      "audience",                  // audience
      "scope",                     // scope
      "requested_token_type",      // requested_token_type
      "invalid_path",              // subject_token_path
      test_signed_jwt_token_type,  // subject_token_type
      nullptr,                     // actor_token_path
      nullptr                      // actor_token_type
  };
  grpc_call_credentials* creds = grpc_sts_credentials_create(&options, nullptr);
  HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                           httpcli_post_should_not_be_called,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds, kTestUrlScheme, kTestAuthority,
                                kTestPath);
  CHECK_EQ(strcmp(creds->debug_string().c_str(), expected_creds_debug_string),
           0);

  creds->Unref();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  gpr_free(test_signed_jwt_path);
}

TEST_F(CredentialsTest, TestStsCredsHttpFailure) {
  const char expected_creds_debug_string[] =
      "StsTokenFetcherCredentials{Path:/v1/"
      "token-exchange,Authority:foo.com:5555,OAuth2TokenFetcherCredentials}";
  ExecCtx exec_ctx;
  auto state = RequestMetadataState::NewInstance(
      // TODO(roth): This should return UNAUTHENTICATED.
      absl::UnavailableError("error parsing oauth2 token"), {});
  char* test_signed_jwt_path = write_tmp_jwt_file(test_signed_jwt);
  grpc_sts_credentials_options valid_options = {
      test_sts_endpoint_url,       // sts_endpoint_url
      "resource",                  // resource
      "audience",                  // audience
      "scope",                     // scope
      "requested_token_type",      // requested_token_type
      test_signed_jwt_path,        // subject_token_path
      test_signed_jwt_token_type,  // subject_token_type
      nullptr,                     // actor_token_path
      nullptr                      // actor_token_type
  };
  grpc_call_credentials* creds =
      grpc_sts_credentials_create(&valid_options, nullptr);
  HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                           token_httpcli_post_failure,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds, kTestUrlScheme, kTestAuthority,
                                kTestPath);
  CHECK_EQ(strcmp(creds->debug_string().c_str(), expected_creds_debug_string),
           0);
  creds->Unref();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  gpr_free(test_signed_jwt_path);
}

void validate_jwt_encode_and_sign_params(const grpc_auth_json_key* json_key,
                                         const char* scope,
                                         gpr_timespec token_lifetime) {
  CHECK(grpc_auth_json_key_is_valid(json_key));
  CHECK_NE(json_key->private_key, nullptr);
#if OPENSSL_VERSION_NUMBER < 0x30000000L
  CHECK(RSA_check_key(json_key->private_key));
#else
  EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(json_key->private_key, NULL);
  CHECK(EVP_PKEY_private_check(ctx));
  EVP_PKEY_CTX_free(ctx);
#endif
  CHECK(json_key->type != nullptr &&
        strcmp(json_key->type, "service_account") == 0);
  CHECK(json_key->private_key_id != nullptr &&
        strcmp(json_key->private_key_id,
               "e6b5137873db8d2ef81e06a47289e6434ec8a165") == 0);
  CHECK(json_key->client_id != nullptr &&
        strcmp(json_key->client_id,
               "777-abaslkan11hlb6nmim3bpspl31ud.apps."
               "googleusercontent.com") == 0);
  CHECK(json_key->client_email != nullptr &&
        strcmp(json_key->client_email,
               "777-abaslkan11hlb6nmim3bpspl31ud@developer."
               "gserviceaccount.com") == 0);
  if (scope != nullptr) CHECK_EQ(strcmp(scope, test_scope), 0);
  CHECK_EQ(gpr_time_cmp(token_lifetime, grpc_max_auth_token_lifetime()), 0);
}

char* encode_and_sign_jwt_success(const grpc_auth_json_key* json_key,
                                  const char* audience,
                                  gpr_timespec token_lifetime,
                                  const char* scope) {
  if (strcmp(audience, test_service_url_no_service_name) != 0 &&
      strcmp(audience, other_test_service_url_no_service_name) != 0) {
    return nullptr;
  }
  validate_jwt_encode_and_sign_params(json_key, scope, token_lifetime);
  return gpr_strdup(test_signed_jwt);
}

char* encode_and_sign_jwt_failure(const grpc_auth_json_key* json_key,
                                  const char* /*audience*/,
                                  gpr_timespec token_lifetime,
                                  const char* scope) {
  validate_jwt_encode_and_sign_params(json_key, scope, token_lifetime);
  return nullptr;
}

char* encode_and_sign_jwt_should_not_be_called(
    const grpc_auth_json_key* /*json_key*/, const char* /*audience*/,
    gpr_timespec /*token_lifetime*/, const char* /*scope*/) {
  CHECK_EQ("grpc_jwt_encode_and_sign should not be called", nullptr);
  return nullptr;
}

grpc_service_account_jwt_access_credentials* creds_as_jwt(
    grpc_call_credentials* creds) {
  CHECK(creds != nullptr);
  CHECK(creds->type() == grpc_service_account_jwt_access_credentials::Type());
  return reinterpret_cast<grpc_service_account_jwt_access_credentials*>(creds);
}

TEST_F(CredentialsTest, TestJwtCredsLifetime) {
  char* json_key_string = test_json_key_str();
  const char expected_creds_debug_string_prefix[] =
      "JWTAccessCredentials{ExpirationTime:";
  // Max lifetime.
  grpc_call_credentials* jwt_creds =
      grpc_service_account_jwt_access_credentials_create(
          json_key_string, grpc_max_auth_token_lifetime(), nullptr);
  CHECK_EQ(gpr_time_cmp(creds_as_jwt(jwt_creds)->jwt_lifetime(),
                        grpc_max_auth_token_lifetime()),
           0);
  // Check security level.
  CHECK_EQ(jwt_creds->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  CHECK_EQ(strncmp(expected_creds_debug_string_prefix,
                   jwt_creds->debug_string().c_str(),
                   strlen(expected_creds_debug_string_prefix)),
           0);
  grpc_call_credentials_release(jwt_creds);

  // Shorter lifetime.
  gpr_timespec token_lifetime = {10, 0, GPR_TIMESPAN};
  CHECK_GT(gpr_time_cmp(grpc_max_auth_token_lifetime(), token_lifetime), 0);
  jwt_creds = grpc_service_account_jwt_access_credentials_create(
      json_key_string, token_lifetime, nullptr);
  CHECK_EQ(
      gpr_time_cmp(creds_as_jwt(jwt_creds)->jwt_lifetime(), token_lifetime), 0);
  CHECK_EQ(strncmp(expected_creds_debug_string_prefix,
                   jwt_creds->debug_string().c_str(),
                   strlen(expected_creds_debug_string_prefix)),
           0);
  grpc_call_credentials_release(jwt_creds);

  // Cropped lifetime.
  gpr_timespec add_to_max = {10, 0, GPR_TIMESPAN};
  token_lifetime = gpr_time_add(grpc_max_auth_token_lifetime(), add_to_max);
  jwt_creds = grpc_service_account_jwt_access_credentials_create(
      json_key_string, token_lifetime, nullptr);
  CHECK(gpr_time_cmp(creds_as_jwt(jwt_creds)->jwt_lifetime(),
                     grpc_max_auth_token_lifetime()) == 0);
  CHECK(strncmp(expected_creds_debug_string_prefix,
                jwt_creds->debug_string().c_str(),
                strlen(expected_creds_debug_string_prefix)) == 0);
  grpc_call_credentials_release(jwt_creds);

  gpr_free(json_key_string);
}

TEST_F(CredentialsTest, TestRemoveServiceFromJwtUri) {
  const char wrong_uri[] = "hello world";
  CHECK(!RemoveServiceNameFromJwtUri(wrong_uri).ok());
  const char valid_uri[] = "https://foo.com/get/";
  const char expected_uri[] = "https://foo.com/";
  auto output = RemoveServiceNameFromJwtUri(valid_uri);
  CHECK_OK(output);
  CHECK_EQ(strcmp(output->c_str(), expected_uri), 0);
}

TEST_F(CredentialsTest, TestJwtCredsSuccess) {
  const char expected_creds_debug_string_prefix[] =
      "JWTAccessCredentials{ExpirationTime:";

  char* json_key_string = test_json_key_str();
  ExecCtx exec_ctx;
  std::string expected_md_value = absl::StrCat("Bearer ", test_signed_jwt);
  std::string emd = absl::StrCat("authorization: ", expected_md_value);
  grpc_call_credentials* creds =
      grpc_service_account_jwt_access_credentials_create(
          json_key_string, grpc_max_auth_token_lifetime(), nullptr);

  // First request: jwt_encode_and_sign should be called.
  auto state = RequestMetadataState::NewInstance(absl::OkStatus(), emd);
  grpc_jwt_encode_and_sign_set_override(encode_and_sign_jwt_success);
  state->RunRequestMetadataTest(creds, kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();

  // Second request: the cached token should be served directly.
  state = RequestMetadataState::NewInstance(absl::OkStatus(), emd);
  grpc_jwt_encode_and_sign_set_override(
      encode_and_sign_jwt_should_not_be_called);
  state->RunRequestMetadataTest(creds, kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();

  // Third request: Different service url so jwt_encode_and_sign should be
  // called again (no caching).
  state = RequestMetadataState::NewInstance(absl::OkStatus(), emd);
  grpc_jwt_encode_and_sign_set_override(encode_and_sign_jwt_success);
  state->RunRequestMetadataTest(creds, kTestUrlScheme, kTestOtherAuthority,
                                kTestOtherPath);
  ExecCtx::Get()->Flush();
  CHECK_EQ(
      strncmp(expected_creds_debug_string_prefix, creds->debug_string().c_str(),
              strlen(expected_creds_debug_string_prefix)),
      0);

  creds->Unref();
  gpr_free(json_key_string);
  grpc_jwt_encode_and_sign_set_override(nullptr);
}

TEST_F(CredentialsTest, TestJwtCredsSigningFailure) {
  const char expected_creds_debug_string_prefix[] =
      "JWTAccessCredentials{ExpirationTime:";
  char* json_key_string = test_json_key_str();
  ExecCtx exec_ctx;
  auto state = RequestMetadataState::NewInstance(
      absl::UnauthenticatedError("Could not generate JWT."), {});
  grpc_call_credentials* creds =
      grpc_service_account_jwt_access_credentials_create(
          json_key_string, grpc_max_auth_token_lifetime(), nullptr);

  grpc_jwt_encode_and_sign_set_override(encode_and_sign_jwt_failure);
  state->RunRequestMetadataTest(creds, kTestUrlScheme, kTestAuthority,
                                kTestPath);

  gpr_free(json_key_string);
  CHECK_EQ(
      strncmp(expected_creds_debug_string_prefix, creds->debug_string().c_str(),
              strlen(expected_creds_debug_string_prefix)),
      0);

  creds->Unref();
  grpc_jwt_encode_and_sign_set_override(nullptr);
}

void set_google_default_creds_env_var_with_file_contents(
    const char* file_prefix, const char* contents) {
  size_t contents_len = strlen(contents);
  char* creds_file_name;
  FILE* creds_file = gpr_tmpfile(file_prefix, &creds_file_name);
  CHECK_NE(creds_file_name, nullptr);
  CHECK_NE(creds_file, nullptr);
  CHECK_EQ(fwrite(contents, 1, contents_len, creds_file), contents_len);
  fclose(creds_file);
  SetEnv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, creds_file_name);
  gpr_free(creds_file_name);
}

bool test_gce_tenancy_checker(void) {
  g_test_gce_tenancy_checker_called = true;
  return g_test_is_on_gce;
}

std::string null_well_known_creds_path_getter(void) { return ""; }

TEST_F(CredentialsTest, TestGoogleDefaultCredsAuthKey) {
  ExecCtx exec_ctx;
  grpc_composite_channel_credentials* creds;
  char* json_key = test_json_key_str();
  grpc_flush_cached_google_default_credentials();
  set_gce_tenancy_checker_for_testing(test_gce_tenancy_checker);
  g_test_gce_tenancy_checker_called = false;
  g_test_is_on_gce = true;
  set_google_default_creds_env_var_with_file_contents(
      "json_key_google_default_creds", json_key);
  grpc_override_well_known_credentials_path_getter(
      null_well_known_creds_path_getter);
  gpr_free(json_key);
  creds = reinterpret_cast<grpc_composite_channel_credentials*>(
      grpc_google_default_credentials_create(nullptr));
  auto* default_creds =
      reinterpret_cast<const grpc_google_default_channel_credentials*>(
          creds->inner_creds());
  CHECK_NE(default_creds->ssl_creds(), nullptr);
  auto* jwt =
      reinterpret_cast<const grpc_service_account_jwt_access_credentials*>(
          creds->call_creds());
  CHECK_EQ(
      strcmp(jwt->key().client_id,
             "777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent.com"),
      0);
  CHECK_EQ(g_test_gce_tenancy_checker_called, false);
  creds->Unref();
  SetEnv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, "");  // Reset.
  grpc_override_well_known_credentials_path_getter(nullptr);
}

TEST_F(CredentialsTest, TestGoogleDefaultCredsRefreshToken) {
  ExecCtx exec_ctx;
  grpc_composite_channel_credentials* creds;
  grpc_flush_cached_google_default_credentials();
  set_google_default_creds_env_var_with_file_contents(
      "refresh_token_google_default_creds", test_refresh_token_str);
  grpc_override_well_known_credentials_path_getter(
      null_well_known_creds_path_getter);
  creds = reinterpret_cast<grpc_composite_channel_credentials*>(
      grpc_google_default_credentials_create(nullptr));
  auto* default_creds =
      reinterpret_cast<const grpc_google_default_channel_credentials*>(
          creds->inner_creds());
  CHECK_NE(default_creds->ssl_creds(), nullptr);
  auto* refresh =
      reinterpret_cast<const grpc_google_refresh_token_credentials*>(
          creds->call_creds());
  CHECK_EQ(strcmp(refresh->refresh_token().client_id,
                  "32555999999.apps.googleusercontent.com"),
           0);
  creds->Unref();
  SetEnv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, "");  // Reset.
  grpc_override_well_known_credentials_path_getter(nullptr);
}

TEST_F(CredentialsTest,
       TestGoogleDefaultCredsExternalAccountCredentialsPscSts) {
  ExecCtx exec_ctx;
  grpc_composite_channel_credentials* creds;
  grpc_flush_cached_google_default_credentials();
  set_google_default_creds_env_var_with_file_contents(
      "google_default_creds_external_account_credentials_psc_sts",
      test_external_account_credentials_psc_sts_str);
  grpc_override_well_known_credentials_path_getter(
      null_well_known_creds_path_getter);
  creds = reinterpret_cast<grpc_composite_channel_credentials*>(
      grpc_google_default_credentials_create(nullptr));
  auto* default_creds =
      reinterpret_cast<const grpc_google_default_channel_credentials*>(
          creds->inner_creds());
  CHECK_NE(default_creds->ssl_creds(), nullptr);
  auto* external =
      reinterpret_cast<const ExternalAccountCredentials*>(creds->call_creds());
  CHECK_NE(external, nullptr);
  creds->Unref();
  SetEnv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, "");  // Reset.
  grpc_override_well_known_credentials_path_getter(nullptr);
}

TEST_F(CredentialsTest,
       TestGoogleDefaultCredsExternalAccountCredentialsPscIam) {
  ExecCtx exec_ctx;
  grpc_composite_channel_credentials* creds;
  grpc_flush_cached_google_default_credentials();
  set_google_default_creds_env_var_with_file_contents(
      "google_default_creds_external_account_credentials_psc_iam",
      test_external_account_credentials_psc_iam_str);
  grpc_override_well_known_credentials_path_getter(
      null_well_known_creds_path_getter);
  creds = reinterpret_cast<grpc_composite_channel_credentials*>(
      grpc_google_default_credentials_create(nullptr));
  auto* default_creds =
      reinterpret_cast<const grpc_google_default_channel_credentials*>(
          creds->inner_creds());
  CHECK_NE(default_creds->ssl_creds(), nullptr);
  auto* external =
      reinterpret_cast<const ExternalAccountCredentials*>(creds->call_creds());
  CHECK_NE(external, nullptr);
  creds->Unref();
  SetEnv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, "");  // Reset.
  grpc_override_well_known_credentials_path_getter(nullptr);
}

int default_creds_metadata_server_detection_httpcli_get_success_override(
    const grpc_http_request* /*request*/, const URI& uri,
    Timestamp /*deadline*/, grpc_closure* on_done,
    grpc_http_response* response) {
  *response = http_response(200, "");
  grpc_http_header* headers =
      static_cast<grpc_http_header*>(gpr_malloc(sizeof(*headers) * 1));
  headers[0].key = gpr_strdup("Metadata-Flavor");
  headers[0].value = gpr_strdup("Google");
  response->hdr_count = 1;
  response->hdrs = headers;
  EXPECT_EQ(uri.path(), "/");
  EXPECT_EQ(uri.authority(), "metadata.google.internal.");
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

TEST_F(CredentialsTest, TestGoogleDefaultCredsGce) {
  ExecCtx exec_ctx;
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(),
      "authorization: Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_");
  grpc_flush_cached_google_default_credentials();
  SetEnv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, "");  // Reset.
  grpc_override_well_known_credentials_path_getter(
      null_well_known_creds_path_getter);
  set_gce_tenancy_checker_for_testing(test_gce_tenancy_checker);
  g_test_gce_tenancy_checker_called = false;
  g_test_is_on_gce = true;

  // Simulate a successful detection of GCE.
  grpc_composite_channel_credentials* creds =
      reinterpret_cast<grpc_composite_channel_credentials*>(
          grpc_google_default_credentials_create(nullptr));

  // Verify that the default creds actually embeds a GCE creds.
  CHECK(creds != nullptr);
  CHECK_NE(creds->call_creds(), nullptr);
  HttpRequest::SetOverride(compute_engine_httpcli_get_success_override,
                           httpcli_post_should_not_be_called,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds->mutable_call_creds(), kTestUrlScheme,
                                kTestAuthority, kTestPath);
  ExecCtx::Get()->Flush();

  CHECK_EQ(g_test_gce_tenancy_checker_called, true);

  // Cleanup.
  creds->Unref();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  grpc_override_well_known_credentials_path_getter(nullptr);
}

TEST_F(CredentialsTest, TestGoogleDefaultCredsNonGce) {
  ExecCtx exec_ctx;
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(),
      "authorization: Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_");
  grpc_flush_cached_google_default_credentials();
  SetEnv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, "");  // Reset.
  grpc_override_well_known_credentials_path_getter(
      null_well_known_creds_path_getter);
  set_gce_tenancy_checker_for_testing(test_gce_tenancy_checker);
  g_test_gce_tenancy_checker_called = false;
  g_test_is_on_gce = false;
  // Simulate a successful detection of metadata server.
  HttpRequest::SetOverride(
      default_creds_metadata_server_detection_httpcli_get_success_override,
      httpcli_post_should_not_be_called, httpcli_put_should_not_be_called);
  grpc_composite_channel_credentials* creds =
      reinterpret_cast<grpc_composite_channel_credentials*>(
          grpc_google_default_credentials_create(nullptr));
  // Verify that the default creds actually embeds a GCE creds.
  CHECK(creds != nullptr);
  CHECK_NE(creds->call_creds(), nullptr);
  HttpRequest::SetOverride(compute_engine_httpcli_get_success_override,
                           httpcli_post_should_not_be_called,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds->mutable_call_creds(), kTestUrlScheme,
                                kTestAuthority, kTestPath);
  ExecCtx::Get()->Flush();
  CHECK_EQ(g_test_gce_tenancy_checker_called, true);
  // Cleanup.
  creds->Unref();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  grpc_override_well_known_credentials_path_getter(nullptr);
}

int default_creds_gce_detection_httpcli_get_failure_override(
    const grpc_http_request* /*request*/, const URI& uri,
    Timestamp /*deadline*/, grpc_closure* on_done,
    grpc_http_response* response) {
  // No magic header.
  EXPECT_EQ(uri.path(), "/");
  EXPECT_EQ(uri.authority(), "metadata.google.internal.");
  *response = http_response(200, "");
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

TEST_F(CredentialsTest, TestNoGoogleDefaultCreds) {
  grpc_flush_cached_google_default_credentials();
  SetEnv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, "");  // Reset.
  grpc_override_well_known_credentials_path_getter(
      null_well_known_creds_path_getter);
  set_gce_tenancy_checker_for_testing(test_gce_tenancy_checker);
  g_test_gce_tenancy_checker_called = false;
  g_test_is_on_gce = false;
  HttpRequest::SetOverride(
      default_creds_gce_detection_httpcli_get_failure_override,
      httpcli_post_should_not_be_called, httpcli_put_should_not_be_called);
  // Simulate a successful detection of GCE.
  CHECK_EQ(grpc_google_default_credentials_create(nullptr), nullptr);
  // Try a second one. GCE detection should occur again.
  g_test_gce_tenancy_checker_called = false;
  CHECK_EQ(grpc_google_default_credentials_create(nullptr), nullptr);
  CHECK_EQ(g_test_gce_tenancy_checker_called, true);
  // Cleanup.
  grpc_override_well_known_credentials_path_getter(nullptr);
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

TEST_F(CredentialsTest, TestGoogleDefaultCredsCallCredsSpecified) {
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(),
      "authorization: Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_");
  ExecCtx exec_ctx;
  grpc_flush_cached_google_default_credentials();
  grpc_call_credentials* call_creds =
      grpc_google_compute_engine_credentials_create(nullptr);
  set_gce_tenancy_checker_for_testing(test_gce_tenancy_checker);
  g_test_gce_tenancy_checker_called = false;
  g_test_is_on_gce = true;
  HttpRequest::SetOverride(
      default_creds_metadata_server_detection_httpcli_get_success_override,
      httpcli_post_should_not_be_called, httpcli_put_should_not_be_called);
  grpc_composite_channel_credentials* channel_creds =
      reinterpret_cast<grpc_composite_channel_credentials*>(
          grpc_google_default_credentials_create(call_creds));
  CHECK_EQ(g_test_gce_tenancy_checker_called, false);
  CHECK_NE(channel_creds, nullptr);
  CHECK_NE(channel_creds->call_creds(), nullptr);
  HttpRequest::SetOverride(compute_engine_httpcli_get_success_override,
                           httpcli_post_should_not_be_called,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(channel_creds->mutable_call_creds(),
                                kTestUrlScheme, kTestAuthority, kTestPath);
  ExecCtx::Get()->Flush();
  channel_creds->Unref();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

struct fake_call_creds : public grpc_call_credentials {
 public:
  void Orphaned() override {}

  ArenaPromise<absl::StatusOr<ClientMetadataHandle>> GetRequestMetadata(
      ClientMetadataHandle initial_metadata,
      const grpc_call_credentials::GetRequestMetadataArgs*) override {
    initial_metadata->Append("foo", Slice::FromStaticString("oof"),
                             [](absl::string_view, const Slice&) { abort(); });
    return Immediate(std::move(initial_metadata));
  }

  UniqueTypeName type() const override {
    static UniqueTypeName::Factory kFactory("fake");
    return kFactory.Create();
  }

 private:
  int cmp_impl(const grpc_call_credentials* other) const override {
    // TODO(yashykt): Check if we can do something better here
    return QsortCompare(static_cast<const grpc_call_credentials*>(this), other);
  }
};

TEST_F(CredentialsTest, TestGoogleDefaultCredsNotDefault) {
  auto state = RequestMetadataState::NewInstance(absl::OkStatus(), "foo: oof");
  ExecCtx exec_ctx;
  grpc_flush_cached_google_default_credentials();
  RefCountedPtr<grpc_call_credentials> call_creds =
      MakeRefCounted<fake_call_creds>();
  set_gce_tenancy_checker_for_testing(test_gce_tenancy_checker);
  g_test_gce_tenancy_checker_called = false;
  g_test_is_on_gce = true;
  HttpRequest::SetOverride(
      default_creds_metadata_server_detection_httpcli_get_success_override,
      httpcli_post_should_not_be_called, httpcli_put_should_not_be_called);
  grpc_composite_channel_credentials* channel_creds =
      reinterpret_cast<grpc_composite_channel_credentials*>(
          grpc_google_default_credentials_create(call_creds.release()));
  CHECK_EQ(g_test_gce_tenancy_checker_called, false);
  CHECK_NE(channel_creds, nullptr);
  CHECK_NE(channel_creds->call_creds(), nullptr);
  state->RunRequestMetadataTest(channel_creds->mutable_call_creds(),
                                kTestUrlScheme, kTestAuthority, kTestPath);
  ExecCtx::Get()->Flush();
  channel_creds->Unref();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

typedef enum {
  PLUGIN_INITIAL_STATE,
  PLUGIN_GET_METADATA_CALLED_STATE,
  PLUGIN_DESTROY_CALLED_STATE
} plugin_state;

const std::map<std::string, std::string> plugin_md = {{"foo", "bar"},
                                                      {"hi", "there"}};

int plugin_get_metadata_success(
    void* state, grpc_auth_metadata_context context,
    grpc_credentials_plugin_metadata_cb /*cb*/, void* /*user_data*/,
    grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
    size_t* num_creds_md, grpc_status_code* /*status*/,
    const char** /*error_details*/) {
  CHECK_EQ(strcmp(context.service_url, test_service_url), 0);
  CHECK_EQ(strcmp(context.method_name, test_method), 0);
  CHECK_EQ(context.channel_auth_context, nullptr);
  CHECK_EQ(context.reserved, nullptr);
  CHECK_LT(plugin_md.size(), GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX);
  plugin_state* s = static_cast<plugin_state*>(state);
  *s = PLUGIN_GET_METADATA_CALLED_STATE;
  size_t i = 0;
  for (auto const& md : plugin_md) {
    memset(&creds_md[i], 0, sizeof(grpc_metadata));
    creds_md[i].key = grpc_slice_from_copied_string(md.first.c_str());
    creds_md[i].value = grpc_slice_from_copied_string(md.second.c_str());
    i += 1;
  }
  *num_creds_md = plugin_md.size();
  return true;  // Synchronous return.
}

const char* plugin_error_details = "Could not get metadata for plugin.";

int plugin_get_metadata_failure(
    void* state, grpc_auth_metadata_context context,
    grpc_credentials_plugin_metadata_cb /*cb*/, void* /*user_data*/,
    grpc_metadata /*creds_md*/[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
    size_t* /*num_creds_md*/, grpc_status_code* status,
    const char** error_details) {
  CHECK_EQ(strcmp(context.service_url, test_service_url), 0);
  CHECK_EQ(strcmp(context.method_name, test_method), 0);
  CHECK_EQ(context.channel_auth_context, nullptr);
  CHECK_EQ(context.reserved, nullptr);
  plugin_state* s = static_cast<plugin_state*>(state);
  *s = PLUGIN_GET_METADATA_CALLED_STATE;
  *status = GRPC_STATUS_UNAUTHENTICATED;
  *error_details = gpr_strdup(plugin_error_details);
  return true;  // Synchronous return.
}

void plugin_destroy(void* state) {
  plugin_state* s = static_cast<plugin_state*>(state);
  *s = PLUGIN_DESTROY_CALLED_STATE;
}

char* plugin_debug_string(void* state) {
  plugin_state* s = static_cast<plugin_state*>(state);
  char* ret = nullptr;
  switch (*s) {
    case PLUGIN_INITIAL_STATE:
      gpr_asprintf(&ret, "TestPluginCredentials{state:INITIAL}");
      break;
    case PLUGIN_GET_METADATA_CALLED_STATE:
      gpr_asprintf(&ret, "TestPluginCredentials{state:GET_METADATA_CALLED}");
      break;
    case PLUGIN_DESTROY_CALLED_STATE:
      gpr_asprintf(&ret, "TestPluginCredentials{state:DESTROY}");
      break;
    default:
      gpr_asprintf(&ret, "TestPluginCredentials{state:UNKNOWN}");
      break;
  }
  return ret;
}

TEST_F(CredentialsTest, TestMetadataPluginSuccess) {
  const char expected_creds_debug_string[] =
      "TestPluginCredentials{state:GET_METADATA_CALLED}";
  plugin_state state = PLUGIN_INITIAL_STATE;
  grpc_metadata_credentials_plugin plugin;
  ExecCtx exec_ctx;
  auto md_state = RequestMetadataState::NewInstance(absl::OkStatus(),
                                                    "foo: bar, hi: there");

  plugin.state = &state;
  plugin.get_metadata = plugin_get_metadata_success;
  plugin.destroy = plugin_destroy;
  plugin.debug_string = plugin_debug_string;

  grpc_call_credentials* creds = grpc_metadata_credentials_create_from_plugin(
      plugin, GRPC_PRIVACY_AND_INTEGRITY, nullptr);
  // Check security level.
  CHECK_EQ(creds->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  CHECK_EQ(state, PLUGIN_INITIAL_STATE);
  md_state->RunRequestMetadataTest(creds, kTestUrlScheme, kTestAuthority,
                                   kTestPath);
  CHECK_EQ(state, PLUGIN_GET_METADATA_CALLED_STATE);
  CHECK_EQ(strcmp(creds->debug_string().c_str(), expected_creds_debug_string),
           0);
  creds->Unref();

  CHECK_EQ(state, PLUGIN_DESTROY_CALLED_STATE);
}

TEST_F(CredentialsTest, TestMetadataPluginFailure) {
  const char expected_creds_debug_string[] =
      "TestPluginCredentials{state:GET_METADATA_CALLED}";

  plugin_state state = PLUGIN_INITIAL_STATE;
  grpc_metadata_credentials_plugin plugin;
  ExecCtx exec_ctx;
  auto md_state = RequestMetadataState::NewInstance(
      // TODO(roth): Is this the right status to use here?
      absl::UnavailableError(
          absl::StrCat("Getting metadata from plugin failed with error: ",
                       plugin_error_details)),
      {});

  plugin.state = &state;
  plugin.get_metadata = plugin_get_metadata_failure;
  plugin.destroy = plugin_destroy;
  plugin.debug_string = plugin_debug_string;

  grpc_call_credentials* creds = grpc_metadata_credentials_create_from_plugin(
      plugin, GRPC_PRIVACY_AND_INTEGRITY, nullptr);
  CHECK_EQ(state, PLUGIN_INITIAL_STATE);
  md_state->RunRequestMetadataTest(creds, kTestUrlScheme, kTestAuthority,
                                   kTestPath);
  CHECK_EQ(state, PLUGIN_GET_METADATA_CALLED_STATE);
  CHECK_EQ(strcmp(creds->debug_string().c_str(), expected_creds_debug_string),
           0);
  creds->Unref();

  CHECK_EQ(state, PLUGIN_DESTROY_CALLED_STATE);
}

TEST_F(CredentialsTest, TestGetWellKnownGoogleCredentialsFilePath) {
  auto home = GetEnv("HOME");
  bool restore_home_env = false;
#if defined(GRPC_BAZEL_BUILD) && \
    (defined(GPR_POSIX_ENV) || defined(GPR_LINUX_ENV))
  // when running under bazel locally, the HOME variable is not set
  // so we set it to some fake value
  restore_home_env = true;
  SetEnv("HOME", "/fake/home/for/bazel");
#endif  // defined(GRPC_BAZEL_BUILD) && (defined(GPR_POSIX_ENV) ||
        // defined(GPR_LINUX_ENV))
  std::string path = grpc_get_well_known_google_credentials_file_path();
  CHECK(!path.empty());
#if defined(GPR_POSIX_ENV) || defined(GPR_LINUX_ENV)
  restore_home_env = true;
  UnsetEnv("HOME");
  path = grpc_get_well_known_google_credentials_file_path();
  CHECK(path.empty());
#endif  // GPR_POSIX_ENV || GPR_LINUX_ENV
  if (restore_home_env) {
    SetOrUnsetEnv("HOME", home);
  }
}

TEST_F(CredentialsTest, TestChannelCredsDuplicateWithoutCallCreds) {
  const char expected_creds_debug_string[] =
      "AccessTokenCredentials{Token:present}";
  ExecCtx exec_ctx;

  grpc_channel_credentials* channel_creds =
      grpc_fake_transport_security_credentials_create();

  RefCountedPtr<grpc_channel_credentials> dup =
      channel_creds->duplicate_without_call_credentials();
  CHECK(dup == channel_creds);
  dup.reset();

  grpc_call_credentials* call_creds =
      grpc_access_token_credentials_create("blah", nullptr);
  grpc_channel_credentials* composite_creds =
      grpc_composite_channel_credentials_create(channel_creds, call_creds,
                                                nullptr);
  CHECK_EQ(
      strcmp(call_creds->debug_string().c_str(), expected_creds_debug_string),
      0);

  call_creds->Unref();
  dup = composite_creds->duplicate_without_call_credentials();
  CHECK(dup == channel_creds);
  dup.reset();

  channel_creds->Unref();
  composite_creds->Unref();
}

typedef struct {
  const char* url_scheme;
  const char* call_host;
  const char* call_method;
  const char* desired_service_url;
  const char* desired_method_name;
} auth_metadata_context_test_case;

void auth_metadata_context_build(const char* url_scheme,
                                 const grpc_slice& call_host,
                                 const grpc_slice& call_method,
                                 grpc_auth_context* auth_context,
                                 grpc_auth_metadata_context* auth_md_context) {
  char* service = grpc_slice_to_c_string(call_method);
  char* last_slash = strrchr(service, '/');
  char* method_name = nullptr;
  char* service_url = nullptr;
  grpc_auth_metadata_context_reset(auth_md_context);
  if (last_slash == nullptr) {
    LOG(ERROR) << "No '/' found in fully qualified method name";
    service[0] = '\0';
    method_name = gpr_strdup("");
  } else if (last_slash == service) {
    method_name = gpr_strdup("");
  } else {
    *last_slash = '\0';
    method_name = gpr_strdup(last_slash + 1);
  }
  char* host_and_port = grpc_slice_to_c_string(call_host);
  if (url_scheme != nullptr && strcmp(url_scheme, GRPC_SSL_URL_SCHEME) == 0) {
    // Remove the port if it is 443.
    char* port_delimiter = strrchr(host_and_port, ':');
    if (port_delimiter != nullptr && strcmp(port_delimiter + 1, "443") == 0) {
      *port_delimiter = '\0';
    }
  }
  gpr_asprintf(&service_url, "%s://%s%s",
               url_scheme == nullptr ? "" : url_scheme, host_and_port, service);
  auth_md_context->service_url = service_url;
  auth_md_context->method_name = method_name;
  auth_md_context->channel_auth_context =
      auth_context == nullptr
          ? nullptr
          : auth_context->Ref(DEBUG_LOCATION, "grpc_auth_metadata_context")
                .release();
  gpr_free(service);
  gpr_free(host_and_port);
}

TEST_F(CredentialsTest, TestAuthMetadataContext) {
  auth_metadata_context_test_case test_cases[] = {
      // No service nor method.
      {"https", "www.foo.com", "", "https://www.foo.com", ""},
      // No method.
      {"https", "www.foo.com", "/Service", "https://www.foo.com/Service", ""},
      // Empty service and method.
      {"https", "www.foo.com", "//", "https://www.foo.com/", ""},
      // Empty method.
      {"https", "www.foo.com", "/Service/", "https://www.foo.com/Service", ""},
      // Malformed url.
      {"https", "www.foo.com:", "/Service/", "https://www.foo.com:/Service",
       ""},
      // https, default explicit port.
      {"https", "www.foo.com:443", "/Service/FooMethod",
       "https://www.foo.com/Service", "FooMethod"},
      // https, default implicit port.
      {"https", "www.foo.com", "/Service/FooMethod",
       "https://www.foo.com/Service", "FooMethod"},
      // https with ipv6 literal, default explicit port.
      {"https", "[1080:0:0:0:8:800:200C:417A]:443", "/Service/FooMethod",
       "https://[1080:0:0:0:8:800:200C:417A]/Service", "FooMethod"},
      // https with ipv6 literal, default implicit port.
      {"https", "[1080:0:0:0:8:800:200C:443]", "/Service/FooMethod",
       "https://[1080:0:0:0:8:800:200C:443]/Service", "FooMethod"},
      // https, custom port.
      {"https", "www.foo.com:8888", "/Service/FooMethod",
       "https://www.foo.com:8888/Service", "FooMethod"},
      // https with ipv6 literal, custom port.
      {"https", "[1080:0:0:0:8:800:200C:417A]:8888", "/Service/FooMethod",
       "https://[1080:0:0:0:8:800:200C:417A]:8888/Service", "FooMethod"},
      // custom url scheme, https default port.
      {"blah", "www.foo.com:443", "/Service/FooMethod",
       "blah://www.foo.com:443/Service", "FooMethod"}};
  for (uint32_t i = 0; i < GPR_ARRAY_SIZE(test_cases); i++) {
    const char* url_scheme = test_cases[i].url_scheme;
    grpc_slice call_host =
        grpc_slice_from_copied_string(test_cases[i].call_host);
    grpc_slice call_method =
        grpc_slice_from_copied_string(test_cases[i].call_method);
    grpc_auth_metadata_context auth_md_context;
    memset(&auth_md_context, 0, sizeof(auth_md_context));
    auth_metadata_context_build(url_scheme, call_host, call_method, nullptr,
                                &auth_md_context);
    if (strcmp(auth_md_context.service_url,
               test_cases[i].desired_service_url) != 0) {
      Crash(absl::StrFormat("Invalid service url, want: %s, got %s.",
                            test_cases[i].desired_service_url,
                            auth_md_context.service_url));
    }
    if (strcmp(auth_md_context.method_name,
               test_cases[i].desired_method_name) != 0) {
      Crash(absl::StrFormat("Invalid method name, want: %s, got %s.",
                            test_cases[i].desired_method_name,
                            auth_md_context.method_name));
    }
    CHECK_EQ(auth_md_context.channel_auth_context, nullptr);
    grpc_slice_unref(call_host);
    grpc_slice_unref(call_method);
    grpc_auth_metadata_context_reset(&auth_md_context);
  }
}

void validate_external_account_creds_token_exchange_request(
    const grpc_http_request* request, const URI& request_uri,
    absl::string_view body) {
  // Check that the body is constructed properly.
  std::string get_url_equivalent =
      absl::StrFormat("%s?%s", "https://foo.com:5555/token", body);
  absl::StatusOr<URI> uri = URI::Parse(get_url_equivalent);
  if (!uri.ok()) {
    LOG(ERROR) << uri.status().ToString();
    CHECK_OK(uri);
  }
  assert_query_parameters(*uri, "audience", "audience");
  assert_query_parameters(*uri, "grant_type",
                          "urn:ietf:params:oauth:grant-type:token-exchange");
  assert_query_parameters(*uri, "requested_token_type",
                          "urn:ietf:params:oauth:token-type:access_token");
  assert_query_parameters(*uri, "subject_token", "test_subject_token");
  assert_query_parameters(*uri, "subject_token_type", "subject_token_type");
  assert_query_parameters(*uri, "scope",
                          "https://www.googleapis.com/auth/cloud-platform");
  // Check the rest of the request.
  EXPECT_EQ(request_uri.authority(), "foo.com:5555");
  EXPECT_EQ(request_uri.path(), "/token");
  ASSERT_EQ(request->hdr_count, 3);
  EXPECT_EQ(absl::string_view(request->hdrs[0].key), "Content-Type");
  EXPECT_EQ(absl::string_view(request->hdrs[0].value),
            "application/x-www-form-urlencoded");
  EXPECT_EQ(absl::string_view(request->hdrs[2].key), "Authorization");
  EXPECT_EQ(absl::string_view(request->hdrs[2].value),
            "Basic Y2xpZW50X2lkOmNsaWVudF9zZWNyZXQ=");
}

void validate_external_account_creds_token_exchange_request_with_url_encode(
    const grpc_http_request* request, const URI& uri, absl::string_view body) {
  // Check that the body is constructed properly.
  EXPECT_EQ(
      body,
      "audience=audience_!%40%23%24&grant_type=urn%3Aietf%3Aparams%3Aoauth%"
      "3Agrant-type%3Atoken-exchange&requested_token_type=urn%3Aietf%"
      "3Aparams%3Aoauth%3Atoken-type%3Aaccess_token&subject_token_type="
      "subject_token_type_!%40%23%24&subject_token=test_subject_token&"
      "scope=https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fcloud-platform&"
      "options=%7B%7D");
  // Check the rest of the request.
  EXPECT_EQ(uri.authority(), "foo.com:5555");
  EXPECT_EQ(uri.path(), "/token_url_encode");
  ASSERT_EQ(request->hdr_count, 3);
  EXPECT_EQ(absl::string_view(request->hdrs[0].key), "Content-Type");
  EXPECT_EQ(absl::string_view(request->hdrs[0].value),
            "application/x-www-form-urlencoded");
  EXPECT_EQ(absl::string_view(request->hdrs[2].key), "Authorization");
  EXPECT_EQ(absl::string_view(request->hdrs[2].value),
            "Basic Y2xpZW50X2lkOmNsaWVudF9zZWNyZXQ=");
}

void validate_external_account_creds_service_account_impersonation_request(
    const grpc_http_request* request, const URI& uri, absl::string_view body) {
  // Check that the body is constructed properly.
  EXPECT_EQ(body, "scope=scope_1%20scope_2&lifetime=3600s");
  // Check the rest of the request.
  EXPECT_EQ(uri.authority(), "foo.com:5555");
  EXPECT_EQ(uri.path(), "/service_account_impersonation");
  ASSERT_EQ(request->hdr_count, 2);
  EXPECT_EQ(absl::string_view(request->hdrs[0].key), "Content-Type");
  EXPECT_EQ(absl::string_view(request->hdrs[0].value),
            "application/x-www-form-urlencoded");
  EXPECT_EQ(absl::string_view(request->hdrs[1].key), "Authorization");
  EXPECT_EQ(absl::string_view(request->hdrs[1].value),
            "Bearer token_exchange_access_token");
}

void validate_external_account_creds_serv_acc_imp_custom_lifetime_request(
    const grpc_http_request* request, const URI& uri, absl::string_view body) {
  // Check that the body is constructed properly.
  EXPECT_EQ(body, "scope=scope_1%20scope_2&lifetime=1800s");
  // Check the rest of the request.
  EXPECT_EQ(uri.authority(), "foo.com:5555");
  EXPECT_EQ(uri.path(), "/service_account_impersonation");
  ASSERT_EQ(request->hdr_count, 2);
  EXPECT_EQ(absl::string_view(request->hdrs[0].key), "Content-Type");
  EXPECT_EQ(absl::string_view(request->hdrs[0].value),
            "application/x-www-form-urlencoded");
  EXPECT_EQ(absl::string_view(request->hdrs[1].key), "Authorization");
  EXPECT_EQ(absl::string_view(request->hdrs[1].value),
            "Bearer token_exchange_access_token");
}

int external_acc_creds_serv_acc_imp_custom_lifetime_httpcli_post_success(
    const grpc_http_request* request, const URI& uri, absl::string_view body,
    Timestamp /*deadline*/, grpc_closure* on_done,
    grpc_http_response* response) {
  if (uri.path() == "/token") {
    validate_external_account_creds_token_exchange_request(request, uri, body);
    *response = http_response(
        200, valid_external_account_creds_token_exchange_response);
  } else if (uri.path() == "/service_account_impersonation") {
    validate_external_account_creds_serv_acc_imp_custom_lifetime_request(
        request, uri, body);
    *response = http_response(
        200,
        valid_external_account_creds_service_account_impersonation_response);
  }
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

int external_account_creds_httpcli_post_success(
    const grpc_http_request* request, const URI& uri, absl::string_view body,
    Timestamp /*deadline*/, grpc_closure* on_done,
    grpc_http_response* response) {
  if (uri.path() == "/token") {
    validate_external_account_creds_token_exchange_request(request, uri, body);
    *response = http_response(
        200, valid_external_account_creds_token_exchange_response);
  } else if (uri.path() == "/service_account_impersonation") {
    validate_external_account_creds_service_account_impersonation_request(
        request, uri, body);
    *response = http_response(
        200,
        valid_external_account_creds_service_account_impersonation_response);
  } else if (uri.path() == "/token_url_encode") {
    validate_external_account_creds_token_exchange_request_with_url_encode(
        request, uri, body);
    *response = http_response(
        200, valid_external_account_creds_token_exchange_response);
  }
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

int external_account_creds_httpcli_post_failure_token_exchange_response_missing_access_token(
    const grpc_http_request* /*request*/, const URI& uri,
    absl::string_view /*body*/, Timestamp /*deadline*/, grpc_closure* on_done,
    grpc_http_response* response) {
  if (uri.path() == "/token") {
    *response = http_response(200,
                              "{\"not_access_token\":\"not_access_token\","
                              "\"expires_in\":3599,"
                              " \"token_type\":\"Bearer\"}");
  } else if (uri.path() == "/service_account_impersonation") {
    *response = http_response(
        200,
        valid_external_account_creds_service_account_impersonation_response);
  }
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

int url_external_account_creds_httpcli_get_success(
    const grpc_http_request* /*request*/, const URI& uri,
    Timestamp /*deadline*/, grpc_closure* on_done,
    grpc_http_response* response) {
  if (uri.path() == "/generate_subject_token_format_text") {
    *response = http_response(
        200,
        valid_url_external_account_creds_retrieve_subject_token_response_format_text);
  } else if (uri.path() == "/path/to/url/creds?p1=v1&p2=v2") {
    *response = http_response(
        200,
        valid_url_external_account_creds_retrieve_subject_token_response_format_text);
  } else if (uri.path() == "/generate_subject_token_format_json") {
    *response = http_response(
        200,
        valid_url_external_account_creds_retrieve_subject_token_response_format_json);
  }
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

void validate_aws_external_account_creds_token_exchange_request(
    const grpc_http_request* request, const URI& request_uri,
    absl::string_view body) {
  // Check that the regional_cred_verification_url got constructed
  // with the correct AWS Region ("test_regionz" or "test_region").
  EXPECT_NE(body.find("regional_cred_verification_url_test_region"), body.npos);
  std::string get_url_equivalent =
      absl::StrFormat("%s?%s", "https://foo.com:5555/token", body);
  absl::StatusOr<URI> uri = URI::Parse(get_url_equivalent);
  CHECK_OK(uri);
  assert_query_parameters(*uri, "audience", "audience");
  assert_query_parameters(*uri, "grant_type",
                          "urn:ietf:params:oauth:grant-type:token-exchange");
  assert_query_parameters(*uri, "requested_token_type",
                          "urn:ietf:params:oauth:token-type:access_token");
  assert_query_parameters(*uri, "subject_token_type", "subject_token_type");
  assert_query_parameters(*uri, "scope",
                          "https://www.googleapis.com/auth/cloud-platform");
  // Check the rest of the request.
  EXPECT_EQ(request_uri.authority(), "foo.com:5555");
  EXPECT_EQ(request_uri.path(), "/token");
  ASSERT_EQ(request->hdr_count, 3);
  EXPECT_EQ(absl::string_view(request->hdrs[0].key), "Content-Type");
  EXPECT_EQ(absl::string_view(request->hdrs[0].value),
            "application/x-www-form-urlencoded");
  EXPECT_EQ(absl::string_view(request->hdrs[1].key), "x-goog-api-client");
  EXPECT_EQ(
      absl::string_view(request->hdrs[1].value),
      absl::StrFormat("gl-cpp/unknown auth/%s google-byoid-sdk source/aws "
                      "sa-impersonation/false config-lifetime/false",
                      grpc_version_string()));
  EXPECT_EQ(absl::string_view(request->hdrs[2].key), "Authorization");
  EXPECT_EQ(absl::string_view(request->hdrs[2].value),
            "Basic Y2xpZW50X2lkOmNsaWVudF9zZWNyZXQ=");
}

int aws_external_account_creds_httpcli_get_success(
    const grpc_http_request* /*request*/, const URI& uri,
    Timestamp /*deadline*/, grpc_closure* on_done,
    grpc_http_response* response) {
  if (uri.path() == "/region_url") {
    *response = http_response(200, "test_regionz");
  } else if (uri.path() == "/url") {
    *response = http_response(200, "test_role_name");
  } else if (uri.path() == "/url_no_role_name") {
    *response = http_response(200, "");
  } else if (uri.path() == "/url/test_role_name") {
    *response = http_response(
        200, valid_aws_external_account_creds_retrieve_signing_keys_response);
  }
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

int aws_imdsv2_external_account_creds_httpcli_get_success(
    const grpc_http_request* request, const URI& uri, Timestamp deadline,
    grpc_closure* on_done, grpc_http_response* response) {
  EXPECT_EQ(request->hdr_count, 1);
  if (request->hdr_count == 1) {
    EXPECT_EQ(absl::string_view(request->hdrs[0].key),
              "x-aws-ec2-metadata-token");
    EXPECT_EQ(absl::string_view(request->hdrs[0].value),
              aws_imdsv2_session_token);
  }
  return aws_external_account_creds_httpcli_get_success(request, uri, deadline,
                                                        on_done, response);
}

int aws_imdsv2_external_account_creds_httpcli_put_success(
    const grpc_http_request* request, const URI& uri,
    absl::string_view /*body*/, Timestamp /*deadline*/, grpc_closure* on_done,
    grpc_http_response* response) {
  EXPECT_EQ(request->hdr_count, 1);
  if (request->hdr_count == 1) {
    EXPECT_EQ(absl::string_view(request->hdrs[0].key),
              "x-aws-ec2-metadata-token-ttl-seconds");
    EXPECT_EQ(absl::string_view(request->hdrs[0].value), "300");
  }
  EXPECT_EQ(uri.path(), "/imdsv2_session_token_url");
  *response = http_response(200, aws_imdsv2_session_token);
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

int aws_external_account_creds_httpcli_post_success(
    const grpc_http_request* request, const URI& uri, absl::string_view body,
    Timestamp /*deadline*/, grpc_closure* on_done,
    grpc_http_response* response) {
  if (uri.path() == "/token") {
    validate_aws_external_account_creds_token_exchange_request(request, uri,
                                                               body);
    *response = http_response(
        200, valid_external_account_creds_token_exchange_response);
  }
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

class TokenFetcherCredentialsTest : public ::testing::Test {
 protected:
  class TestTokenFetcherCredentials final : public TokenFetcherCredentials {
   public:
    explicit TestTokenFetcherCredentials(
        std::shared_ptr<grpc_event_engine::experimental::EventEngine>
            event_engine = nullptr)
        : TokenFetcherCredentials(std::move(event_engine),
                                  /*test_only_use_backoff_jitter=*/false) {}

    ~TestTokenFetcherCredentials() override { CHECK_EQ(queue_.size(), 0); }

    void AddResult(absl::StatusOr<RefCountedPtr<Token>> result) {
      MutexLock lock(&mu_);
      queue_.push_front(std::move(result));
    }

    size_t num_fetches() const { return num_fetches_; }

   private:
    class TestFetchRequest final : public FetchRequest {
     public:
      TestFetchRequest(
          grpc_event_engine::experimental::EventEngine& event_engine,
          absl::AnyInvocable<void(absl::StatusOr<RefCountedPtr<Token>>)>
              on_done,
          absl::StatusOr<RefCountedPtr<Token>> result) {
        event_engine.Run([on_done = std::move(on_done),
                          result = std::move(result)]() mutable {
          ExecCtx exec_ctx;
          std::exchange(on_done, nullptr)(std::move(result));
        });
      }

      void Orphan() override { Unref(); }
    };

    OrphanablePtr<FetchRequest> FetchToken(
        Timestamp deadline,
        absl::AnyInvocable<void(absl::StatusOr<RefCountedPtr<Token>>)> on_done)
        override {
      absl::StatusOr<RefCountedPtr<Token>> result;
      {
        MutexLock lock(&mu_);
        CHECK(!queue_.empty());
        result = std::move(queue_.back());
        queue_.pop_back();
      }
      num_fetches_.fetch_add(1);
      return MakeOrphanable<TestFetchRequest>(
          event_engine(), std::move(on_done), std::move(result));
    }

    std::string debug_string() override {
      return "TestTokenFetcherCredentials";
    }

    UniqueTypeName type() const override {
      static UniqueTypeName::Factory kFactory("TestTokenFetcherCredentials");
      return kFactory.Create();
    }

    Mutex mu_;
    std::deque<absl::StatusOr<RefCountedPtr<Token>>> queue_
        ABSL_GUARDED_BY(&mu_);

    std::atomic<size_t> num_fetches_{0};
  };

  void SetUp() override {
    event_engine_ = std::make_shared<FuzzingEventEngine>(
        FuzzingEventEngine::Options(), fuzzing_event_engine::Actions());
    grpc_timer_manager_set_start_threaded(false);
    grpc_init();
    creds_ = MakeRefCounted<TestTokenFetcherCredentials>(event_engine_);
  }

  void TearDown() override {
    event_engine_->FuzzingDone();
    event_engine_->TickUntilIdle();
    event_engine_->UnsetGlobalHooks();
    creds_.reset();
    WaitForSingleOwner(std::move(event_engine_));
    grpc_shutdown_blocking();
  }

  static RefCountedPtr<TokenFetcherCredentials::Token> MakeToken(
      absl::string_view token, Timestamp expiration = Timestamp::InfFuture()) {
    return MakeRefCounted<TokenFetcherCredentials::Token>(
        Slice::FromCopiedString(token), expiration);
  }

  std::shared_ptr<FuzzingEventEngine> event_engine_;
  RefCountedPtr<TestTokenFetcherCredentials> creds_;
};

TEST_F(TokenFetcherCredentialsTest, Basic) {
  const auto kExpirationTime = Timestamp::Now() + Duration::Hours(1);
  ExecCtx exec_ctx;
  creds_->AddResult(MakeToken("foo", kExpirationTime));
  // First request will trigger a fetch.
  LOG(INFO) << "First request";
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: foo", /*expect_delay=*/true);
  state->RunRequestMetadataTest(creds_.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  EXPECT_EQ(creds_->num_fetches(), 1);
  // Second request while fetch is still outstanding will be delayed but
  // will not trigger a new fetch.
  LOG(INFO) << "Second request";
  state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: foo", /*expect_delay=*/true);
  state->RunRequestMetadataTest(creds_.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  EXPECT_EQ(creds_->num_fetches(), 1);
  // Now tick to finish the fetch.
  event_engine_->TickUntilIdle();
  // Next request will be served from cache with no delay.
  LOG(INFO) << "Third request";
  state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: foo", /*expect_delay=*/false);
  state->RunRequestMetadataTest(creds_.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  EXPECT_EQ(creds_->num_fetches(), 1);
  // Advance time to expiration minus expiration adjustment and prefetch time.
  exec_ctx.TestOnlySetNow(kExpirationTime - Duration::Seconds(90));
  // No new fetch yet.
  EXPECT_EQ(creds_->num_fetches(), 1);
  // Next request will trigger a new fetch but will still use the
  // cached token.
  creds_->AddResult(MakeToken("bar"));
  LOG(INFO) << "Fourth request";
  state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: foo", /*expect_delay=*/false);
  state->RunRequestMetadataTest(creds_.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  EXPECT_EQ(creds_->num_fetches(), 2);
  event_engine_->TickUntilIdle();
  // Next request will use the new data.
  LOG(INFO) << "Fifth request";
  state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: bar", /*expect_delay=*/false);
  state->RunRequestMetadataTest(creds_.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  EXPECT_EQ(creds_->num_fetches(), 2);
}

TEST_F(TokenFetcherCredentialsTest, Expires30SecondsEarly) {
  const auto kExpirationTime = Timestamp::Now() + Duration::Hours(1);
  ExecCtx exec_ctx;
  creds_->AddResult(MakeToken("foo", kExpirationTime));
  // First request will trigger a fetch.
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: foo", /*expect_delay=*/true);
  state->RunRequestMetadataTest(creds_.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  EXPECT_EQ(creds_->num_fetches(), 1);
  event_engine_->TickUntilIdle();
  // Advance time to expiration minus 30 seconds.
  exec_ctx.TestOnlySetNow(kExpirationTime - Duration::Seconds(30));
  // No new fetch yet.
  EXPECT_EQ(creds_->num_fetches(), 1);
  // Next request will trigger a new fetch and will delay the call until
  // the fetch completes.
  creds_->AddResult(MakeToken("bar"));
  state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: bar", /*expect_delay=*/true);
  state->RunRequestMetadataTest(creds_.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  EXPECT_EQ(creds_->num_fetches(), 2);
  event_engine_->TickUntilIdle();
}

TEST_F(TokenFetcherCredentialsTest, FetchFails) {
  const absl::Status kExpectedError = absl::UnavailableError("bummer, dude");
  std::optional<FuzzingEventEngine::Duration> run_after_duration;
  event_engine_->SetRunAfterDurationCallback(
      [&](FuzzingEventEngine::Duration duration) {
        run_after_duration = duration;
      });
  ExecCtx exec_ctx;
  // First request will trigger a fetch, which will fail.
  LOG(INFO) << "Sending first RPC.";
  creds_->AddResult(kExpectedError);
  auto state = RequestMetadataState::NewInstance(kExpectedError, "",
                                                 /*expect_delay=*/true);
  state->RunRequestMetadataTest(creds_.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  EXPECT_EQ(creds_->num_fetches(), 1);
  while (!run_after_duration.has_value()) event_engine_->Tick();
  // Make sure backoff was set for the right period.
  EXPECT_EQ(run_after_duration, std::chrono::seconds(1));
  run_after_duration.reset();
  // Start a new call now, which will fail because we're in backoff.
  LOG(INFO) << "Sending second RPC.";
  state = RequestMetadataState::NewInstance(
      kExpectedError, "authorization: foo", /*expect_delay=*/false);
  state->RunRequestMetadataTest(creds_.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  EXPECT_EQ(creds_->num_fetches(), 1);
  // Tick until backoff expires.
  LOG(INFO) << "Waiting for backoff.";
  event_engine_->TickUntilIdle();
  EXPECT_EQ(creds_->num_fetches(), 1);
  // Starting another call should trigger a new fetch, which will
  // succeed this time.
  LOG(INFO) << "Sending third RPC.";
  creds_->AddResult(MakeToken("foo"));
  state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: foo", /*expect_delay=*/true);
  state->RunRequestMetadataTest(creds_.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  EXPECT_EQ(creds_->num_fetches(), 2);
}

TEST_F(TokenFetcherCredentialsTest, Backoff) {
  const absl::Status kExpectedError = absl::UnavailableError("bummer, dude");
  std::optional<FuzzingEventEngine::Duration> run_after_duration;
  event_engine_->SetRunAfterDurationCallback(
      [&](FuzzingEventEngine::Duration duration) {
        run_after_duration = duration;
      });
  ExecCtx exec_ctx;
  // First request will trigger a fetch, which will fail.
  LOG(INFO) << "Sending first RPC.";
  creds_->AddResult(kExpectedError);
  auto state = RequestMetadataState::NewInstance(kExpectedError, "",
                                                 /*expect_delay=*/true);
  state->RunRequestMetadataTest(creds_.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  EXPECT_EQ(creds_->num_fetches(), 1);
  while (!run_after_duration.has_value()) event_engine_->Tick();
  // Make sure backoff was set for the right period.
  EXPECT_EQ(run_after_duration, std::chrono::seconds(1));
  run_after_duration.reset();
  // Start a new call now, which will fail because we're in backoff.
  LOG(INFO) << "Sending second RPC.";
  state = RequestMetadataState::NewInstance(kExpectedError, "",
                                            /*expect_delay=*/false);
  state->RunRequestMetadataTest(creds_.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  EXPECT_EQ(creds_->num_fetches(), 1);
  // Tick until backoff expires.
  LOG(INFO) << "Waiting for backoff.";
  event_engine_->TickUntilIdle();
  EXPECT_EQ(creds_->num_fetches(), 1);
  // Starting another call should trigger a new fetch, which will again fail.
  LOG(INFO) << "Sending third RPC.";
  creds_->AddResult(kExpectedError);
  state = RequestMetadataState::NewInstance(kExpectedError, "",
                                            /*expect_delay=*/true);
  state->RunRequestMetadataTest(creds_.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  EXPECT_EQ(creds_->num_fetches(), 2);
  while (!run_after_duration.has_value()) event_engine_->Tick();
  // The backoff time should be longer now.
  EXPECT_EQ(run_after_duration, std::chrono::milliseconds(1600))
      << "actual: " << run_after_duration->count();
  run_after_duration.reset();
  // Start a new call now, which will fail because we're in backoff.
  LOG(INFO) << "Sending fourth RPC.";
  state = RequestMetadataState::NewInstance(kExpectedError, "",
                                            /*expect_delay=*/false);
  state->RunRequestMetadataTest(creds_.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  EXPECT_EQ(creds_->num_fetches(), 2);
  // Tick until backoff expires.
  LOG(INFO) << "Waiting for backoff.";
  event_engine_->TickUntilIdle();
  EXPECT_EQ(creds_->num_fetches(), 2);
  // Starting another call should trigger a new fetch, which will again fail.
  LOG(INFO) << "Sending fifth RPC.";
  creds_->AddResult(kExpectedError);
  state = RequestMetadataState::NewInstance(kExpectedError, "",
                                            /*expect_delay=*/true);
  state->RunRequestMetadataTest(creds_.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  EXPECT_EQ(creds_->num_fetches(), 3);
  while (!run_after_duration.has_value()) event_engine_->Tick();
  // The backoff time should be longer now.
  EXPECT_EQ(run_after_duration, std::chrono::milliseconds(2560))
      << "actual: " << run_after_duration->count();
}

TEST_F(TokenFetcherCredentialsTest, ShutdownWhileBackoffTimerPending) {
  const absl::Status kExpectedError = absl::UnavailableError("bummer, dude");
  std::optional<FuzzingEventEngine::Duration> run_after_duration;
  event_engine_->SetRunAfterDurationCallback(
      [&](FuzzingEventEngine::Duration duration) {
        run_after_duration = duration;
      });
  ExecCtx exec_ctx;
  creds_->AddResult(kExpectedError);
  // First request will trigger a fetch, which will fail.
  auto state = RequestMetadataState::NewInstance(kExpectedError, "",
                                                 /*expect_delay=*/true);
  state->RunRequestMetadataTest(creds_.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  EXPECT_EQ(creds_->num_fetches(), 1);
  while (!run_after_duration.has_value()) event_engine_->Tick();
  // Make sure backoff was set for the right period.
  EXPECT_EQ(run_after_duration, std::chrono::seconds(1));
  run_after_duration.reset();
  // Do nothing else.  Make sure the creds shut down correctly.
}

// The subclass of ExternalAccountCredentials for testing.
// ExternalAccountCredentials is an abstract class so we can't directly test
// against it.
class TestExternalAccountCredentials final : public ExternalAccountCredentials {
 public:
  TestExternalAccountCredentials(
      Options options, std::vector<std::string> scopes,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine = nullptr)
      : ExternalAccountCredentials(std::move(options), std::move(scopes),
                                   std::move(event_engine)) {}

  std::string GetMetricsValue() { return MetricsHeaderValue(); }

  std::string debug_string() override {
    return "TestExternalAccountCredentials";
  }

  UniqueTypeName type() const override {
    static UniqueTypeName::Factory kFactory("TestExternalAccountCredentials");
    return kFactory.Create();
  }

 private:
  OrphanablePtr<FetchBody> RetrieveSubjectToken(
      Timestamp /*deadline*/,
      absl::AnyInvocable<void(absl::StatusOr<std::string>)> on_done) override {
    return MakeOrphanable<NoOpFetchBody>(event_engine(), std::move(on_done),
                                         "test_subject_token");
  }
};

TEST_F(CredentialsTest, TestExternalAccountCredsMetricsHeader) {
  Json credential_source = Json::FromString("");
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  TestExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  TestExternalAccountCredentials creds(options, {});
  EXPECT_EQ(
      creds.GetMetricsValue(),
      absl::StrFormat("gl-cpp/unknown auth/%s google-byoid-sdk source/unknown "
                      "sa-impersonation/false config-lifetime/false",
                      grpc_version_string()));
}

TEST_F(CredentialsTest,
       TestExternalAccountCredsMetricsHeaderWithServiceAccountImpersonation) {
  Json credential_source = Json::FromString("");
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  TestExternalAccountCredentials::Options options = {
      "external_account",    // type;
      "audience",            // audience;
      "subject_token_type",  // subject_token_type;
      "https://foo.com:5555/service_account_impersonation",  // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  TestExternalAccountCredentials creds(options, {});
  EXPECT_EQ(
      creds.GetMetricsValue(),
      absl::StrFormat("gl-cpp/unknown auth/%s google-byoid-sdk source/unknown "
                      "sa-impersonation/true config-lifetime/false",
                      grpc_version_string()));
}

TEST_F(CredentialsTest,
       TestExternalAccountCredsMetricsHeaderWithConfigLifetime) {
  Json credential_source = Json::FromString("");
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 5000;
  TestExternalAccountCredentials::Options options = {
      "external_account",    // type;
      "audience",            // audience;
      "subject_token_type",  // subject_token_type;
      "https://foo.com:5555/service_account_impersonation",  // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  TestExternalAccountCredentials creds(options, {});
  EXPECT_EQ(
      creds.GetMetricsValue(),
      absl::StrFormat("gl-cpp/unknown auth/%s google-byoid-sdk source/unknown "
                      "sa-impersonation/true config-lifetime/true",
                      grpc_version_string()));
}

class ExternalAccountCredentialsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grpc_timer_manager_set_start_threaded(false);
    grpc_init();
  }

  void TearDown() override {
    event_engine_->FuzzingDone();
    event_engine_->TickUntilIdle();
    event_engine_->UnsetGlobalHooks();
    WaitForSingleOwner(std::move(event_engine_));
    grpc_shutdown_blocking();
  }

  std::shared_ptr<FuzzingEventEngine> event_engine_ =
      std::make_shared<FuzzingEventEngine>(FuzzingEventEngine::Options(),
                                           fuzzing_event_engine::Actions());
};

TEST_F(ExternalAccountCredentialsTest, Success) {
  ExecCtx exec_ctx;
  Json credential_source = Json::FromString("");
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  TestExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds = MakeRefCounted<TestExternalAccountCredentials>(
      options, std::vector<std::string>(), event_engine_);
  // Check security level.
  EXPECT_EQ(creds->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  // First request: http post should be called.
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: Bearer token_exchange_access_token");
  HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                           external_account_creds_httpcli_post_success,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  // Second request: the cached token should be served directly.
  state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: Bearer token_exchange_access_token");
  HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                           httpcli_post_should_not_be_called,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

TEST_F(ExternalAccountCredentialsTest, SuccessWithUrlEncode) {
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer token_exchange_access_token"}};
  ExecCtx exec_ctx;
  Json credential_source = Json::FromString("");
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  TestExternalAccountCredentials::Options options = {
      "external_account",             // type;
      "audience_!@#$",                // audience;
      "subject_token_type_!@#$",      // subject_token_type;
      "",                             // service_account_impersonation_url;
      service_account_impersonation,  // service_account_impersonation;
      "https://foo.com:5555/token_url_encode",  // token_url;
      "https://foo.com:5555/token_info",        // token_info_url;
      credential_source,                        // credential_source;
      "quota_project_id",                       // quota_project_id;
      "client_id",                              // client_id;
      "client_secret",                          // client_secret;
      "",                                       // workforce_pool_user_project;
  };
  auto creds = MakeRefCounted<TestExternalAccountCredentials>(
      options, std::vector<std::string>(), event_engine_);
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: Bearer token_exchange_access_token");
  HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                           external_account_creds_httpcli_post_success,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

TEST_F(ExternalAccountCredentialsTest, SuccessWithServiceAccountImpersonation) {
  ExecCtx exec_ctx;
  Json credential_source = Json::FromString("");
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  TestExternalAccountCredentials::Options options = {
      "external_account",    // type;
      "audience",            // audience;
      "subject_token_type",  // subject_token_type;
      "https://foo.com:5555/service_account_impersonation",  // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds = MakeRefCounted<TestExternalAccountCredentials>(
      options, std::vector<std::string>{"scope_1", "scope_2"}, event_engine_);
  // Check security level.
  EXPECT_EQ(creds->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  // First request: http put should be called.
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(),
      "authorization: Bearer service_account_impersonation_access_token");
  HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                           external_account_creds_httpcli_post_success,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

TEST_F(ExternalAccountCredentialsTest,
       SuccessWithServiceAccountImpersonationAndCustomTokenLifetime) {
  ExecCtx exec_ctx;
  Json credential_source = Json::FromString("");
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 1800;
  TestExternalAccountCredentials::Options options = {
      "external_account",    // type;
      "audience",            // audience;
      "subject_token_type",  // subject_token_type;
      "https://foo.com:5555/service_account_impersonation",  // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds = MakeRefCounted<TestExternalAccountCredentials>(
      options, std::vector<std::string>{"scope_1", "scope_2"}, event_engine_);
  // Check security level.
  EXPECT_EQ(creds->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  // First request: http put should be called.
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(),
      "authorization: Bearer service_account_impersonation_access_token");
  HttpRequest::SetOverride(
      httpcli_get_should_not_be_called,
      external_acc_creds_serv_acc_imp_custom_lifetime_httpcli_post_success,
      httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

TEST_F(ExternalAccountCredentialsTest,
       FailureWithServiceAccountImpersonationAndInvalidCustomTokenLifetime) {
  const char* options_string1 =
      "{\"type\":\"external_account\",\"audience\":\"audience\","
      "\"subject_token_type\":\"subject_token_type\","
      "\"service_account_impersonation_url\":\"service_account_impersonation_"
      "url\",\"service_account_impersonation\":"
      "{\"token_lifetime_seconds\":599},"
      "\"token_url\":\"https://foo.com:5555/token\","
      "\"token_info_url\":\"https://foo.com:5555/token_info\","
      "\"credential_source\":{\"url\":\"https://foo.com:5555/"
      "generate_subject_token_format_json\","
      "\"headers\":{\"Metadata-Flavor\":\"Google\"},"
      "\"format\":{\"type\":\"json\",\"subject_token_field_name\":\"access_"
      "token\"}},\"quota_project_id\":\"quota_project_id\","
      "\"client_id\":\"client_id\",\"client_secret\":\"client_secret\"}";
  auto json = JsonParse(options_string1);
  ASSERT_TRUE(json.ok()) << json.status();
  auto creds = ExternalAccountCredentials::Create(*json, {"scope1", "scope2"});
  EXPECT_EQ("token_lifetime_seconds must be more than 600s",
            creds.status().message());

  const char* options_string2 =
      "{\"type\":\"external_account\",\"audience\":\"audience\","
      "\"subject_token_type\":\"subject_token_type\","
      "\"service_account_impersonation_url\":\"service_account_impersonation_"
      "url\",\"service_account_impersonation\":"
      "{\"token_lifetime_seconds\":43201},"
      "\"token_url\":\"https://foo.com:5555/token\","
      "\"token_info_url\":\"https://foo.com:5555/token_info\","
      "\"credential_source\":{\"url\":\"https://foo.com:5555/"
      "generate_subject_token_format_json\","
      "\"headers\":{\"Metadata-Flavor\":\"Google\"},"
      "\"format\":{\"type\":\"json\",\"subject_token_field_name\":\"access_"
      "token\"}},\"quota_project_id\":\"quota_project_id\","
      "\"client_id\":\"client_id\",\"client_secret\":\"client_secret\"}";
  json = JsonParse(options_string2);
  ASSERT_TRUE(json.ok()) << json.status();
  creds = ExternalAccountCredentials::Create(*json, {"scope1", "scope2"});
  EXPECT_EQ("token_lifetime_seconds must be less than 43200s",
            creds.status().message());
}

TEST_F(ExternalAccountCredentialsTest, FailureInvalidTokenUrl) {
  ExecCtx exec_ctx;
  Json credential_source = Json::FromString("");
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  TestExternalAccountCredentials::Options options = {
      "external_account",    // type;
      "audience",            // audience;
      "subject_token_type",  // subject_token_type;
      "https://foo.com:5555/service_account_impersonation",  // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "invalid_token_url",                // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds = MakeRefCounted<TestExternalAccountCredentials>(
      options, std::vector<std::string>(), event_engine_);
  HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                           httpcli_post_should_not_be_called,
                           httpcli_put_should_not_be_called);
  // TODO(roth): This should return UNAUTHENTICATED.
  grpc_error_handle expected_error = absl::UnknownError(
      "error fetching oauth2 token: Invalid token url: "
      "invalid_token_url. Error: INVALID_ARGUMENT: Could not parse "
      "'scheme' from uri 'invalid_token_url'. Scheme not found.");
  auto state = RequestMetadataState::NewInstance(expected_error, {});
  state->RunRequestMetadataTest(creds.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

TEST_F(ExternalAccountCredentialsTest,
       FailureInvalidServiceAccountImpersonationUrl) {
  ExecCtx exec_ctx;
  Json credential_source = Json::FromString("");
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  TestExternalAccountCredentials::Options options = {
      "external_account",                           // type;
      "audience",                                   // audience;
      "subject_token_type",                         // subject_token_type;
      "invalid_service_account_impersonation_url",  // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds = MakeRefCounted<TestExternalAccountCredentials>(
      options, std::vector<std::string>(), event_engine_);
  HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                           external_account_creds_httpcli_post_success,
                           httpcli_put_should_not_be_called);
  // TODO(roth): This should return UNAUTHENTICATED.
  grpc_error_handle expected_error = absl::UnknownError(
      "error fetching oauth2 token: Invalid service account impersonation url: "
      "invalid_service_account_impersonation_url. Error: INVALID_ARGUMENT: "
      "Could not parse 'scheme' from uri "
      "'invalid_service_account_impersonation_url'. Scheme not found.");
  auto state = RequestMetadataState::NewInstance(expected_error, {});
  state->RunRequestMetadataTest(creds.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

TEST_F(ExternalAccountCredentialsTest,
       FailureTokenExchangeResponseMissingAccessToken) {
  ExecCtx exec_ctx;
  Json credential_source = Json::FromString("");
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  TestExternalAccountCredentials::Options options = {
      "external_account",    // type;
      "audience",            // audience;
      "subject_token_type",  // subject_token_type;
      "https://foo.com:5555/service_account_impersonation",  // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds = MakeRefCounted<TestExternalAccountCredentials>(
      options, std::vector<std::string>(), event_engine_);
  HttpRequest::SetOverride(
      httpcli_get_should_not_be_called,
      external_account_creds_httpcli_post_failure_token_exchange_response_missing_access_token,
      httpcli_put_should_not_be_called);
  // TODO(roth): This should return UNAUTHENTICATED.
  grpc_error_handle expected_error = absl::UnknownError(
      "error fetching oauth2 token: Missing or invalid access_token in "
      "{\"not_access_token\":\"not_access_token\",\"expires_in\":3599, "
      "\"token_type\":\"Bearer\"}.");
  auto state = RequestMetadataState::NewInstance(expected_error, {});
  state->RunRequestMetadataTest(creds.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

TEST_F(ExternalAccountCredentialsTest,
       UrlExternalAccountCredsSuccessFormatText) {
  ExecCtx exec_ctx;
  auto credential_source = JsonParse(
      valid_url_external_account_creds_options_credential_source_format_text);
  ASSERT_TRUE(credential_source.ok()) << credential_source.status();
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      *credential_source,                 // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      UrlExternalAccountCredentials::Create(options, {}, event_engine_);
  ASSERT_TRUE(creds.ok()) << creds.status();
  ASSERT_NE(*creds, nullptr);
  EXPECT_EQ((*creds)->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: Bearer token_exchange_access_token");
  HttpRequest::SetOverride(url_external_account_creds_httpcli_get_success,
                           external_account_creds_httpcli_post_success,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds->get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

TEST_F(ExternalAccountCredentialsTest,
       UrlExternalAccountCredsSuccessWithQueryParamsFormatText) {
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer token_exchange_access_token"}};
  ExecCtx exec_ctx;
  auto credential_source = JsonParse(
      valid_url_external_account_creds_options_credential_source_with_query_params_format_text);
  ASSERT_TRUE(credential_source.ok()) << credential_source.status();
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      *credential_source,                 // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      UrlExternalAccountCredentials::Create(options, {}, event_engine_);
  ASSERT_TRUE(creds.ok()) << creds.status();
  ASSERT_NE(*creds, nullptr);
  EXPECT_EQ((*creds)->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: Bearer token_exchange_access_token");
  HttpRequest::SetOverride(url_external_account_creds_httpcli_get_success,
                           external_account_creds_httpcli_post_success,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds->get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

TEST_F(ExternalAccountCredentialsTest,
       UrlExternalAccountCredsSuccessFormatJson) {
  ExecCtx exec_ctx;
  auto credential_source = JsonParse(
      valid_url_external_account_creds_options_credential_source_format_json);
  ASSERT_TRUE(credential_source.ok()) << credential_source.status();
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      *credential_source,                 // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      UrlExternalAccountCredentials::Create(options, {}, event_engine_);
  ASSERT_TRUE(creds.ok()) << creds.status();
  ASSERT_NE(*creds, nullptr);
  EXPECT_EQ((*creds)->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: Bearer token_exchange_access_token");
  HttpRequest::SetOverride(url_external_account_creds_httpcli_get_success,
                           external_account_creds_httpcli_post_success,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds->get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

TEST_F(ExternalAccountCredentialsTest,
       UrlExternalAccountCredsFailureInvalidCredentialSourceUrl) {
  auto credential_source =
      JsonParse(invalid_url_external_account_creds_options_credential_source);
  ASSERT_TRUE(credential_source.ok()) << credential_source.status();
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      *credential_source,                 // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds = UrlExternalAccountCredentials::Create(options, {});
  ASSERT_FALSE(creds.ok());
  EXPECT_THAT(creds.status().message(),
              ::testing::StartsWith("Invalid credential source url."));
}

TEST_F(ExternalAccountCredentialsTest,
       FileExternalAccountCredsSuccessFormatText) {
  ExecCtx exec_ctx;
  char* subject_token_path = write_tmp_jwt_file("test_subject_token");
  auto credential_source = JsonParse(absl::StrFormat(
      "{\"file\":\"%s\"}",
      absl::StrReplaceAll(subject_token_path, {{"\\", "\\\\"}})));
  ASSERT_TRUE(credential_source.ok()) << credential_source.status();
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      *credential_source,                 // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      FileExternalAccountCredentials::Create(options, {}, event_engine_);
  ASSERT_TRUE(creds.ok()) << creds.status();
  ASSERT_NE(*creds, nullptr);
  EXPECT_EQ((*creds)->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: Bearer token_exchange_access_token");
  HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                           external_account_creds_httpcli_post_success,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds->get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  gpr_free(subject_token_path);
}

TEST_F(ExternalAccountCredentialsTest,
       FileExternalAccountCredsSuccessFormatJson) {
  ExecCtx exec_ctx;
  char* subject_token_path =
      write_tmp_jwt_file("{\"access_token\":\"test_subject_token\"}");
  auto credential_source = JsonParse(absl::StrFormat(
      "{\n"
      "\"file\":\"%s\",\n"
      "\"format\":\n"
      "{\n"
      "\"type\":\"json\",\n"
      "\"subject_token_field_name\":\"access_token\"\n"
      "}\n"
      "}",
      absl::StrReplaceAll(subject_token_path, {{"\\", "\\\\"}})));
  ASSERT_TRUE(credential_source.ok()) << credential_source.status();
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      *credential_source,                 // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      FileExternalAccountCredentials::Create(options, {}, event_engine_);
  ASSERT_TRUE(creds.ok()) << creds.status();
  ASSERT_NE(*creds, nullptr);
  EXPECT_EQ((*creds)->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: Bearer token_exchange_access_token");
  HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                           external_account_creds_httpcli_post_success,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds->get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  gpr_free(subject_token_path);
}

TEST_F(ExternalAccountCredentialsTest,
       FileExternalAccountCredsFailureFileNotFound) {
  ExecCtx exec_ctx;
  auto credential_source = JsonParse("{\"file\":\"non_exisiting_file\"}");
  ASSERT_TRUE(credential_source.ok()) << credential_source.status();
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      *credential_source,                 // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      FileExternalAccountCredentials::Create(options, {}, event_engine_);
  ASSERT_TRUE(creds.ok()) << creds.status();
  ASSERT_NE(*creds, nullptr);
  HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                           httpcli_post_should_not_be_called,
                           httpcli_put_should_not_be_called);
  // TODO(roth): This should return UNAVAILABLE.
  grpc_error_handle expected_error = absl::InternalError(
      "error fetching oauth2 token: Failed to load file: "
      "non_exisiting_file due to error(fdopen): No such file or directory");
  auto state = RequestMetadataState::NewInstance(expected_error, {});
  state->RunRequestMetadataTest(creds->get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

TEST_F(ExternalAccountCredentialsTest,
       FileExternalAccountCredsFailureInvalidJsonContent) {
  ExecCtx exec_ctx;
  char* subject_token_path = write_tmp_jwt_file("not_a_valid_json_file");
  auto credential_source = JsonParse(absl::StrFormat(
      "{\n"
      "\"file\":\"%s\",\n"
      "\"format\":\n"
      "{\n"
      "\"type\":\"json\",\n"
      "\"subject_token_field_name\":\"access_token\"\n"
      "}\n"
      "}",
      absl::StrReplaceAll(subject_token_path, {{"\\", "\\\\"}})));
  ASSERT_TRUE(credential_source.ok()) << credential_source.status();
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      *credential_source,                 // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      FileExternalAccountCredentials::Create(options, {}, event_engine_);
  ASSERT_TRUE(creds.ok()) << creds.status();
  ASSERT_NE(*creds, nullptr);
  HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                           httpcli_post_should_not_be_called,
                           httpcli_put_should_not_be_called);
  // TODO(roth): This should return UNAUTHENTICATED.
  grpc_error_handle expected_error = absl::UnknownError(
      "error fetching oauth2 token: The content of the file is not a "
      "valid json object.");
  auto state = RequestMetadataState::NewInstance(expected_error, {});
  state->RunRequestMetadataTest(creds->get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  gpr_free(subject_token_path);
}

TEST_F(ExternalAccountCredentialsTest, AwsExternalAccountCredsSuccess) {
  ExecCtx exec_ctx;
  auto credential_source =
      JsonParse(valid_aws_external_account_creds_options_credential_source);
  ASSERT_TRUE(credential_source.ok()) << credential_source.status();
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      *credential_source,                 // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      AwsExternalAccountCredentials::Create(options, {}, event_engine_);
  ASSERT_TRUE(creds.ok()) << creds.status();
  ASSERT_NE(*creds, nullptr);
  EXPECT_EQ((*creds)->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: Bearer token_exchange_access_token");
  HttpRequest::SetOverride(aws_external_account_creds_httpcli_get_success,
                           aws_external_account_creds_httpcli_post_success,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds->get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

TEST_F(ExternalAccountCredentialsTest, AwsImdsv2ExternalAccountCredsSuccess) {
  ExecCtx exec_ctx;
  auto credential_source = JsonParse(
      valid_aws_imdsv2_external_account_creds_options_credential_source);
  ASSERT_TRUE(credential_source.ok()) << credential_source.status();
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      *credential_source,                 // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      AwsExternalAccountCredentials::Create(options, {}, event_engine_);
  ASSERT_TRUE(creds.ok()) << creds.status();
  ASSERT_NE(*creds, nullptr);
  EXPECT_EQ((*creds)->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: Bearer token_exchange_access_token");
  HttpRequest::SetOverride(
      aws_imdsv2_external_account_creds_httpcli_get_success,
      aws_external_account_creds_httpcli_post_success,
      aws_imdsv2_external_account_creds_httpcli_put_success);
  state->RunRequestMetadataTest(creds->get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

TEST_F(ExternalAccountCredentialsTest,
       AwsImdsv2ExternalAccountCredShouldNotUseMetadataServer) {
  ExecCtx exec_ctx;
  SetEnv("AWS_REGION", "test_regionz");
  SetEnv("AWS_ACCESS_KEY_ID", "test_access_key_id");
  SetEnv("AWS_SECRET_ACCESS_KEY", "test_secret_access_key");
  SetEnv("AWS_SESSION_TOKEN", "test_token");
  auto credential_source = JsonParse(
      valid_aws_imdsv2_external_account_creds_options_credential_source);
  ASSERT_TRUE(credential_source.ok()) << credential_source.status();
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      *credential_source,                 // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      AwsExternalAccountCredentials::Create(options, {}, event_engine_);
  ASSERT_TRUE(creds.ok()) << creds.status();
  ASSERT_NE(*creds, nullptr);
  EXPECT_EQ((*creds)->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: Bearer token_exchange_access_token");
  HttpRequest::SetOverride(aws_external_account_creds_httpcli_get_success,
                           aws_external_account_creds_httpcli_post_success,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds->get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  UnsetEnv("AWS_REGION");
  UnsetEnv("AWS_ACCESS_KEY_ID");
  UnsetEnv("AWS_SECRET_ACCESS_KEY");
  UnsetEnv("AWS_SESSION_TOKEN");
}

TEST_F(
    ExternalAccountCredentialsTest,
    AwsImdsv2ExternalAccountCredShouldNotUseMetadataServerOptionalTokenMissing) {
  ExecCtx exec_ctx;
  SetEnv("AWS_REGION", "test_regionz");
  SetEnv("AWS_ACCESS_KEY_ID", "test_access_key_id");
  SetEnv("AWS_SECRET_ACCESS_KEY", "test_secret_access_key");
  auto credential_source = JsonParse(
      valid_aws_imdsv2_external_account_creds_options_credential_source);
  ASSERT_TRUE(credential_source.ok()) << credential_source.status();
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      *credential_source,                 // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      AwsExternalAccountCredentials::Create(options, {}, event_engine_);
  ASSERT_TRUE(creds.ok()) << creds.status();
  ASSERT_NE(*creds, nullptr);
  EXPECT_EQ((*creds)->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: Bearer token_exchange_access_token");
  HttpRequest::SetOverride(aws_external_account_creds_httpcli_get_success,
                           aws_external_account_creds_httpcli_post_success,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds->get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  UnsetEnv("AWS_REGION");
  UnsetEnv("AWS_ACCESS_KEY_ID");
  UnsetEnv("AWS_SECRET_ACCESS_KEY");
}

TEST_F(ExternalAccountCredentialsTest, AwsExternalAccountCredsSuccessIpv6) {
  ExecCtx exec_ctx;
  auto credential_source = JsonParse(
      valid_aws_external_account_creds_options_credential_source_ipv6);
  ASSERT_TRUE(credential_source.ok()) << credential_source.status();
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      *credential_source,                 // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      AwsExternalAccountCredentials::Create(options, {}, event_engine_);
  ASSERT_TRUE(creds.ok()) << creds.status();
  ASSERT_NE(*creds, nullptr);
  EXPECT_EQ((*creds)->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: Bearer token_exchange_access_token");
  HttpRequest::SetOverride(
      aws_imdsv2_external_account_creds_httpcli_get_success,
      aws_external_account_creds_httpcli_post_success,
      aws_imdsv2_external_account_creds_httpcli_put_success);
  state->RunRequestMetadataTest(creds->get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

TEST_F(ExternalAccountCredentialsTest,
       AwsExternalAccountCredsSuccessPathRegionEnvKeysUrl) {
  ExecCtx exec_ctx;
  SetEnv("AWS_REGION", "test_regionz");
  auto credential_source =
      JsonParse(valid_aws_external_account_creds_options_credential_source);
  ASSERT_TRUE(credential_source.ok()) << credential_source.status();
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      *credential_source,                 // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      AwsExternalAccountCredentials::Create(options, {}, event_engine_);
  ASSERT_TRUE(creds.ok()) << creds.status();
  ASSERT_NE(*creds, nullptr);
  EXPECT_EQ((*creds)->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: Bearer token_exchange_access_token");
  HttpRequest::SetOverride(aws_external_account_creds_httpcli_get_success,
                           aws_external_account_creds_httpcli_post_success,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds->get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  UnsetEnv("AWS_REGION");
}

TEST_F(ExternalAccountCredentialsTest,
       AwsExternalAccountCredsSuccessPathDefaultRegionEnvKeysUrl) {
  ExecCtx exec_ctx;
  SetEnv("AWS_DEFAULT_REGION", "test_regionz");
  auto credential_source =
      JsonParse(valid_aws_external_account_creds_options_credential_source);
  ASSERT_TRUE(credential_source.ok()) << credential_source.status();
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      *credential_source,                 // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      AwsExternalAccountCredentials::Create(options, {}, event_engine_);
  ASSERT_TRUE(creds.ok()) << creds.status();
  ASSERT_NE(*creds, nullptr);
  EXPECT_EQ((*creds)->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: Bearer token_exchange_access_token");
  HttpRequest::SetOverride(aws_external_account_creds_httpcli_get_success,
                           aws_external_account_creds_httpcli_post_success,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds->get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  UnsetEnv("AWS_DEFAULT_REGION");
}

TEST_F(ExternalAccountCredentialsTest,
       AwsExternalAccountCredsSuccessPathDuplicateRegionEnvKeysUrl) {
  ExecCtx exec_ctx;
  // Make sure that AWS_REGION gets used over AWS_DEFAULT_REGION
  SetEnv("AWS_REGION", "test_regionz");
  SetEnv("AWS_DEFAULT_REGION", "ERROR_REGION");
  auto credential_source =
      JsonParse(valid_aws_external_account_creds_options_credential_source);
  ASSERT_TRUE(credential_source.ok()) << credential_source.status();
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      *credential_source,                 // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      AwsExternalAccountCredentials::Create(options, {}, event_engine_);
  ASSERT_TRUE(creds.ok()) << creds.status();
  ASSERT_NE(*creds, nullptr);
  EXPECT_EQ((*creds)->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: Bearer token_exchange_access_token");
  HttpRequest::SetOverride(aws_external_account_creds_httpcli_get_success,
                           aws_external_account_creds_httpcli_post_success,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds->get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  UnsetEnv("AWS_REGION");
  UnsetEnv("AWS_DEFAULT_REGION");
}

TEST_F(ExternalAccountCredentialsTest,
       AwsExternalAccountCredsSuccessPathRegionUrlKeysEnv) {
  ExecCtx exec_ctx;
  SetEnv("AWS_ACCESS_KEY_ID", "test_access_key_id");
  SetEnv("AWS_SECRET_ACCESS_KEY", "test_secret_access_key");
  SetEnv("AWS_SESSION_TOKEN", "test_token");
  auto credential_source =
      JsonParse(valid_aws_external_account_creds_options_credential_source);
  ASSERT_TRUE(credential_source.ok()) << credential_source.status();
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      *credential_source,                 // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      AwsExternalAccountCredentials::Create(options, {}, event_engine_);
  ASSERT_TRUE(creds.ok()) << creds.status();
  ASSERT_NE(*creds, nullptr);
  EXPECT_EQ((*creds)->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: Bearer token_exchange_access_token");
  HttpRequest::SetOverride(aws_external_account_creds_httpcli_get_success,
                           aws_external_account_creds_httpcli_post_success,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds->get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  UnsetEnv("AWS_ACCESS_KEY_ID");
  UnsetEnv("AWS_SECRET_ACCESS_KEY");
  UnsetEnv("AWS_SESSION_TOKEN");
}

TEST_F(ExternalAccountCredentialsTest,
       AwsExternalAccountCredsSuccessPathRegionEnvKeysEnv) {
  ExecCtx exec_ctx;
  SetEnv("AWS_REGION", "test_regionz");
  SetEnv("AWS_ACCESS_KEY_ID", "test_access_key_id");
  SetEnv("AWS_SECRET_ACCESS_KEY", "test_secret_access_key");
  SetEnv("AWS_SESSION_TOKEN", "test_token");
  auto credential_source =
      JsonParse(valid_aws_external_account_creds_options_credential_source);
  ASSERT_TRUE(credential_source.ok()) << credential_source.status();
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      *credential_source,                 // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      AwsExternalAccountCredentials::Create(options, {}, event_engine_);
  ASSERT_TRUE(creds.ok()) << creds.status();
  ASSERT_NE(*creds, nullptr);
  EXPECT_EQ((*creds)->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: Bearer token_exchange_access_token");
  HttpRequest::SetOverride(aws_external_account_creds_httpcli_get_success,
                           aws_external_account_creds_httpcli_post_success,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds->get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  UnsetEnv("AWS_REGION");
  UnsetEnv("AWS_ACCESS_KEY_ID");
  UnsetEnv("AWS_SECRET_ACCESS_KEY");
  UnsetEnv("AWS_SESSION_TOKEN");
}

TEST_F(ExternalAccountCredentialsTest,
       AwsExternalAccountCredsSuccessPathDefaultRegionEnvKeysEnv) {
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer token_exchange_access_token"}};
  ExecCtx exec_ctx;
  SetEnv("AWS_DEFAULT_REGION", "test_regionz");
  SetEnv("AWS_ACCESS_KEY_ID", "test_access_key_id");
  SetEnv("AWS_SECRET_ACCESS_KEY", "test_secret_access_key");
  SetEnv("AWS_SESSION_TOKEN", "test_token");
  auto credential_source =
      JsonParse(valid_aws_external_account_creds_options_credential_source);
  ASSERT_TRUE(credential_source.ok()) << credential_source.status();
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      *credential_source,                 // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      AwsExternalAccountCredentials::Create(options, {}, event_engine_);
  ASSERT_TRUE(creds.ok()) << creds.status();
  ASSERT_NE(*creds, nullptr);
  EXPECT_EQ((*creds)->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: Bearer token_exchange_access_token");
  HttpRequest::SetOverride(aws_external_account_creds_httpcli_get_success,
                           aws_external_account_creds_httpcli_post_success,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds->get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  UnsetEnv("AWS_DEFAULT_REGION");
  UnsetEnv("AWS_ACCESS_KEY_ID");
  UnsetEnv("AWS_SECRET_ACCESS_KEY");
  UnsetEnv("AWS_SESSION_TOKEN");
}

TEST_F(ExternalAccountCredentialsTest,
       AwsExternalAccountCredsSuccessPathDuplicateRegionEnvKeysEnv) {
  ExecCtx exec_ctx;
  // Make sure that AWS_REGION gets used over AWS_DEFAULT_REGION
  SetEnv("AWS_REGION", "test_regionz");
  SetEnv("AWS_DEFAULT_REGION", "ERROR_REGION");
  SetEnv("AWS_ACCESS_KEY_ID", "test_access_key_id");
  SetEnv("AWS_SECRET_ACCESS_KEY", "test_secret_access_key");
  SetEnv("AWS_SESSION_TOKEN", "test_token");
  auto credential_source =
      JsonParse(valid_aws_external_account_creds_options_credential_source);
  ASSERT_TRUE(credential_source.ok()) << credential_source.status();
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      *credential_source,                 // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      AwsExternalAccountCredentials::Create(options, {}, event_engine_);
  ASSERT_TRUE(creds.ok()) << creds.status();
  ASSERT_NE(*creds, nullptr);
  EXPECT_EQ((*creds)->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::OkStatus(), "authorization: Bearer token_exchange_access_token");
  HttpRequest::SetOverride(aws_external_account_creds_httpcli_get_success,
                           aws_external_account_creds_httpcli_post_success,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds->get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  UnsetEnv("AWS_REGION");
  UnsetEnv("AWS_DEFAULT_REGION");
  UnsetEnv("AWS_ACCESS_KEY_ID");
  UnsetEnv("AWS_SECRET_ACCESS_KEY");
  UnsetEnv("AWS_SESSION_TOKEN");
}

TEST_F(ExternalAccountCredentialsTest, CreateSuccess) {
  // url credentials
  const char* url_options_string =
      "{\"type\":\"external_account\",\"audience\":\"audience\",\"subject_"
      "token_type\":\"subject_token_type\",\"service_account_impersonation_"
      "url\":\"service_account_impersonation_url\","
      "\"token_url\":\"https://foo.com:5555/"
      "token\",\"token_info_url\":\"https://foo.com:5555/"
      "token_info\",\"credential_source\":{\"url\":\"https://foo.com:5555/"
      "generate_subject_token_format_json\",\"headers\":{\"Metadata-Flavor\":"
      "\"Google\"},\"format\":{\"type\":\"json\",\"subject_token_field_name\":"
      "\"access_token\"}},\"quota_project_id\":\"quota_"
      "project_id\",\"client_id\":\"client_id\",\"client_secret\":\"client_"
      "secret\"}";
  const char* url_scopes_string = "scope1,scope2";
  grpc_call_credentials* url_creds = grpc_external_account_credentials_create(
      url_options_string, url_scopes_string);
  ASSERT_NE(url_creds, nullptr);
  url_creds->Unref();
  // file credentials
  const char* file_options_string =
      "{\"type\":\"external_account\",\"audience\":\"audience\",\"subject_"
      "token_type\":\"subject_token_type\",\"service_account_impersonation_"
      "url\":\"service_account_impersonation_url\","
      "\"token_url\":\"https://foo.com:5555/"
      "token\",\"token_info_url\":\"https://foo.com:5555/"
      "token_info\",\"credential_source\":{\"file\":\"credentials_file_path\"},"
      "\"quota_project_id\":\"quota_"
      "project_id\",\"client_id\":\"client_id\",\"client_secret\":\"client_"
      "secret\"}";
  const char* file_scopes_string = "scope1,scope2";
  grpc_call_credentials* file_creds = grpc_external_account_credentials_create(
      file_options_string, file_scopes_string);
  ASSERT_NE(file_creds, nullptr);
  file_creds->Unref();
  // aws credentials
  const char* aws_options_string =
      "{\"type\":\"external_account\",\"audience\":\"audience\",\"subject_"
      "token_type\":\"subject_token_type\",\"service_account_impersonation_"
      "url\":\"service_account_impersonation_url\","
      "\"token_url\":\"https://"
      "foo.com:5555/token\",\"token_info_url\":\"https://foo.com:5555/"
      "token_info\",\"credential_source\":{\"environment_id\":\"aws1\","
      "\"region_url\":\"https://169.254.169.254:5555/"
      "region_url\",\"url\":\"https://"
      "169.254.169.254:5555/url\",\"regional_cred_verification_url\":\"https://"
      "foo.com:5555/regional_cred_verification_url_{region}\"},"
      "\"quota_project_id\":\"quota_"
      "project_id\",\"client_id\":\"client_id\",\"client_secret\":\"client_"
      "secret\"}";
  const char* aws_scopes_string = "scope1,scope2";
  grpc_call_credentials* aws_creds = grpc_external_account_credentials_create(
      aws_options_string, aws_scopes_string);
  ASSERT_NE(aws_creds, nullptr);
  aws_creds->Unref();
}

TEST_F(ExternalAccountCredentialsTest,
       AwsExternalAccountCredsFailureUnmatchedEnvironmentId) {
  auto credential_source = JsonParse(
      invalid_aws_external_account_creds_options_credential_source_unmatched_environment_id);
  ASSERT_TRUE(credential_source.ok()) << credential_source.status();
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      *credential_source,                 // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      AwsExternalAccountCredentials::Create(options, {}, event_engine_);
  ASSERT_FALSE(creds.ok());
  EXPECT_EQ("environment_id does not match.", creds.status().message());
}

TEST_F(ExternalAccountCredentialsTest,
       AwsExternalAccountCredsFailureInvalidRegionalCredVerificationUrl) {
  ExecCtx exec_ctx;
  auto credential_source = JsonParse(
      invalid_aws_external_account_creds_options_credential_source_invalid_regional_cred_verification_url);
  ASSERT_TRUE(credential_source.ok()) << credential_source.status();
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      *credential_source,                 // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      AwsExternalAccountCredentials::Create(options, {}, event_engine_);
  ASSERT_TRUE(creds.ok()) << creds.status();
  ASSERT_NE(*creds, nullptr);
  EXPECT_EQ((*creds)->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  // TODO(roth): This should return UNAUTHENTICATED.
  grpc_error_handle expected_error = absl::UnknownError(
      "error fetching oauth2 token: Creating aws request signer failed.");
  auto state = RequestMetadataState::NewInstance(expected_error, {});
  HttpRequest::SetOverride(aws_external_account_creds_httpcli_get_success,
                           aws_external_account_creds_httpcli_post_success,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds->get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

TEST_F(ExternalAccountCredentialsTest,
       AwsExternalAccountCredsFailureMissingRoleName) {
  ExecCtx exec_ctx;
  auto credential_source = JsonParse(
      invalid_aws_external_account_creds_options_credential_source_missing_role_name);
  ASSERT_TRUE(credential_source.ok()) << credential_source.status();
  TestExternalAccountCredentials::ServiceAccountImpersonation
      service_account_impersonation;
  service_account_impersonation.token_lifetime_seconds = 3600;
  ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      service_account_impersonation,      // service_account_impersonation;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      *credential_source,                 // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      AwsExternalAccountCredentials::Create(options, {}, event_engine_);
  ASSERT_TRUE(creds.ok()) << creds.status();
  ASSERT_NE(*creds, nullptr);
  EXPECT_EQ((*creds)->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  // TODO(roth): This should return UNAUTHENTICATED.
  grpc_error_handle expected_error = absl::UnknownError(
      "error fetching oauth2 token: "
      "Missing role name when retrieving signing keys.");
  auto state = RequestMetadataState::NewInstance(expected_error, {});
  HttpRequest::SetOverride(aws_external_account_creds_httpcli_get_success,
                           aws_external_account_creds_httpcli_post_success,
                           httpcli_put_should_not_be_called);
  state->RunRequestMetadataTest(creds->get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
  event_engine_->TickUntilIdle();
  HttpRequest::SetOverride(nullptr, nullptr, nullptr);
}

TEST_F(ExternalAccountCredentialsTest, CreateFailureInvalidJsonFormat) {
  const char* options_string = "invalid_json";
  grpc_call_credentials* creds =
      grpc_external_account_credentials_create(options_string, "");
  EXPECT_EQ(creds, nullptr);
}

TEST_F(ExternalAccountCredentialsTest, CreateFailureInvalidOptionsFormat) {
  const char* options_string = "{\"random_key\":\"random_value\"}";
  grpc_call_credentials* creds =
      grpc_external_account_credentials_create(options_string, "");
  EXPECT_EQ(creds, nullptr);
}

TEST_F(ExternalAccountCredentialsTest,
       CreateFailureInvalidOptionsCredentialSource) {
  const char* options_string =
      "{\"type\":\"external_account\",\"audience\":\"audience\",\"subject_"
      "token_type\":\"subject_token_type\",\"service_account_impersonation_"
      "url\":\"service_account_impersonation_url\","
      "\"token_url\":\"https://foo.com:5555/"
      "token\",\"token_info_url\":\"https://foo.com:5555/"
      "token_info\",\"credential_source\":{\"random_key\":\"random_value\"},"
      "\"quota_project_id\":\"quota_"
      "project_id\",\"client_id\":\"client_id\",\"client_secret\":\"client_"
      "secret\"}";
  grpc_call_credentials* creds =
      grpc_external_account_credentials_create(options_string, "");
  EXPECT_EQ(creds, nullptr);
}

TEST_F(ExternalAccountCredentialsTest, CreateSuccessWorkforcePool) {
  const char* url_options_string =
      "{\"type\":\"external_account\",\"audience\":\"//iam.googleapis.com/"
      "locations/location/workforcePools/pool/providers/provider\",\"subject_"
      "token_type\":\"subject_token_type\",\"service_account_impersonation_"
      "url\":\"service_account_impersonation_url\","
      "\"token_url\":\"https://foo.com:5555/"
      "token\",\"token_info_url\":\"https://foo.com:5555/"
      "token_info\",\"credential_source\":{\"url\":\"https://foo.com:5555/"
      "generate_subject_token_format_json\",\"headers\":{\"Metadata-Flavor\":"
      "\"Google\"},\"format\":{\"type\":\"json\",\"subject_token_field_name\":"
      "\"access_token\"}},\"quota_project_id\":\"quota_"
      "project_id\",\"client_id\":\"client_id\",\"client_secret\":\"client_"
      "secret\",\"workforce_pool_user_project\":\"workforce_pool_user_"
      "project\"}";
  const char* url_scopes_string = "scope1,scope2";
  grpc_call_credentials* url_creds = grpc_external_account_credentials_create(
      url_options_string, url_scopes_string);
  ASSERT_NE(url_creds, nullptr);
  url_creds->Unref();
}

TEST_F(ExternalAccountCredentialsTest,
       CreateFailureInvalidWorkforcePoolAudience) {
  const char* url_options_string =
      "{\"type\":\"external_account\",\"audience\":\"invalid_workforce_pool_"
      "audience\",\"subject_"
      "token_type\":\"subject_token_type\",\"service_account_impersonation_"
      "url\":\"service_account_impersonation_url\","
      "\"token_url\":\"https://foo.com:5555/"
      "token\",\"token_info_url\":\"https://foo.com:5555/"
      "token_info\",\"credential_source\":{\"url\":\"https://foo.com:5555/"
      "generate_subject_token_format_json\",\"headers\":{\"Metadata-Flavor\":"
      "\"Google\"},\"format\":{\"type\":\"json\",\"subject_token_field_name\":"
      "\"access_token\"}},\"quota_project_id\":\"quota_"
      "project_id\",\"client_id\":\"client_id\",\"client_secret\":\"client_"
      "secret\",\"workforce_pool_user_project\":\"workforce_pool_user_"
      "project\"}";
  const char* url_scopes_string = "scope1,scope2";
  grpc_call_credentials* url_creds = grpc_external_account_credentials_create(
      url_options_string, url_scopes_string);
  ASSERT_EQ(url_creds, nullptr);
}

TEST_F(CredentialsTest, TestFakeCallCredentialsCompareSuccess) {
  auto call_creds = MakeRefCounted<fake_call_creds>();
  CHECK_EQ(call_creds->cmp(call_creds.get()), 0);
}

TEST_F(CredentialsTest, TestFakeCallCredentialsCompareFailure) {
  auto fake_creds = MakeRefCounted<fake_call_creds>();
  auto* md_creds = grpc_md_only_test_credentials_create("key", "value");
  CHECK_NE(fake_creds->cmp(md_creds), 0);
  CHECK_NE(md_creds->cmp(fake_creds.get()), 0);
  grpc_call_credentials_release(md_creds);
}

TEST_F(CredentialsTest, TestHttpRequestSSLCredentialsCompare) {
  auto creds_1 = CreateHttpRequestSSLCredentials();
  auto creds_2 = CreateHttpRequestSSLCredentials();
  EXPECT_EQ(creds_1->cmp(creds_2.get()), 0);
  EXPECT_EQ(creds_2->cmp(creds_1.get()), 0);
}

TEST_F(CredentialsTest, TestHttpRequestSSLCredentialsSingleton) {
  auto creds_1 = CreateHttpRequestSSLCredentials();
  auto creds_2 = CreateHttpRequestSSLCredentials();
  EXPECT_EQ(creds_1, creds_2);
}

class GcpServiceAccountIdentityCredentialsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grpc_init();
    g_http_status = 200;
    g_audience = "";
    g_token = nullptr;
    g_on_http_request_error = nullptr;
    HttpRequest::SetOverride(HttpGetOverride, httpcli_post_should_not_be_called,
                             httpcli_put_should_not_be_called);
  }

  void TearDown() override {
    HttpRequest::SetOverride(nullptr, nullptr, nullptr);
    grpc_shutdown_blocking();
  }

  static void ValidateHttpRequest(const grpc_http_request* request,
                                  const URI& uri) {
    EXPECT_EQ(uri.authority(), "metadata.google.internal.");
    EXPECT_EQ(uri.path(),
              "/computeMetadata/v1/instance/service-accounts/default/identity");
    EXPECT_THAT(
        uri.query_parameter_map(),
        ::testing::ElementsAre(::testing::Pair("audience", g_audience)));
    ASSERT_EQ(request->hdr_count, 1);
    EXPECT_EQ(absl::string_view(request->hdrs[0].key), "Metadata-Flavor");
    EXPECT_EQ(absl::string_view(request->hdrs[0].value), "Google");
  }

  static int HttpGetOverride(const grpc_http_request* request, const URI& uri,
                             Timestamp /*deadline*/, grpc_closure* on_done,
                             grpc_http_response* response) {
    // Validate request.
    ValidateHttpRequest(request, uri);
    // Generate response.
    *response = http_response(g_http_status, g_token == nullptr ? "" : g_token);
    ExecCtx::Run(DEBUG_LOCATION, on_done,
                 g_on_http_request_error == nullptr ? absl::OkStatus()
                                                    : *g_on_http_request_error);
    return 1;
  }

  // Constructs a synthetic JWT token that's just valid enough for the
  // call creds to extract the expiration date.
  static std::string MakeToken(Timestamp expiration) {
    gpr_timespec ts = expiration.as_timespec(GPR_CLOCK_REALTIME);
    std::string json = absl::StrCat("{\"exp\":", ts.tv_sec, "}");
    return absl::StrCat("foo.", absl::WebSafeBase64Escape(json), ".bar");
  }

  static int g_http_status;
  static absl::string_view g_audience;
  static const char* g_token;
  static absl::Status* g_on_http_request_error;
};

int GcpServiceAccountIdentityCredentialsTest::g_http_status;
absl::string_view GcpServiceAccountIdentityCredentialsTest::g_audience;
const char* GcpServiceAccountIdentityCredentialsTest::g_token;
absl::Status* GcpServiceAccountIdentityCredentialsTest::g_on_http_request_error;

TEST_F(GcpServiceAccountIdentityCredentialsTest, Basic) {
  g_audience = "CV-6";
  auto token = MakeToken(Timestamp::Now() + Duration::Hours(1));
  g_token = token.c_str();
  ExecCtx exec_ctx;
  auto creds =
      MakeRefCounted<GcpServiceAccountIdentityCallCredentials>(g_audience);
  CHECK_EQ(creds->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(absl::OkStatus(), g_token);
  state->RunRequestMetadataTest(creds.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
}

// HTTP status 429 is mapped to UNAVAILABLE as per
// https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md.
TEST_F(GcpServiceAccountIdentityCredentialsTest, FailsWithHttpStatus429) {
  g_audience = "CV-5_Midway";
  g_http_status = 429;
  ExecCtx exec_ctx;
  auto creds =
      MakeRefCounted<GcpServiceAccountIdentityCallCredentials>(g_audience);
  CHECK_EQ(creds->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::UnavailableError("JWT fetch failed with status 429"), "");
  state->RunRequestMetadataTest(creds.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
}

// HTTP status 400 is mapped to INTERNAL as per
// https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md,
// so it should be rewritten as UNAUTHENTICATED.
TEST_F(GcpServiceAccountIdentityCredentialsTest, FailsWithHttpStatus400) {
  g_audience = "CV-8_SantaCruzIslands";
  g_http_status = 400;
  ExecCtx exec_ctx;
  auto creds =
      MakeRefCounted<GcpServiceAccountIdentityCallCredentials>(g_audience);
  CHECK_EQ(creds->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::UnauthenticatedError("JWT fetch failed with status 400"), "");
  state->RunRequestMetadataTest(creds.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
}

TEST_F(GcpServiceAccountIdentityCredentialsTest, FailsWithHttpIOError) {
  g_audience = "CV-2_CoralSea";
  absl::Status status = absl::InternalError("uh oh");
  g_on_http_request_error = &status;
  ExecCtx exec_ctx;
  auto creds =
      MakeRefCounted<GcpServiceAccountIdentityCallCredentials>(g_audience);
  CHECK_EQ(creds->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::UnavailableError("INTERNAL:uh oh"), "");
  state->RunRequestMetadataTest(creds.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
}

TEST_F(GcpServiceAccountIdentityCredentialsTest, TokenHasWrongNumberOfDots) {
  g_audience = "CV-7_Guadalcanal";
  std::string bad_token = "foo.bar";
  g_token = bad_token.c_str();
  ExecCtx exec_ctx;
  auto creds =
      MakeRefCounted<GcpServiceAccountIdentityCallCredentials>(g_audience);
  CHECK_EQ(creds->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::UnauthenticatedError("error parsing JWT token"), "");
  state->RunRequestMetadataTest(creds.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
}

TEST_F(GcpServiceAccountIdentityCredentialsTest, TokenPayloadNotBase64) {
  g_audience = "CVE-56_Makin";
  std::string bad_token = "foo.&.bar";
  g_token = bad_token.c_str();
  ExecCtx exec_ctx;
  auto creds =
      MakeRefCounted<GcpServiceAccountIdentityCallCredentials>(g_audience);
  CHECK_EQ(creds->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::UnauthenticatedError("error parsing JWT token"), "");
  state->RunRequestMetadataTest(creds.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
}

TEST_F(GcpServiceAccountIdentityCredentialsTest, TokenPayloadNotJson) {
  g_audience = "CVE-73_Samar";
  std::string bad_token =
      absl::StrCat("foo.", absl::WebSafeBase64Escape("xxx"), ".bar");
  g_token = bad_token.c_str();
  ExecCtx exec_ctx;
  auto creds =
      MakeRefCounted<GcpServiceAccountIdentityCallCredentials>(g_audience);
  CHECK_EQ(creds->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::UnauthenticatedError("error parsing JWT token"), "");
  state->RunRequestMetadataTest(creds.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
}

TEST_F(GcpServiceAccountIdentityCredentialsTest, TokenInvalidExpiration) {
  g_audience = "CVL-23_Leyte";
  std::string bad_token = absl::StrCat(
      "foo.", absl::WebSafeBase64Escape("{\"exp\":\"foo\"}"), ".bar");
  g_token = bad_token.c_str();
  ExecCtx exec_ctx;
  auto creds =
      MakeRefCounted<GcpServiceAccountIdentityCallCredentials>(g_audience);
  CHECK_EQ(creds->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  auto state = RequestMetadataState::NewInstance(
      absl::UnauthenticatedError("error parsing JWT token"), "");
  state->RunRequestMetadataTest(creds.get(), kTestUrlScheme, kTestAuthority,
                                kTestPath);
  ExecCtx::Get()->Flush();
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
