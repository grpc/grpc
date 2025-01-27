// Copyright 2021 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include <string>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/security/authorization/authorization_policy_provider.h"
#include "src/core/lib/security/authorization/grpc_authorization_policy_provider.h"
#include "src/core/util/notification.h"
#include "src/core/util/time.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/test_util/tls_utils.h"

namespace grpc_core {
namespace {

void TestAllowAuthorizedRequest(CoreEnd2endTest& test) {
  auto c = test.NewClientCall("/foo").Timeout(Duration::Seconds(5)).Create();
  IncomingMetadata server_initial_metadata;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  auto s = test.RequestCall(101);
  test.Expect(101, true);
  test.Step();
  IncomingCloseOnServer client_close;
  s.NewBatch(102)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {})
      .RecvCloseOnServer(client_close);
  test.Expect(102, true);
  test.Expect(1, true);
  test.Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_OK);
}

void TestDenyUnauthorizedRequest(CoreEnd2endTest& test) {
  auto c = test.NewClientCall("/foo").Timeout(Duration::Seconds(5)).Create();
  IncomingMetadata server_initial_metadata;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  test.Expect(1, true);
  test.Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_PERMISSION_DENIED);
  EXPECT_EQ(server_status.message(), "Unauthorized RPC request rejected.");
}

void InitWithPolicy(CoreEnd2endTest& test,
                    grpc_authorization_policy_provider* provider) {
  test.InitServer(ChannelArgs().Set(
      GRPC_ARG_AUTHORIZATION_POLICY_PROVIDER,
      ChannelArgs::Pointer(provider,
                           grpc_authorization_policy_provider_arg_vtable())));
  test.InitClient(ChannelArgs());
}

void InitWithStaticData(CoreEnd2endTest& test, const char* authz_policy) {
  grpc_status_code code = GRPC_STATUS_OK;
  const char* error_details;
  grpc_authorization_policy_provider* provider =
      grpc_authorization_policy_provider_static_data_create(authz_policy, &code,
                                                            &error_details);
  EXPECT_EQ(code, GRPC_STATUS_OK);
  InitWithPolicy(test, provider);
}

class InitWithTempFile {
 public:
  InitWithTempFile(CoreEnd2endTest& test, const char* authz_policy)
      : tmp_file_(authz_policy) {
    grpc_status_code code = GRPC_STATUS_OK;
    const char* error_details;
    provider_ = grpc_authorization_policy_provider_file_watcher_create(
        tmp_file_.name().c_str(), /*refresh_interval_sec=*/1, &code,
        &error_details);
    CHECK_EQ(code, GRPC_STATUS_OK);
    InitWithPolicy(test, provider_);
  }

  InitWithTempFile(const InitWithTempFile&) = delete;
  InitWithTempFile& operator=(const InitWithTempFile&) = delete;

  FileWatcherAuthorizationPolicyProvider* provider() {
    return dynamic_cast<FileWatcherAuthorizationPolicyProvider*>(provider_);
  }

  testing::TmpFile& file() { return tmp_file_; }

 private:
  testing::TmpFile tmp_file_;
  grpc_authorization_policy_provider* provider_;
};

CORE_END2END_TEST(SecureEnd2endTests, StaticInitAllowAuthorizedRequest) {
  InitWithStaticData(*this,
                     "{"
                     "  \"name\": \"authz\","
                     "  \"allow_rules\": ["
                     "    {"
                     "      \"name\": \"allow_foo\","
                     "      \"request\": {"
                     "        \"paths\": ["
                     "          \"*/foo\""
                     "        ]"
                     "      }"
                     "    }"
                     "  ]"
                     "}");
  TestAllowAuthorizedRequest(*this);
}

CORE_END2END_TEST(SecureEnd2endTests, StaticInitDenyUnauthorizedRequest) {
  InitWithStaticData(*this,
                     "{"
                     "  \"name\": \"authz\","
                     "  \"allow_rules\": ["
                     "    {"
                     "      \"name\": \"allow_bar\","
                     "      \"request\": {"
                     "        \"paths\": ["
                     "          \"*/bar\""
                     "        ]"
                     "      }"
                     "    }"
                     "  ],"
                     "  \"deny_rules\": ["
                     "    {"
                     "      \"name\": \"deny_foo\","
                     "      \"request\": {"
                     "        \"paths\": ["
                     "          \"*/foo\""
                     "        ]"
                     "      }"
                     "    }"
                     "  ]"
                     "}");
  TestDenyUnauthorizedRequest(*this);
}

CORE_END2END_TEST(SecureEnd2endTests, StaticInitDenyRequestNoMatchInPolicy) {
  InitWithStaticData(*this,
                     "{"
                     "  \"name\": \"authz\","
                     "  \"allow_rules\": ["
                     "    {"
                     "      \"name\": \"allow_bar\","
                     "      \"request\": {"
                     "        \"paths\": ["
                     "          \"*/bar\""
                     "        ]"
                     "      }"
                     "    }"
                     "  ]"
                     "}");
  TestDenyUnauthorizedRequest(*this);
}

CORE_END2END_TEST(SecureEnd2endTests, FileWatcherInitAllowAuthorizedRequest) {
  InitWithTempFile tmp_policy(*this,
                              "{"
                              "  \"name\": \"authz\","
                              "  \"allow_rules\": ["
                              "    {"
                              "      \"name\": \"allow_foo\","
                              "      \"request\": {"
                              "        \"paths\": ["
                              "          \"*/foo\""
                              "        ]"
                              "      }"
                              "    }"
                              "  ]"
                              "}");
  TestAllowAuthorizedRequest(*this);
}

