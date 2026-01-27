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

#include "src/core/credentials/call/regional_access_boundary_util.h"

#include <grpc/grpc.h>
#include <grpc/support/string_util.h>
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
#include "src/core/util/http_client/httpcli.h"
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

  std::string custom_url_ =
      "https://localhost:1234/v1/projects/-/serviceAccounts/test/"
      "allowedLocations";

  std::string build_regional_access_boundary_url() override {
    return custom_url_;
  }

  int cmp_impl(const grpc_call_credentials*) const override { return 0; }
};

struct MockHttpResponse {
  int status = 200;
  std::string body;
};

// Global pointer to control mock response
MockHttpResponse* g_mock_response = nullptr;
int g_mock_get_count = 0;

int MockGet(const grpc_http_request* /*request*/, const grpc_core::URI& /*uri*/,
            grpc_core::Timestamp /*deadline*/, grpc_closure* on_complete,
            grpc_http_response* response) {
  g_mock_get_count++;
  if (g_mock_response) {
    response->status = g_mock_response->status;
    response->body = gpr_strdup(g_mock_response->body.c_str());
    response->body_length = g_mock_response->body.length();
    response->hdr_count = 0;
    response->hdrs = nullptr;
  } else {
    // Default fallback if g_mock_response is null (shouldn't happen in our
    // tests)
    response->status = 404;
  }
  // Schedule the callback on the ExecCtx
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_complete, absl::OkStatus());
  return 1;
}

class RegionalAccessBoundaryUtilTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grpc_init();
    HttpRequest::SetOverride(MockGet, nullptr, nullptr);
    SetEnv("GOOGLE_AUTH_REGIONAL_ACCESS_BOUNDARY_ENABLED", "true");
    creds_ = MakeRefCounted<FakeCallCredentials>();
    arena_ = SimpleArenaAllocator()->MakeArena();
    metadata_ = arena_->MakePooled<ClientMetadata>();
    metadata_->Set(HttpAuthorityMetadata(),
                   Slice::FromStaticString("example.com"));
    metadata_->Append(GRPC_AUTHORIZATION_METADATA_KEY,
                      Slice::FromStaticString("Bearer token"),
                      [](absl::string_view, const Slice&) { abort(); });
  }

  void TearDown() override {
    HttpRequest::SetOverride(nullptr, nullptr, nullptr);
    g_mock_response = nullptr;
    g_mock_get_count = 0;
    creds_.reset();
    metadata_.reset();
    arena_.reset();
    UnsetEnv("GOOGLE_AUTH_REGIONAL_ACCESS_BOUNDARY_ENABLED");
    grpc_shutdown();
  }

  void WaitForFetchToComplete() { grpc_core::ExecCtx::Get()->Flush(); }

  RefCountedPtr<FakeCallCredentials> creds_;
  RefCountedPtr<Arena> arena_;
  ClientMetadataHandle metadata_;
};

TEST_F(RegionalAccessBoundaryUtilTest, DisabledViaEnvVar) {
  grpc_core::ExecCtx exec_ctx;
  SetEnv("GOOGLE_AUTH_REGIONAL_ACCESS_BOUNDARY_ENABLED", "false");

  auto promise = FetchRegionalAccessBoundary(creds_, std::move(metadata_));
  EXPECT_FALSE(creds_->regional_access_boundary_fetch_in_flight);

  Poll<absl::StatusOr<ClientMetadataHandle>> poll = promise();
  ASSERT_TRUE(poll.ready());
  auto result = std::move(poll.value());
  ASSERT_TRUE(result.ok());
}

TEST_F(RegionalAccessBoundaryUtilTest, CacheMissTriggersFetch) {
  grpc_core::ExecCtx exec_ctx;
  auto promise = FetchRegionalAccessBoundary(creds_, std::move(metadata_));

  Poll<absl::StatusOr<ClientMetadataHandle>> poll = promise();
  ASSERT_TRUE(poll.ready());
  auto result = std::move(poll.value());
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(creds_->regional_access_boundary_fetch_in_flight);
}

