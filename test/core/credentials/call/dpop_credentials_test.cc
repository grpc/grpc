// Copyright 2025 gRPC authors.
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

#include "src/core/credentials/call/dpop/dpop_credentials.h"

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security_constants.h>
#include <stdlib.h>

#include <string>
#include <utility>
#include <vector>

#include "src/core/call/metadata_batch.h"
#include "src/core/credentials/transport/security_connector.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/exec_ctx_wakeup_scheduler.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/transport/auth_context.h"
#include "src/core/util/notification.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/test_util/test_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {

using ::testing::HasSubstr;

// P-256 key generated for tests only.
constexpr char kTestEcPrivatePem[] =
    "-----BEGIN EC PRIVATE KEY-----\n"
    "MHcCAQEEIPMEEXa2VO4i23yTei/hh39yx6JuNy8a3bmJlQgQTXVwoAoGCCqGSM49\n"
    "AwEHoUQDQgAE54m/R+zIUc5kUwBzPyoe07jqN3DCOlwML7Lg3wYKR58N5uC6hrIe\n"
    "fFvM3DicHFiuT1yg1XCaj4CHv+jxMNUlMA==\n"
    "-----END EC PRIVATE KEY-----\n";

constexpr char kTestAccessToken[] = "test-access-token";
constexpr char kTestAuthority[] = "foo.test.google.fr:443";
constexpr char kTestPath[] = "/foo/bar";
constexpr char kTestChannelBindingA[] =
    "tls-exporter:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
constexpr char kTestChannelBindingB[] =
    "tls-exporter:BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB";

class BogusSecurityConnector : public grpc_channel_security_connector {
 public:
  explicit BogusSecurityConnector(absl::string_view url_scheme)
      : grpc_channel_security_connector(url_scheme, nullptr, nullptr) {}

  void check_peer(tsi_peer, grpc_endpoint*, const ChannelArgs&,
                  RefCountedPtr<grpc_auth_context>*, grpc_closure*) override {
    abort();
  }

  void cancel_check_peer(grpc_closure*, grpc_error_handle) override { abort(); }

  int cmp(const grpc_security_connector*) const override {
    abort();
    return 0;
  }

  ArenaPromise<absl::Status> CheckCallHost(absl::string_view,
                                           grpc_auth_context*) override {
    return Immediate(absl::PermissionDeniedError("unreachable"));
  }

  void add_handshakers(const ChannelArgs&, grpc_pollset_set*,
                       HandshakeManager*) override {
    abort();
  }
};

bool Base64UrlUnescape(absl::string_view in, std::string* out) {
  std::string padded(in);
  while (padded.size() % 4 != 0) padded.push_back('=');
  return absl::WebSafeBase64Unescape(padded, out);
}

