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
#include <grpc/status.h>

#include <memory>
#include <optional>

#include "absl/log/log.h"
#include "gtest/gtest.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/time.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/test_util/test_call_creds.h"

namespace grpc_core {
namespace {

const char iam_token[] = "token";
const char iam_selector[] = "selector";
const char overridden_iam_token[] = "overridden_token";
const char overridden_iam_selector[] = "overridden_selector";
const char fake_md_key[] = "fake_key";
const char fake_md_value[] = "fake_value";
const char overridden_fake_md_key[] = "overridden_fake_key";
const char overridden_fake_md_value[] = "overridden_fake_value";

void PrintAuthContext(bool is_client, const grpc_auth_context* ctx) {
  const grpc_auth_property* p;
  grpc_auth_property_iterator it;
  LOG(INFO) << (is_client ? "client" : "server") << " peer:";
  LOG(INFO) << "\tauthenticated: "
            << (grpc_auth_context_peer_is_authenticated(ctx) ? "YES" : "NO");
  it = grpc_auth_context_peer_identity(ctx);
  while ((p = grpc_auth_property_iterator_next(&it)) != nullptr) {
    LOG(INFO) << "\t\t" << p->name << ": " << p->value;
  }
  LOG(INFO) << "\tall properties:";
  it = grpc_auth_context_property_iterator(ctx);
  while ((p = grpc_auth_property_iterator_next(&it)) != nullptr) {
    LOG(INFO) << "\t\t" << p->name << ": " << p->value;
  }
}

void TestRequestResponseWithPayloadAndCallCreds(CoreEnd2endTest& test,
                                                bool use_secure_call_creds) {
  auto c = test.NewClientCall("/foo").Timeout(Duration::Minutes(1)).Create();
  grpc_call_credentials* creds;
  if (use_secure_call_creds) {
    creds =
        grpc_google_iam_credentials_create(iam_token, iam_selector, nullptr);
  } else {
    creds = grpc_md_only_test_credentials_create(fake_md_key, fake_md_value);
  }
  EXPECT_NE(creds, nullptr);
  c.SetCredentials(creds);
  IncomingMetadata server_initial_metadata;
  IncomingMessage server_message;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage("hello world")
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvMessage(server_message)
      .RecvStatusOnClient(server_status);
  auto s = test.RequestCall(101);
  test.Expect(101, true);
  test.Step();
  PrintAuthContext(false, s.GetAuthContext().get());
  PrintAuthContext(true, c.GetAuthContext().get());
  // Cannot set creds on the server call object.
  EXPECT_NE(grpc_call_set_credentials(s.c_call(), nullptr), GRPC_CALL_OK);
  IncomingMessage client_message;
  s.NewBatch(102).SendInitialMetadata({}).RecvMessage(client_message);
  test.Expect(102, true);
  test.Step();
  IncomingCloseOnServer client_close;
  s.NewBatch(103)
      .RecvCloseOnServer(client_close)
      .SendMessage("hello you")
      .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});
  test.Expect(103, true);
  test.Expect(1, true);
  test.Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_OK);
  EXPECT_EQ(server_status.message(), IsErrorFlattenEnabled() ? "" : "xyz");
  EXPECT_EQ(s.method(), "/foo");
  EXPECT_FALSE(client_close.was_cancelled());
  EXPECT_EQ(client_message.payload(), "hello world");
  EXPECT_EQ(server_message.payload(), "hello you");
  if (use_secure_call_creds) {
    EXPECT_EQ(s.GetInitialMetadata(GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY),
              iam_token);
    EXPECT_EQ(s.GetInitialMetadata(GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY),
              iam_selector);
  } else {
    EXPECT_EQ(s.GetInitialMetadata(fake_md_key), fake_md_value);
  }
}