TEST_F(RegionalAccessBoundaryUtilTest, CacheHitDoesNotTriggerFetch) {
  grpc_core::ExecCtx exec_ctx;
  {
    MutexLockForGprMu lock(&creds_->regional_access_boundary_cache_mu);
    creds_->regional_access_boundary_cache = RegionalAccessBoundary{
        "us-west1",
        {"us-west1"},
        gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                     gpr_time_from_seconds(100, GPR_TIMESPAN))};
  }

  auto promise = FetchRegionalAccessBoundary(creds_, std::move(metadata_));
  EXPECT_FALSE(creds_->regional_access_boundary_fetch_in_flight);
  EXPECT_EQ(g_mock_get_count, 0);

  Poll<absl::StatusOr<ClientMetadataHandle>> poll = promise();
  ASSERT_TRUE(poll.ready());
  auto result = std::move(poll.value());
  ASSERT_TRUE(result.ok());

  std::string buffer;
  std::optional<absl::string_view> value =
      (*result)->GetStringValue("x-allowed-locations", &buffer);
  EXPECT_TRUE(value.has_value());
  if (value.has_value()) {
    EXPECT_EQ(*value, "us-west1");
  }
}

TEST_F(RegionalAccessBoundaryUtilTest, RegionalEndpointIgnored) {
  grpc_core::ExecCtx exec_ctx;
  metadata_->Set(HttpAuthorityMetadata(),
                 Slice::FromStaticString("rep.googleapis.com"));

  auto promise = FetchRegionalAccessBoundary(creds_, std::move(metadata_));
  EXPECT_FALSE(creds_->regional_access_boundary_fetch_in_flight);

  Poll<absl::StatusOr<ClientMetadataHandle>> poll = promise();
  ASSERT_TRUE(poll.ready());
}

TEST_F(RegionalAccessBoundaryUtilTest, ExpiredCacheTriggersFetch) {
  grpc_core::ExecCtx exec_ctx;
  {
    MutexLockForGprMu lock(&creds_->regional_access_boundary_cache_mu);
    creds_->regional_access_boundary_cache = RegionalAccessBoundary{
        "us-west1",
        {"us-west1"},
        gpr_time_sub(gpr_now(GPR_CLOCK_REALTIME),
                     gpr_time_from_seconds(100, GPR_TIMESPAN))};
  }

  auto promise = FetchRegionalAccessBoundary(creds_, std::move(metadata_));
  EXPECT_TRUE(creds_->regional_access_boundary_fetch_in_flight);

  Poll<absl::StatusOr<ClientMetadataHandle>> poll = promise();
  ASSERT_TRUE(poll.ready());
}

TEST_F(RegionalAccessBoundaryUtilTest, CooldownPreventsFetch) {
  grpc_core::ExecCtx exec_ctx;
  {
    MutexLockForGprMu lock(&creds_->regional_access_boundary_cache_mu);
    creds_->regional_access_boundary_cooldown_deadline = gpr_time_add(
        gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_seconds(100, GPR_TIMESPAN));
  }

  auto promise = FetchRegionalAccessBoundary(creds_, std::move(metadata_));
  EXPECT_FALSE(creds_->regional_access_boundary_fetch_in_flight);

  Poll<absl::StatusOr<ClientMetadataHandle>> poll = promise();
  ASSERT_TRUE(poll.ready());
}

