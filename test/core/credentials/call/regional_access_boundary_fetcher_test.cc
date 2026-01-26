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

#include "src/core/credentials/call/regional_access_boundary_fetcher.h"

#include <grpc/support/time.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/credentials/call/call_credentials.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/util/env.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace {

class FakeCallCredentials : public grpc_call_credentials {
 public:
  FakeCallCredentials() : grpc_call_credentials(GRPC_SECURITY_NONE) {}

  ArenaPromise<absl::StatusOr<ClientMetadataHandle>> GetRequestMetadata(
      ClientMetadataHandle, const GetRequestMetadataArgs*) override {
    return Immediate(absl::UnimplementedError("Not implemented"));
  }

  void Orphaned() override {}

  UniqueTypeName type() const override {
    static UniqueTypeName::Factory kFactory("fake");
    return kFactory.Create();
  }

  int cmp_impl(const grpc_call_credentials*) const override { return 0; }
};

class RegionalAccessBoundaryFetcherTest : public ::testing::Test {
 protected:
  void SetUp() override {
    SetEnv("GOOGLE_AUTH_REGIONAL_ACCESS_BOUNDARY_ENABLED", "true");
    creds_ = MakeRefCounted<FakeCallCredentials>();
    arena_ = SimpleArenaAllocator()->MakeArena();
    metadata_ = arena_->MakePooled<ClientMetadata>();
    metadata_->Set(HttpAuthorityMetadata(),
                   Slice::FromStaticString("googleapis.com"));
  }

  void TearDown() override {
    UnsetEnv("GOOGLE_AUTH_REGIONAL_ACCESS_BOUNDARY_ENABLED");
  }

  RefCountedPtr<FakeCallCredentials> creds_;
  RefCountedPtr<Arena> arena_;
  ClientMetadataHandle metadata_;
};

TEST_F(RegionalAccessBoundaryFetcherTest, DisabledViaEnvVar) {
  SetEnv("GOOGLE_AUTH_REGIONAL_ACCESS_BOUNDARY_ENABLED", "false");

  auto promise = creds_->regional_access_boundary_fetcher_->Fetch(creds_, std::move(metadata_));
  EXPECT_FALSE(creds_->regional_access_boundary_fetcher_->fetch_in_flight_);
  
  Poll<absl::StatusOr<ClientMetadataHandle>> poll = promise();
  ASSERT_TRUE(poll.ready());
  auto result = std::move(poll.value());
  ASSERT_TRUE(result.ok());
}

TEST_F(RegionalAccessBoundaryFetcherTest, CacheMissTriggersFetch) {
  auto promise = creds_->regional_access_boundary_fetcher_->Fetch(creds_, std::move(metadata_));
  EXPECT_TRUE(creds_->regional_access_boundary_fetcher_->fetch_in_flight_);
  
  Poll<absl::StatusOr<ClientMetadataHandle>> poll = promise();
  ASSERT_TRUE(poll.ready());
  auto result = std::move(poll.value());
  ASSERT_TRUE(result.ok());
}

TEST_F(RegionalAccessBoundaryFetcherTest, CacheHitDoesNotTriggerFetch) {
  {
    MutexLockForGprMu lock(&creds_->regional_access_boundary_fetcher_->cache_mu_);
    creds_->regional_access_boundary_fetcher_->cache_ = RegionalAccessBoundary{
        "us-west1", {"us-west1"}, gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_seconds(100, GPR_TIMESPAN))};
  }

  auto promise = creds_->regional_access_boundary_fetcher_->Fetch(creds_, std::move(metadata_));
  EXPECT_FALSE(creds_->regional_access_boundary_fetcher_->fetch_in_flight_);

  Poll<absl::StatusOr<ClientMetadataHandle>> poll = promise();
  ASSERT_TRUE(poll.ready());
  auto result = std::move(poll.value());
  ASSERT_TRUE(result.ok());
  
  std::string buffer;
  std::optional<absl::string_view> value = (*result)->GetStringValue("x-allowed-locations", &buffer);
  EXPECT_TRUE(value.has_value());
  if (value.has_value()) {
    EXPECT_EQ(*value, "us-west1");
  }
}

TEST_F(RegionalAccessBoundaryFetcherTest, RegionalEndpointIgnored) {
  metadata_->Set(HttpAuthorityMetadata(),
                 Slice::FromStaticString("rep.googleapis.com"));
  
  auto promise = creds_->regional_access_boundary_fetcher_->Fetch(creds_, std::move(metadata_));
  EXPECT_FALSE(creds_->regional_access_boundary_fetcher_->fetch_in_flight_);
  
  Poll<absl::StatusOr<ClientMetadataHandle>> poll = promise();
  ASSERT_TRUE(poll.ready());
}

TEST_F(RegionalAccessBoundaryFetcherTest, NonGoogleApisEndpointIgnored) {
  metadata_->Set(HttpAuthorityMetadata(),
                 Slice::FromStaticString("example.com"));

  auto promise = creds_->regional_access_boundary_fetcher_->Fetch(creds_, std::move(metadata_));
  EXPECT_FALSE(creds_->regional_access_boundary_fetcher_->fetch_in_flight_);

  Poll<absl::StatusOr<ClientMetadataHandle>> poll = promise();
  ASSERT_TRUE(poll.ready());
}

TEST_F(RegionalAccessBoundaryFetcherTest, ExpiredCacheTriggersFetch) {
  {
    MutexLockForGprMu lock(&creds_->regional_access_boundary_fetcher_->cache_mu_);
    creds_->regional_access_boundary_fetcher_->cache_ = RegionalAccessBoundary{
        "us-west1", {"us-west1"}, gpr_time_sub(gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_seconds(100, GPR_TIMESPAN))};
  }
  
  auto promise = creds_->regional_access_boundary_fetcher_->Fetch(creds_, std::move(metadata_));
  EXPECT_TRUE(creds_->regional_access_boundary_fetcher_->fetch_in_flight_);
  
  Poll<absl::StatusOr<ClientMetadataHandle>> poll = promise();
  ASSERT_TRUE(poll.ready());
}

TEST_F(RegionalAccessBoundaryFetcherTest, CooldownPreventsFetch) {
  {
    MutexLockForGprMu lock(&creds_->regional_access_boundary_fetcher_->cache_mu_);
    creds_->regional_access_boundary_fetcher_->cooldown_deadline_ = gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_seconds(100, GPR_TIMESPAN));
  }
  
  auto promise = creds_->regional_access_boundary_fetcher_->Fetch(creds_, std::move(metadata_));
  EXPECT_FALSE(creds_->regional_access_boundary_fetcher_->fetch_in_flight_);
  
  Poll<absl::StatusOr<ClientMetadataHandle>> poll = promise();
  ASSERT_TRUE(poll.ready());
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