void TestRequestResponseWithPayloadAndOverriddenCallCreds(
    CoreEnd2endTest& test, bool use_secure_call_creds) {
  auto c = test.NewClientCall("/foo").Timeout(Duration::Minutes(1)).Create();
  grpc_call_credentials* creds;
  if (use_secure_call_creds) {
    creds =
        grpc_google_iam_credentials_create(iam_token, iam_selector, nullptr);
  } else {
    creds = grpc_md_only_test_credentials_create(fake_md_key, fake_md_value);
  }
  EXPECT_NE(creds, nullptr);
  c.SetCredentials(creds);
  if (use_secure_call_creds) {
    creds = grpc_google_iam_credentials_create(
        overridden_iam_token, overridden_iam_selector, nullptr);
  } else {
    creds = grpc_md_only_test_credentials_create(overridden_fake_md_key,
                                                 overridden_fake_md_value);
  }
  c.SetCredentials(creds);
  IncomingMetadata server_initial_metadata;
  IncomingMessage server_message;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage("hello world")
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvMessage(server_message)
      .RecvStatusOnClient(server_status);
  auto s = test.RequestCall(101);
  test.Expect(101, true);
  test.Step();
  PrintAuthContext(false, s.GetAuthContext().get());
  PrintAuthContext(true, c.GetAuthContext().get());
  // Cannot set creds on the server call object.
  EXPECT_NE(grpc_call_set_credentials(s.c_call(), nullptr), GRPC_CALL_OK);
  IncomingMessage client_message;
  s.NewBatch(102).SendInitialMetadata({}).RecvMessage(client_message);
  test.Expect(102, true);
  test.Step();
  IncomingCloseOnServer client_close;
  s.NewBatch(103)
      .RecvCloseOnServer(client_close)
      .SendMessage("hello you")
      .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});
  test.Expect(103, true);
  test.Expect(1, true);
  test.Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_OK);
  EXPECT_EQ(server_status.message(), IsErrorFlattenEnabled() ? "" : "xyz");
  EXPECT_EQ(s.method(), "/foo");
  EXPECT_FALSE(client_close.was_cancelled());
  EXPECT_EQ(client_message.payload(), "hello world");
  EXPECT_EQ(server_message.payload(), "hello you");
  if (use_secure_call_creds) {
    EXPECT_EQ(s.GetInitialMetadata(GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY),
              overridden_iam_token);
    EXPECT_EQ(s.GetInitialMetadata(GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY),
              overridden_iam_selector);
  } else {
    EXPECT_EQ(s.GetInitialMetadata(overridden_fake_md_key),
              overridden_fake_md_value);
  }
}

void TestRequestResponseWithPayloadAndDeletedCallCreds(
    CoreEnd2endTest& test, bool use_secure_call_creds) {
  auto c = test.NewClientCall("/foo").Timeout(Duration::Minutes(1)).Create();
  grpc_call_credentials* creds;
  if (use_secure_call_creds) {
    creds =
        grpc_google_iam_credentials_create(iam_token, iam_selector, nullptr);
  } else {
    creds = grpc_md_only_test_credentials_create(fake_md_key, fake_md_value);
  }
  EXPECT_NE(creds, nullptr);
  c.SetCredentials(creds);
  c.SetCredentials(nullptr);
  IncomingMetadata server_initial_metadata;
  IncomingMessage server_message;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage("hello world")
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvMessage(server_message)
      .RecvStatusOnClient(server_status);
  auto s = test.RequestCall(101);
  test.Expect(101, true);
  test.Step();
  PrintAuthContext(false, s.GetAuthContext().get());
  PrintAuthContext(true, c.GetAuthContext().get());
  // Cannot set creds on the server call object.
  EXPECT_NE(grpc_call_set_credentials(s.c_call(), nullptr), GRPC_CALL_OK);
  IncomingMessage client_message;
  s.NewBatch(102).SendInitialMetadata({}).RecvMessage(client_message);
  test.Expect(102, true);
  test.Step();
  IncomingCloseOnServer client_close;
  s.NewBatch(103)
      .RecvCloseOnServer(client_close)
      .SendMessage("hello you")
      .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});
  test.Expect(103, true);
  test.Expect(1, true);
  test.Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_OK);
  EXPECT_EQ(server_status.message(), IsErrorFlattenEnabled() ? "" : "xyz");
  EXPECT_EQ(s.method(), "/foo");
  EXPECT_FALSE(client_close.was_cancelled());
  EXPECT_EQ(client_message.payload(), "hello world");
  EXPECT_EQ(server_message.payload(), "hello you");
  EXPECT_EQ(s.GetInitialMetadata(GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY),
            std::nullopt);
  EXPECT_EQ(s.GetInitialMetadata(GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY),
            std::nullopt);
  EXPECT_EQ(s.GetInitialMetadata(fake_md_key), std::nullopt);
}