TEST_F(RegionalAccessBoundaryUtilTest, FetchSuccess) {
  grpc_core::ExecCtx exec_ctx;
  MockHttpResponse response = {200,
                               "{\"encodedLocations\": \"abcd\", "
                               "\"locations\": [\"us-west1\"]}"};
  g_mock_response = &response;

  auto promise = FetchRegionalAccessBoundary(creds_, std::move(metadata_));

  // Fetch should be in flight initially
  EXPECT_TRUE(creds_->regional_access_boundary_fetch_in_flight);

  WaitForFetchToComplete();

  EXPECT_FALSE(creds_->regional_access_boundary_fetch_in_flight);

  // Verify cache update
  MutexLockForGprMu lock(&creds_->regional_access_boundary_cache_mu);
  ASSERT_TRUE(creds_->regional_access_boundary_cache.has_value());
  EXPECT_EQ(creds_->regional_access_boundary_cache->encoded_locations, "abcd");
  ASSERT_EQ(creds_->regional_access_boundary_cache->locations.size(), 1);
  EXPECT_EQ(creds_->regional_access_boundary_cache->locations[0], "us-west1");
  EXPECT_EQ(creds_->regional_access_boundary_cooldown_multiplier, 1);
}

TEST_F(RegionalAccessBoundaryUtilTest, FetchFailure401SetsCooldown) {
  grpc_core::ExecCtx exec_ctx;

  MockHttpResponse response = {401, "Unauthorized"};

  g_mock_response = &response;

  auto promise = FetchRegionalAccessBoundary(creds_, std::move(metadata_));

  WaitForFetchToComplete();

  EXPECT_FALSE(creds_->regional_access_boundary_fetch_in_flight);

  // Verify cache update for 401

  MutexLockForGprMu lock(&creds_->regional_access_boundary_cache_mu);

  EXPECT_FALSE(creds_->regional_access_boundary_cache.has_value());

  EXPECT_GT(gpr_time_cmp(creds_->regional_access_boundary_cooldown_deadline,
                         gpr_now(GPR_CLOCK_REALTIME)),
            0);

  EXPECT_GT(creds_->regional_access_boundary_cooldown_multiplier, 1);
}

TEST_F(RegionalAccessBoundaryUtilTest, FetchFailure404Retries) {
  grpc_core::ExecCtx exec_ctx;

  MockHttpResponse response = {404, "Not Found"};

  g_mock_response = &response;

  auto promise = FetchRegionalAccessBoundary(creds_, std::move(metadata_));

  // 404 should retry, so fetch should still be in flight

  EXPECT_TRUE(creds_->regional_access_boundary_fetch_in_flight);
}

TEST_F(RegionalAccessBoundaryUtilTest, CooldownRespectedAfterFailure) {
  grpc_core::ExecCtx exec_ctx;
  MockHttpResponse response = {401, "Unauthorized"};
  g_mock_response = &response;

  // First fetch fails and sets cooldown
  auto promise1 = FetchRegionalAccessBoundary(creds_, std::move(metadata_));
  WaitForFetchToComplete();
  EXPECT_FALSE(creds_->regional_access_boundary_fetch_in_flight);
  EXPECT_EQ(g_mock_get_count, 1);

  {
    MutexLockForGprMu lock(&creds_->regional_access_boundary_cache_mu);
    EXPECT_GT(gpr_time_cmp(creds_->regional_access_boundary_cooldown_deadline,
                           gpr_now(GPR_CLOCK_REALTIME)),
              0);
  }

  // Second fetch should be blocked by cooldown
  // Need new metadata since previous one was moved
  auto metadata2 = arena_->MakePooled<ClientMetadata>();
  metadata2->Set(HttpAuthorityMetadata(),
                 Slice::FromStaticString("example.com"));
  metadata2->Append(GRPC_AUTHORIZATION_METADATA_KEY,
                    Slice::FromStaticString("Bearer token"),
                    [](absl::string_view, const Slice&) { abort(); });

  auto promise2 = FetchRegionalAccessBoundary(creds_, std::move(metadata2));

  // Verify no new fetch started
  EXPECT_FALSE(creds_->regional_access_boundary_fetch_in_flight);
  EXPECT_EQ(g_mock_get_count, 1);  // Count should NOT increase
}