RefCountedPtr<grpc_auth_context> MakeSslAuthContext(
    absl::string_view channel_binding = kTestChannelBindingA) {
  auto auth_context = MakeRefCounted<grpc_auth_context>(nullptr);
  auth_context->add_cstring_property(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                                     GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  if (!channel_binding.empty()) {
    auth_context->add_property(GRPC_TLS_CHANNEL_BINDING_PROPERTY_NAME,
                               channel_binding.data(), channel_binding.size());
  }
  return auth_context;
}

struct DpopMetadata {
  std::string authorization;
  std::string dpop;
};

absl::StatusOr<DpopMetadata> GetDpopMetadata(
    grpc_call_credentials* creds, absl::string_view url_scheme,
    absl::string_view authority, absl::string_view path,
    RefCountedPtr<grpc_auth_context> auth_context = nullptr) {
  ExecCtx exec_ctx;
  auto arena = SimpleArenaAllocator()->MakeArena();
  grpc_metadata_batch md;
  md.Set(HttpAuthorityMetadata(), Slice::FromCopiedString(authority));
  md.Set(HttpPathMetadata(), Slice::FromCopiedString(path));

  grpc_call_credentials::GetRequestMetadataArgs args;
  args.security_connector = MakeRefCounted<BogusSecurityConnector>(url_scheme);
  args.auth_context = std::move(auth_context);

  grpc_polling_entity pollent =
      grpc_polling_entity_create_from_pollset_set(grpc_pollset_set_create());
  Notification done;
  absl::Status status;
  auto activity = MakeActivity(
      [creds, &md, &args] {
        return Map(
            creds->GetRequestMetadata(
                ClientMetadataHandle(&md, Arena::PooledDeleter(nullptr)),
                &args),
            [](absl::StatusOr<ClientMetadataHandle> metadata) {
              return metadata.status();
            });
      },
      ExecCtxWakeupScheduler(),
      [&](absl::Status s) {
        status = std::move(s);
        done.Notify();
      },
      arena.get(), &pollent);
  done.WaitForNotification();
  activity.reset();
  grpc_pollset_set_destroy(grpc_polling_entity_pollset_set(&pollent));
  if (!status.ok()) return status;

  DpopMetadata out;
  md.Remove(HttpAuthorityMetadata());
  md.Remove(HttpPathMetadata());
  md.Log([&out](absl::string_view key, absl::string_view value) {
    if (key == GRPC_AUTHORIZATION_METADATA_KEY) {
      out.authorization = std::string(value);
    } else if (key == GRPC_DPOP_PROOF_METADATA_KEY) {
      out.dpop = std::string(value);
    }
  });
  return out;
}

TEST(DpopCredentialsTest, CreateRejectsEmptyInputs) {
  EXPECT_EQ(nullptr,
            grpc_dpop_credentials_create("", kTestEcPrivatePem, nullptr));
  EXPECT_EQ(nullptr,
            grpc_dpop_credentials_create(kTestAccessToken, "", nullptr));
  EXPECT_EQ(nullptr, grpc_dpop_credentials_create(nullptr, kTestEcPrivatePem,
                                                  nullptr));
  EXPECT_EQ(nullptr, grpc_dpop_credentials_create(kTestAccessToken, nullptr,
                                                  nullptr));
}

TEST(DpopCredentialsTest, CreateSucceeds) {
  grpc_call_credentials* creds = grpc_dpop_credentials_create(
      kTestAccessToken, kTestEcPrivatePem, nullptr);
  ASSERT_NE(creds, nullptr);
  EXPECT_EQ(creds->type(), grpc_dpop_credentials::Type());
  EXPECT_EQ(creds->min_security_level(), GRPC_PRIVACY_AND_INTEGRITY);
  EXPECT_EQ(creds->debug_string(), "DpopCredentials{token:present}");
  creds->Unref();
}

TEST(DpopCredentialsTest, RequiresTlsConnection) {
  grpc_call_credentials* creds = grpc_dpop_credentials_create(
      kTestAccessToken, kTestEcPrivatePem, nullptr);
  ASSERT_NE(creds, nullptr);

  auto result = GetDpopMetadata(creds, "http", kTestAuthority, kTestPath);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kUnauthenticated);
  EXPECT_THAT(result.status().message(), HasSubstr("TLS-secured connection"));

  creds->Unref();
}

TEST(DpopCredentialsTest, RejectsNonSslAuthContext) {
  grpc_call_credentials* creds = grpc_dpop_credentials_create(
      kTestAccessToken, kTestEcPrivatePem, nullptr);
  ASSERT_NE(creds, nullptr);

  auto auth_context = MakeRefCounted<grpc_auth_context>(nullptr);
  auth_context->add_cstring_property(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                                     "insecure");

  auto result = GetDpopMetadata(creds, GRPC_SSL_URL_SCHEME, kTestAuthority,
                                kTestPath, std::move(auth_context));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kUnauthenticated);
  EXPECT_THAT(result.status().message(), HasSubstr("TLS-secured connection"));

  creds->Unref();
}

TEST(DpopCredentialsTest, RequiresTlsChannelBinding) {
  grpc_call_credentials* creds = grpc_dpop_credentials_create(
      kTestAccessToken, kTestEcPrivatePem, nullptr);
  ASSERT_NE(creds, nullptr);

  // SSL auth context without channel binding must fail closed.
  auto result = GetDpopMetadata(creds, GRPC_SSL_URL_SCHEME, kTestAuthority,
                                kTestPath, MakeSslAuthContext(""));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kUnauthenticated);
  EXPECT_THAT(result.status().message(), HasSubstr("channel binding"));

  creds->Unref();
}