CORE_END2END_TEST(PerCallCredsOnInsecureTests,
                  RequestWithServerRejectingClientCreds) {
  InitClient(ChannelArgs());
  InitServer(ChannelArgs().Set(FAIL_AUTH_CHECK_SERVER_ARG_NAME, true));
  auto c = NewClientCall("/foo").Timeout(Duration::Minutes(1)).Create();
  auto* creds =
      grpc_md_only_test_credentials_create(fake_md_key, fake_md_value);
  EXPECT_NE(creds, nullptr);
  c.SetCredentials(creds);
  IncomingMetadata server_initial_metadata;
  IncomingMessage server_message;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage("hello world")
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvMessage(server_message)
      .RecvStatusOnClient(server_status);
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNAUTHENTICATED);
}

CORE_END2END_TEST(PerCallCredsTests, RequestResponseWithPayloadAndCallCreds) {
  if (IsLocalConnectorSecureEnabled()) {
    SKIP_IF_LOCAL_TCP_CREDS();
  }
  TestRequestResponseWithPayloadAndCallCreds(*this, true);
}

CORE_END2END_TEST(PerCallCredsTests,
                  RequestResponseWithPayloadAndOverriddenCallCreds) {
  if (IsLocalConnectorSecureEnabled()) {
    SKIP_IF_LOCAL_TCP_CREDS();
  }
  TestRequestResponseWithPayloadAndOverriddenCallCreds(*this, true);
}

CORE_END2END_TEST(PerCallCredsTests,
                  RequestResponseWithPayloadAndDeletedCallCreds) {
  TestRequestResponseWithPayloadAndDeletedCallCreds(*this, true);
}

CORE_END2END_TEST(PerCallCredsTests,
                  RequestResponseWithPayloadAndInsecureCallCreds) {
  TestRequestResponseWithPayloadAndCallCreds(*this, false);
}

CORE_END2END_TEST(PerCallCredsTests,
                  RequestResponseWithPayloadAndOverriddenInsecureCallCreds) {
  TestRequestResponseWithPayloadAndOverriddenCallCreds(*this, false);
}

CORE_END2END_TEST(PerCallCredsTests,
                  RequestResponseWithPayloadAndDeletedInsecureCallCreds) {
  TestRequestResponseWithPayloadAndDeletedCallCreds(*this, false);
}

CORE_END2END_TEST(PerCallCredsOnInsecureTests,
                  RequestResponseWithPayloadAndInsecureCallCreds) {
  TestRequestResponseWithPayloadAndCallCreds(*this, false);
}

CORE_END2END_TEST(PerCallCredsOnInsecureTests,
                  RequestResponseWithPayloadAndOverriddenInsecureCallCreds) {
  TestRequestResponseWithPayloadAndOverriddenCallCreds(*this, false);
}

CORE_END2END_TEST(PerCallCredsOnInsecureTests,
                  RequestResponseWithPayloadAndDeletedInsecureCallCreds) {
  TestRequestResponseWithPayloadAndDeletedCallCreds(*this, false);
}

CORE_END2END_TEST(PerCallCredsOnInsecureTests, FailToSendCallCreds) {
  auto c = NewClientCall("/foo").Timeout(Duration::Seconds(5)).Create();
  grpc_call_credentials* creds;
  creds = grpc_google_iam_credentials_create(iam_token, iam_selector, nullptr);
  EXPECT_NE(creds, nullptr);
  c.SetCredentials(creds);
  IncomingMetadata server_initial_metadata;
  IncomingMessage server_message;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage("hello world")
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvMessage(server_message)
      .RecvStatusOnClient(server_status);
  // Expect the call to fail since the channel credentials did not satisfy the
  // minimum security level requirements.
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNAUTHENTICATED);
}

}  // namespace
}  // namespace grpc_core
