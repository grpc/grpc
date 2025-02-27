//
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
//

#include <grpc/grpc_security.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/credentials/call/call_credentials.h"
#include "src/core/credentials/transport/composite/composite_channel_credentials.h"
#include "src/core/credentials/transport/fake/fake_credentials.h"
#include "test/core/test_util/test_call_creds.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

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

TEST(CompositeChannelCredentialsTest, CompareSuccess) {
  auto* insecure_creds = grpc_insecure_credentials_create();
  auto fake_creds = MakeRefCounted<fake_call_creds>();
  auto* composite_creds_1 = grpc_composite_channel_credentials_create(
      insecure_creds, fake_creds.get(), nullptr);
  auto* composite_creds_2 = grpc_composite_channel_credentials_create(
      insecure_creds, fake_creds.get(), nullptr);
  EXPECT_EQ(composite_creds_1->cmp(composite_creds_2), 0);
  EXPECT_EQ(composite_creds_2->cmp(composite_creds_1), 0);
  grpc_channel_credentials_release(insecure_creds);
  grpc_channel_credentials_release(composite_creds_1);
  grpc_channel_credentials_release(composite_creds_2);
}

TEST(CompositeChannelCredentialsTest, RecursiveDuplicateWithoutCallCreds) {
  auto* insecure_creds = grpc_insecure_credentials_create();
  auto inner_fake_creds = MakeRefCounted<fake_call_creds>();
  auto outer_fake_creds = MakeRefCounted<fake_call_creds>();
  auto* inner_composite_creds = grpc_composite_channel_credentials_create(
      insecure_creds, inner_fake_creds.get(), nullptr);
  auto* outer_composite_creds = grpc_composite_channel_credentials_create(
      inner_composite_creds, outer_fake_creds.get(), nullptr);
  auto duplicate_without_call_creds =
      outer_composite_creds->duplicate_without_call_credentials();
  EXPECT_EQ(duplicate_without_call_creds.get(), insecure_creds);
  grpc_channel_credentials_release(insecure_creds);
  grpc_channel_credentials_release(inner_composite_creds);
  grpc_channel_credentials_release(outer_composite_creds);
}

TEST(CompositeChannelCredentialsTest, CompareFailureDifferentChannelCreds) {
  auto* insecure_creds = grpc_insecure_credentials_create();
  auto* fake_channel_creds = grpc_fake_transport_security_credentials_create();
  auto fake_creds = MakeRefCounted<fake_call_creds>();
  auto* composite_creds_1 = grpc_composite_channel_credentials_create(
      insecure_creds, fake_creds.get(), nullptr);
  auto* composite_creds_2 = grpc_composite_channel_credentials_create(
      fake_channel_creds, fake_creds.get(), nullptr);
  EXPECT_NE(composite_creds_1->cmp(composite_creds_2), 0);
  EXPECT_NE(composite_creds_2->cmp(composite_creds_1), 0);
  grpc_channel_credentials_release(insecure_creds);
  grpc_channel_credentials_release(fake_channel_creds);
  grpc_channel_credentials_release(composite_creds_1);
  grpc_channel_credentials_release(composite_creds_2);
}

TEST(CompositeChannelCredentialsTest, CompareFailureDifferentCallCreds) {
  auto* insecure_creds = grpc_insecure_credentials_create();
  auto fake_creds = MakeRefCounted<fake_call_creds>();
  auto* md_creds = grpc_md_only_test_credentials_create("key", "value");
  auto* composite_creds_1 = grpc_composite_channel_credentials_create(
      insecure_creds, fake_creds.get(), nullptr);
  auto* composite_creds_2 = grpc_composite_channel_credentials_create(
      insecure_creds, md_creds, nullptr);
  EXPECT_NE(composite_creds_1->cmp(composite_creds_2), 0);
  EXPECT_NE(composite_creds_2->cmp(composite_creds_1), 0);
  grpc_channel_credentials_release(insecure_creds);
  grpc_call_credentials_release(md_creds);
  grpc_channel_credentials_release(composite_creds_1);
  grpc_channel_credentials_release(composite_creds_2);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  const auto result = RUN_ALL_TESTS();
  return result;
}