TEST(DpopCredentialsTest, AttachesDpopProofBoundToTlsSession) {
  grpc_call_credentials* creds = grpc_dpop_credentials_create(
      kTestAccessToken, kTestEcPrivatePem, nullptr);
  ASSERT_NE(creds, nullptr);

  auto result =
      GetDpopMetadata(creds, GRPC_SSL_URL_SCHEME, kTestAuthority, kTestPath,
                      MakeSslAuthContext(kTestChannelBindingA));
  ASSERT_TRUE(result.ok()) << result.status();

  EXPECT_EQ(result->authorization, absl::StrCat("DPoP ", kTestAccessToken));
  ASSERT_FALSE(result->dpop.empty());

  std::vector<absl::string_view> parts = absl::StrSplit(result->dpop, '.');
  ASSERT_EQ(parts.size(), 3u);

  std::string header_json;
  ASSERT_TRUE(Base64UrlUnescape(parts[0], &header_json));
  EXPECT_THAT(header_json, HasSubstr("\"typ\":\"dpop+jwt\""));
  EXPECT_THAT(header_json, HasSubstr("\"alg\":\"ES256\""));
  EXPECT_THAT(header_json, HasSubstr("\"kty\":\"EC\""));

  std::string claims_json;
  ASSERT_TRUE(Base64UrlUnescape(parts[1], &claims_json));
  EXPECT_THAT(claims_json, HasSubstr("\"htm\":\"POST\""));
  EXPECT_THAT(claims_json,
              HasSubstr("\"htu\":\"https://foo.test.google.fr/foo/bar\""));
  EXPECT_THAT(claims_json, HasSubstr("\"ath\":"));
  EXPECT_THAT(claims_json, HasSubstr("\"jti\":"));
  EXPECT_THAT(claims_json, HasSubstr("\"iat\":"));
  EXPECT_THAT(claims_json,
              HasSubstr(absl::StrCat("\"tls_channel_binding\":\"",
                                     kTestChannelBindingA, "\"")));

  creds->Unref();
}

TEST(DpopCredentialsTest, DifferentTlsSessionsProduceDifferentProofs) {
  grpc_call_credentials* creds = grpc_dpop_credentials_create(
      kTestAccessToken, kTestEcPrivatePem, nullptr);
  ASSERT_NE(creds, nullptr);

  auto result_a =
      GetDpopMetadata(creds, GRPC_SSL_URL_SCHEME, kTestAuthority, kTestPath,
                      MakeSslAuthContext(kTestChannelBindingA));
  auto result_b =
      GetDpopMetadata(creds, GRPC_SSL_URL_SCHEME, kTestAuthority, kTestPath,
                      MakeSslAuthContext(kTestChannelBindingB));
  ASSERT_TRUE(result_a.ok()) << result_a.status();
  ASSERT_TRUE(result_b.ok()) << result_b.status();
  EXPECT_NE(result_a->dpop, result_b->dpop);

  std::string claims_a;
  std::string claims_b;
  std::vector<absl::string_view> parts_a = absl::StrSplit(result_a->dpop, '.');
  std::vector<absl::string_view> parts_b = absl::StrSplit(result_b->dpop, '.');
  ASSERT_EQ(parts_a.size(), 3u);
  ASSERT_EQ(parts_b.size(), 3u);
  ASSERT_TRUE(Base64UrlUnescape(parts_a[1], &claims_a));
  ASSERT_TRUE(Base64UrlUnescape(parts_b[1], &claims_b));
  EXPECT_THAT(claims_a, HasSubstr(kTestChannelBindingA));
  EXPECT_THAT(claims_b, HasSubstr(kTestChannelBindingB));
  EXPECT_THAT(claims_a, ::testing::Not(HasSubstr(kTestChannelBindingB)));
  EXPECT_THAT(claims_b, ::testing::Not(HasSubstr(kTestChannelBindingA)));

  creds->Unref();
}

TEST(DpopCredentialsTest, InvalidPemFailsProof) {
  grpc_call_credentials* creds = grpc_dpop_credentials_create(
      kTestAccessToken,
      "-----BEGIN EC PRIVATE KEY-----\nnot-a-key\n-----END EC PRIVATE KEY-----\n",
      nullptr);
  ASSERT_NE(creds, nullptr);

  auto result =
      GetDpopMetadata(creds, GRPC_SSL_URL_SCHEME, kTestAuthority, kTestPath,
                      MakeSslAuthContext(kTestChannelBindingA));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kUnauthenticated);
  EXPECT_THAT(result.status().message(), HasSubstr("failed to build proof"));

  creds->Unref();
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