CORE_END2END_TEST(SecureEnd2endTests, FileWatcherInitDenyUnauthorizedRequest) {
  InitWithTempFile tmp_policy(*this,
                              "{"
                              "  \"name\": \"authz\","
                              "  \"allow_rules\": ["
                              "    {"
                              "      \"name\": \"allow_bar\","
                              "      \"request\": {"
                              "        \"paths\": ["
                              "          \"*/bar\""
                              "        ]"
                              "      }"
                              "    }"
                              "  ],"
                              "  \"deny_rules\": ["
                              "    {"
                              "      \"name\": \"deny_foo\","
                              "      \"request\": {"
                              "        \"paths\": ["
                              "          \"*/foo\""
                              "        ]"
                              "      }"
                              "    }"
                              "  ]"
                              "}");
  TestDenyUnauthorizedRequest(*this);
}

CORE_END2END_TEST(SecureEnd2endTests,
                  FileWatcherInitDenyRequestNoMatchInPolicy) {
  InitWithTempFile tmp_policy(*this,
                              "{"
                              "  \"name\": \"authz\","
                              "  \"allow_rules\": ["
                              "    {"
                              "      \"name\": \"allow_bar\","
                              "      \"request\": {"
                              "        \"paths\": ["
                              "          \"*/bar\""
                              "        ]"
                              "      }"
                              "    }"
                              "  ]"
                              "}");
  TestDenyUnauthorizedRequest(*this);
}

CORE_END2END_TEST(SecureEnd2endTests, FileWatcherValidPolicyReload) {
  InitWithTempFile tmp_policy(*this,
                              "{"
                              "  \"name\": \"authz\","
                              "  \"allow_rules\": ["
                              "    {"
                              "      \"name\": \"allow_foo\","
                              "      \"request\": {"
                              "        \"paths\": ["
                              "          \"*/foo\""
                              "        ]"
                              "      }"
                              "    }"
                              "  ]"
                              "}");
  TestAllowAuthorizedRequest(*this);
  Notification on_reload_done;
  tmp_policy.provider()->SetCallbackForTesting(
      [&on_reload_done](bool contents_changed, absl::Status status) {
        if (contents_changed) {
          EXPECT_EQ(status, absl::OkStatus());
          on_reload_done.Notify();
        }
      });
  // Replace existing policy in file with a different authorization policy.
  tmp_policy.file().RewriteFile(
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_bar\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/bar\""
      "        ]"
      "      }"
      "    }"
      "  ],"
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"deny_foo\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/foo\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}");
  on_reload_done.WaitForNotification();
  TestDenyUnauthorizedRequest(*this);
  tmp_policy.provider()->SetCallbackForTesting(nullptr);
}

CORE_END2END_TEST(SecureEnd2endTests, FileWatcherInvalidPolicySkipReload) {
  InitWithTempFile tmp_policy(*this,
                              "{"
                              "  \"name\": \"authz\","
                              "  \"allow_rules\": ["
                              "    {"
                              "      \"name\": \"allow_foo\","
                              "      \"request\": {"
                              "        \"paths\": ["
                              "          \"*/foo\""
                              "        ]"
                              "      }"
                              "    }"
                              "  ]"
                              "}");
  TestAllowAuthorizedRequest(*this);
  Notification on_reload_done;
  tmp_policy.provider()->SetCallbackForTesting(
      [&on_reload_done](bool contents_changed, absl::Status status) {
        if (contents_changed) {
          EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
          EXPECT_EQ(status.message(), "\"name\" field is not present.");
          on_reload_done.Notify();
        }
      });
  // Replace existing policy in file with an invalid policy.
  tmp_policy.file().RewriteFile("{}");
  on_reload_done.WaitForNotification();
  TestAllowAuthorizedRequest(*this);
  tmp_policy.provider()->SetCallbackForTesting(nullptr);
}

CORE_END2END_TEST(SecureEnd2endTests, FileWatcherRecoversFromFailure) {
  InitWithTempFile tmp_policy(*this,
                              "{"
                              "  \"name\": \"authz\","
                              "  \"allow_rules\": ["
                              "    {"
                              "      \"name\": \"allow_foo\","
                              "      \"request\": {"
                              "        \"paths\": ["
                              "          \"*/foo\""
                              "        ]"
                              "      }"
                              "    }"
                              "  ]"
                              "}");
  TestAllowAuthorizedRequest(*this);
  Notification on_first_reload_done;
  tmp_policy.provider()->SetCallbackForTesting(
      [&on_first_reload_done](bool contents_changed, absl::Status status) {
        if (contents_changed) {
          EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
          EXPECT_EQ(status.message(), "\"name\" field is not present.");
          on_first_reload_done.Notify();
        }
      });
  // Replace existing policy in file with an invalid policy.
  tmp_policy.file().RewriteFile("{}");
  on_first_reload_done.WaitForNotification();
  TestAllowAuthorizedRequest(*this);
  Notification on_second_reload_done;
  tmp_policy.provider()->SetCallbackForTesting(
      [&on_second_reload_done](bool contents_changed, absl::Status status) {
        if (contents_changed) {
          EXPECT_EQ(status, absl::OkStatus());
          on_second_reload_done.Notify();
        }
      });
  // Recover from reload errors, by replacing invalid policy in file with a
  // valid policy.
  tmp_policy.file().RewriteFile(
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_bar\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/bar\""
      "        ]"
      "      }"
      "    }"
      "  ],"
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"deny_foo\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/foo\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}");
  on_second_reload_done.WaitForNotification();
  TestDenyUnauthorizedRequest(*this);
  tmp_policy.provider()->SetCallbackForTesting(nullptr);
}

}  // namespace
}  // namespace grpc_core