TEST_F(RegionalAccessBoundaryUtilTest, InFlightFetchPreventsNewFetch) {
  grpc_core::ExecCtx exec_ctx;
  MockHttpResponse response = {200, "{}"};
  g_mock_response = &response;

  // First fetch starts
  auto promise1 = FetchRegionalAccessBoundary(creds_, std::move(metadata_));
  EXPECT_TRUE(creds_->regional_access_boundary_fetch_in_flight);
  EXPECT_EQ(g_mock_get_count, 1);

  // Second fetch should join the existing flight
  auto metadata2 = arena_->MakePooled<ClientMetadata>();
  metadata2->Set(HttpAuthorityMetadata(),
                 Slice::FromStaticString("example.com"));
  metadata2->Append(GRPC_AUTHORIZATION_METADATA_KEY,
                    Slice::FromStaticString("Bearer token"),
                    [](absl::string_view, const Slice&) { abort(); });

  auto promise2 = FetchRegionalAccessBoundary(creds_, std::move(metadata2));
  
  // Verify no new fetch started
  EXPECT_TRUE(creds_->regional_access_boundary_fetch_in_flight);
  EXPECT_EQ(g_mock_get_count, 1);
}

TEST_F(RegionalAccessBoundaryUtilTest, InvalidUrlIgnored) {
  grpc_core::ExecCtx exec_ctx;
  creds_->custom_url_ = ":invalid_scheme";

  auto promise = FetchRegionalAccessBoundary(creds_, std::move(metadata_));

  EXPECT_FALSE(creds_->regional_access_boundary_fetch_in_flight);
  EXPECT_EQ(g_mock_get_count, 0);

  Poll<absl::StatusOr<ClientMetadataHandle>> poll = promise();
  ASSERT_TRUE(poll.ready());
  auto result = std::move(poll.value());
  ASSERT_TRUE(result.ok());
}

TEST_F(RegionalAccessBoundaryUtilTest, MissingAccessToken) {
  grpc_core::ExecCtx exec_ctx;
  
  auto metadata_no_auth = arena_->MakePooled<ClientMetadata>();
  metadata_no_auth->Set(HttpAuthorityMetadata(), Slice::FromStaticString("example.com"));

  auto promise = FetchRegionalAccessBoundary(creds_, std::move(metadata_no_auth));

  EXPECT_FALSE(creds_->regional_access_boundary_fetch_in_flight);
  EXPECT_EQ(g_mock_get_count, 0);

  Poll<absl::StatusOr<ClientMetadataHandle>> poll = promise();
  ASSERT_TRUE(poll.ready());
  auto result = std::move(poll.value());
  ASSERT_TRUE(result.ok());
}

TEST_F(RegionalAccessBoundaryUtilTest, FetchWithOnlyLocationsDoesNotCache) {
  grpc_core::ExecCtx exec_ctx;
  MockHttpResponse response = {200, "{\"locations\": [\"us-west1\"]}"};
  g_mock_response = &response;

  auto promise = FetchRegionalAccessBoundary(creds_, std::move(metadata_));
  WaitForFetchToComplete();

  MutexLockForGprMu lock(&creds_->regional_access_boundary_cache_mu);
  EXPECT_FALSE(creds_->regional_access_boundary_cache.has_value());
  EXPECT_GT(gpr_time_cmp(creds_->regional_access_boundary_cooldown_deadline,
                         gpr_now(GPR_CLOCK_REALTIME)),
            0);
}

TEST_F(RegionalAccessBoundaryUtilTest, MalformedResponse) {
  grpc_core::ExecCtx exec_ctx;
  MockHttpResponse response = {200, "Not JSON"};
  g_mock_response = &response;

  auto promise = FetchRegionalAccessBoundary(creds_, std::move(metadata_));
  WaitForFetchToComplete();

  MutexLockForGprMu lock(&creds_->regional_access_boundary_cache_mu);
  EXPECT_FALSE(creds_->regional_access_boundary_cache.has_value());
  EXPECT_GT(gpr_time_cmp(creds_->regional_access_boundary_cooldown_deadline,
                         gpr_now(GPR_CLOCK_REALTIME)),
            0);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}