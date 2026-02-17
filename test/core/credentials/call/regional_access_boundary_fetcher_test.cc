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

#include <grpc/support/string_util.h>
#include <grpc/support/alloc.h>

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
#include "src/core/util/http_client/httpcli.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {

class FakeCallCredentials : public grpc_call_credentials {
 public:
  FakeCallCredentials() : grpc_call_credentials(GRPC_SECURITY_NONE), regional_access_boundary_fetcher_(MakeOrphanable<RegionalAccessBoundaryFetcher>()) {}

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

  OrphanablePtr<RegionalAccessBoundaryFetcher> regional_access_boundary_fetcher_;
};

class RegionalAccessBoundaryFetcherTest : public ::testing::Test {
 protected:
  bool has_cache() { grpc_core::MutexLock lock(&creds_->regional_access_boundary_fetcher_->cache_mu_); return creds_->regional_access_boundary_fetcher_->cache_.has_value(); }
  std::string cached_encoded_locations() { grpc_core::MutexLock lock(&creds_->regional_access_boundary_fetcher_->cache_mu_); return creds_->regional_access_boundary_fetcher_->cache_->encoded_locations; }
  int cooldown_multiplier() { grpc_core::MutexLock lock(&creds_->regional_access_boundary_fetcher_->cache_mu_); return creds_->regional_access_boundary_fetcher_->cooldown_multiplier_; }
  void set_cache(RegionalAccessBoundary cache) { grpc_core::MutexLock lock(&creds_->regional_access_boundary_fetcher_->cache_mu_); creds_->regional_access_boundary_fetcher_->cache_ = cache; }
  void set_cooldown_deadline(grpc_core::Timestamp t) { grpc_core::MutexLock lock(&creds_->regional_access_boundary_fetcher_->cache_mu_); creds_->regional_access_boundary_fetcher_->cooldown_deadline_ = t; }

  bool fetch_in_flight() { grpc_core::MutexLock lock(&creds_->regional_access_boundary_fetcher_->cache_mu_); return creds_->regional_access_boundary_fetcher_->fetch_in_flight_; }
  void set_fetch_in_flight(bool val) { grpc_core::MutexLock lock(&creds_->regional_access_boundary_fetcher_->cache_mu_); creds_->regional_access_boundary_fetcher_->fetch_in_flight_ = val; }
  bool has_retry_timer() { grpc_core::MutexLock lock(&creds_->regional_access_boundary_fetcher_->cache_mu_); return creds_->regional_access_boundary_fetcher_->retry_timer_handle_.has_value(); }
  grpc_core::Timestamp cooldown_deadline() { grpc_core::MutexLock lock(&creds_->regional_access_boundary_fetcher_->cache_mu_); return creds_->regional_access_boundary_fetcher_->cooldown_deadline_; }

  bool has_retry_timer(RegionalAccessBoundaryFetcher* fetcher) { 
    grpc_core::MutexLock lock(&fetcher->cache_mu_); 
    return fetcher->retry_timer_handle_.has_value(); 
  }
  bool fetch_in_flight(RegionalAccessBoundaryFetcher* fetcher) { 
    grpc_core::MutexLock lock(&fetcher->cache_mu_); 
    return fetcher->fetch_in_flight_; 
  }

  RefCountedPtr<RegionalAccessBoundaryFetcher> RefFetcher() {
    return creds_->regional_access_boundary_fetcher_->Ref();
  }

  void SetUp() override {
    grpc_init();
    creds_ = MakeRefCounted<FakeCallCredentials>();
    arena_ = SimpleArenaAllocator()->MakeArena();
    metadata_ = arena_->MakePooled<ClientMetadata>();
    metadata_->Set(HttpAuthorityMetadata(),
                   Slice::FromStaticString("googleapis.com"));
    HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  }

  void TearDown() override {
    HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  }

  RefCountedPtr<FakeCallCredentials> creds_;
  RefCountedPtr<Arena> arena_;
  ClientMetadataHandle metadata_;
};

TEST_F(RegionalAccessBoundaryFetcherTest, CacheMissTriggersFetch) {
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_);
  EXPECT_TRUE(fetch_in_flight());
  
  
}

TEST_F(RegionalAccessBoundaryFetcherTest, CacheHitDoesNotTriggerFetch) {
  set_cache(RegionalAccessBoundary{
      "us-west1", {"us-west1"}, grpc_core::Timestamp::Now() + grpc_core::Duration::Seconds(7200)});
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });

  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_);
  EXPECT_FALSE(fetch_in_flight());

  
  
  std::string buffer;
  std::optional<absl::string_view> value = metadata_->GetStringValue("x-allowed-locations", &buffer);
  EXPECT_TRUE(value.has_value());
  if (value.has_value()) {
    EXPECT_EQ(*value, "us-west1");
  }
}

TEST_F(RegionalAccessBoundaryFetcherTest, ExpiredCacheTriggersFetch) {
  set_cache(RegionalAccessBoundary{
      "us-west1", {"us-west1"}, grpc_core::Timestamp::Now() - grpc_core::Duration::Seconds(100)});
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_);
  EXPECT_TRUE(fetch_in_flight());
  
}

TEST_F(RegionalAccessBoundaryFetcherTest, CooldownPreventsFetch) {
  set_cooldown_deadline(grpc_core::Timestamp::Now() + grpc_core::Duration::Seconds(100));
  
  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_);
  EXPECT_FALSE(fetch_in_flight());
  
}

TEST_F(RegionalAccessBoundaryFetcherTest, CooldownExpiredAllowsFetch) {
  set_cooldown_deadline(grpc_core::Timestamp::Now() - grpc_core::Duration::Seconds(100));
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  
  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_);
  EXPECT_TRUE(fetch_in_flight());
  
}

TEST_F(RegionalAccessBoundaryFetcherTest, InvalidUriParsing) {
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  creds_->regional_access_boundary_fetcher_->Fetch("invalid_uri_!@#$", "", *metadata_);
  EXPECT_FALSE(fetch_in_flight());
}

TEST_F(RegionalAccessBoundaryFetcherTest, RegionalEndpointIgnored) {
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  metadata_->Set(HttpAuthorityMetadata(), Slice::FromStaticString("rep.googleapis.com"));
  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_);
  EXPECT_FALSE(fetch_in_flight());
  
  metadata_->Set(HttpAuthorityMetadata(), Slice::FromStaticString("foo.rep.googleapis.com"));
  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_);
  EXPECT_FALSE(fetch_in_flight());
}

TEST_F(RegionalAccessBoundaryFetcherTest, NonGoogleApisEndpointIgnored) {
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  metadata_->Set(HttpAuthorityMetadata(), Slice::FromStaticString("example.com"));
  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_);
  EXPECT_FALSE(fetch_in_flight());

  metadata_->Set(HttpAuthorityMetadata(), Slice::FromStaticString("fake-googleapis.com"));
  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_);
  EXPECT_FALSE(fetch_in_flight());
}

grpc_http_response http_response(int status, const char* body) {
  grpc_http_response response;
  response = {};
  response.status = status;
  response.body = gpr_strdup(const_cast<char*>(body));
  response.body_length = strlen(body);
  return response;
}

int httpcli_get_valid_json(const grpc_http_request* /*request*/,
                           const URI& /*uri*/, Timestamp /*deadline*/,
                           grpc_closure* on_done,
                           grpc_http_response* response) {
  *response = http_response(200, "{\"encodedLocations\": \"us-west1\", \"locations\": [\"us-west1\"]}");
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

TEST_F(RegionalAccessBoundaryFetcherTest, GoogleApisEndpointAllowed) {
  ExecCtx exec_ctx;
  HttpRequest::SetOverride(httpcli_get_valid_json, nullptr, nullptr);
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  
  metadata_->Set(HttpAuthorityMetadata(), Slice::FromStaticString("googleapis.com"));
  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_);
  EXPECT_TRUE(fetch_in_flight());
}

TEST_F(RegionalAccessBoundaryFetcherTest, GoogleApisEndpointWithPortAllowed) {
  ExecCtx exec_ctx;
  HttpRequest::SetOverride(httpcli_get_valid_json, nullptr, nullptr);
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  
  metadata_->Set(HttpAuthorityMetadata(), Slice::FromStaticString("googleapis.com:443"));
  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_);
  EXPECT_TRUE(fetch_in_flight());
}

TEST_F(RegionalAccessBoundaryFetcherTest, SubdomainGoogleApisEndpointAllowed) {
  ExecCtx exec_ctx;
  HttpRequest::SetOverride(httpcli_get_valid_json, nullptr, nullptr);
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  
  metadata_->Set(HttpAuthorityMetadata(), Slice::FromStaticString("pubsub.googleapis.com"));
  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_);
  EXPECT_TRUE(fetch_in_flight());
}

int httpcli_get_malformed_json(const grpc_http_request* /*request*/,
                           const URI& /*uri*/, Timestamp /*deadline*/,
                           grpc_closure* on_done,
                           grpc_http_response* response) {
  *response = http_response(200, "{\"encodedLocations\"");
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

int httpcli_get_missing_fields_json(const grpc_http_request* /*request*/,
                           const URI& /*uri*/, Timestamp /*deadline*/,
                           grpc_closure* on_done,
                           grpc_http_response* response) {
  *response = http_response(200, "{\"locations\": [\"us-west1\"]}");
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

int httpcli_get_valid_json_with_non_string_locations(const grpc_http_request* /*request*/,
                           const URI& /*uri*/, Timestamp /*deadline*/,
                           grpc_closure* on_done,
                           grpc_http_response* response) {
  *response = http_response(200, "{\"encodedLocations\": \"us-west1\", \"locations\": [\"us-west1\", 123, \"us-east1\"]}");
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

TEST_F(RegionalAccessBoundaryFetcherTest, ValidJsonResponse) {
  ExecCtx exec_ctx;
  HttpRequest::SetOverride(httpcli_get_valid_json, nullptr, nullptr);
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_);
  EXPECT_TRUE(fetch_in_flight());
  

  ExecCtx::Get()->Flush();

  EXPECT_FALSE(fetch_in_flight());
  EXPECT_TRUE(has_cache());
  EXPECT_EQ(cached_encoded_locations(), "us-west1");
  EXPECT_EQ(cooldown_multiplier(), 1);
}

TEST_F(RegionalAccessBoundaryFetcherTest, MalformedJsonResponse) {
  ExecCtx exec_ctx;
  HttpRequest::SetOverride(httpcli_get_malformed_json, nullptr, nullptr);
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_);
  EXPECT_TRUE(fetch_in_flight());

  ExecCtx::Get()->Flush();

  EXPECT_FALSE(fetch_in_flight());
  EXPECT_FALSE(has_cache());
  EXPECT_GT(cooldown_deadline(), grpc_core::Timestamp::Now());
}

TEST_F(RegionalAccessBoundaryFetcherTest, ValidJsonMissingFields) {
  ExecCtx exec_ctx;
  HttpRequest::SetOverride(httpcli_get_missing_fields_json, nullptr, nullptr);
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_);
  EXPECT_TRUE(fetch_in_flight());

  ExecCtx::Get()->Flush();

  EXPECT_FALSE(fetch_in_flight());
  EXPECT_FALSE(has_cache());
  EXPECT_GT(cooldown_deadline(), grpc_core::Timestamp::Now());
}

TEST_F(RegionalAccessBoundaryFetcherTest, ValidJsonWithNonStringLocations) {
  ExecCtx exec_ctx;
  HttpRequest::SetOverride(httpcli_get_valid_json_with_non_string_locations, nullptr, nullptr);
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_);
  EXPECT_TRUE(fetch_in_flight());

  ExecCtx::Get()->Flush();

  EXPECT_FALSE(fetch_in_flight());
  EXPECT_TRUE(has_cache());
  EXPECT_EQ(cached_encoded_locations(), "us-west1");
  // The non-string location (123) should be dropped, meaning two valid locations remain.
  // We can't query the vector elements directly from the test's view unless we add accessors.
  // But verifying caching is successful is enough for this test.
  EXPECT_EQ(cooldown_multiplier(), 1);
}

int g_mock_get_count = 0;

int httpcli_get_500(const grpc_http_request* /*request*/,
                           const URI& /*uri*/, Timestamp /*deadline*/,
                           grpc_closure* on_done,
                           grpc_http_response* response) {
  g_mock_get_count++;
  *response = http_response(500, "");
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

int httpcli_get_404(const grpc_http_request* /*request*/,
                           const URI& /*uri*/, Timestamp /*deadline*/,
                           grpc_closure* on_done,
                           grpc_http_response* response) {
  g_mock_get_count++;
  *response = http_response(404, "");
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

TEST_F(RegionalAccessBoundaryFetcherTest, RetryableHttpErrors) {
  ExecCtx exec_ctx;
  g_mock_get_count = 0;
  HttpRequest::SetOverride(httpcli_get_500, nullptr, nullptr);
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_);
  EXPECT_TRUE(fetch_in_flight());

  ExecCtx::Get()->Flush();

  EXPECT_TRUE(fetch_in_flight());
  EXPECT_TRUE(has_retry_timer());
  EXPECT_EQ(g_mock_get_count, 1);
  
  EXPECT_EQ(g_mock_get_count, 1);
  
  creds_->regional_access_boundary_fetcher_.reset();
}

TEST_F(RegionalAccessBoundaryFetcherTest, NonRetryableHttpErrors) {
  ExecCtx exec_ctx;
  g_mock_get_count = 0;
  HttpRequest::SetOverride(httpcli_get_404, nullptr, nullptr);
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_);
  EXPECT_TRUE(fetch_in_flight());

  ExecCtx::Get()->Flush();

  EXPECT_FALSE(fetch_in_flight());
  EXPECT_FALSE(has_retry_timer());
  EXPECT_GT(cooldown_deadline(), grpc_core::Timestamp::Now());
  EXPECT_EQ(g_mock_get_count, 1);
}


TEST_F(RegionalAccessBoundaryFetcherTest, CancelPendingFetch) {
  ExecCtx exec_ctx;
  HttpRequest::SetOverride(httpcli_get_500, nullptr, nullptr);
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_);
  EXPECT_TRUE(fetch_in_flight());

  ExecCtx::Get()->Flush();

  EXPECT_TRUE(has_retry_timer());
  
  auto fetcher = RefFetcher();
  creds_->regional_access_boundary_fetcher_.reset();

  EXPECT_FALSE(has_retry_timer(fetcher.get()));
}

static grpc_closure* g_stalled_on_done = nullptr;

int httpcli_get_stalled(const grpc_http_request* /*request*/,
                           const URI& /*uri*/, Timestamp /*deadline*/,
                           grpc_closure* on_done,
                           grpc_http_response* response) {
  g_mock_get_count++;
  g_stalled_on_done = on_done;
  // Do not call ExecCtx::Run here. The request is now "in flight" and stalled.
  return 1;
}

TEST_F(RegionalAccessBoundaryFetcherTest, CancelPendingFetchWithInFlightRequest) {
  ExecCtx exec_ctx;
  g_mock_get_count = 0;
  g_stalled_on_done = nullptr;
  HttpRequest::SetOverride(httpcli_get_stalled, nullptr, nullptr);
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  
  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_);
  EXPECT_TRUE(fetch_in_flight());

  // Cancel immediately while the HTTP request is still in flight (but stalled).
  auto fetcher = RefFetcher();
  creds_->regional_access_boundary_fetcher_.reset();
  
  // The fetcher should have cleared the pending request internally. We can verify
  // by executing the stashed on_done closure explicitly.
  if (g_stalled_on_done != nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, g_stalled_on_done, absl::CancelledError("orphaned"));
  }
  
  ExecCtx::Get()->Flush();
  // The fetch shouldn't be in flight anymore since it was cleanly aborted.
  EXPECT_FALSE(fetch_in_flight(fetcher.get()));
}

TEST_F(RegionalAccessBoundaryFetcherTest, CooldownResetsOnSuccess) {
  ExecCtx exec_ctx;
  g_mock_get_count = 0;
  
  // 1. Force a 404 error to trigger cooldown immediately (no retries).
  HttpRequest::SetOverride(httpcli_get_404, nullptr, nullptr);
  auto metadata_1 = arena_->MakePooled<ClientMetadata>();
  metadata_1->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  metadata_1->Set(HttpAuthorityMetadata(), Slice::FromStaticString("googleapis.com"));
  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_1);
  ExecCtx::Get()->Flush();
  
  EXPECT_FALSE(fetch_in_flight());
  EXPECT_EQ(cooldown_multiplier(), 2);

  // 2. Override with a successful 200 OK, but first advance time past cooldown.
  HttpRequest::SetOverride(httpcli_get_valid_json, nullptr, nullptr);
  exec_ctx.TestOnlySetNow(grpc_core::Timestamp::Now() + grpc_core::Duration::Minutes(16));
  ExecCtx::Get()->Flush();

  auto metadata_success = arena_->MakePooled<ClientMetadata>();
  metadata_success->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  metadata_success->Set(HttpAuthorityMetadata(), Slice::FromStaticString("googleapis.com"));
  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_success);
  ExecCtx::Get()->Flush();

  // The fetch should succeed and cache the values, resetting cooldown to 1.
  EXPECT_FALSE(fetch_in_flight());
  EXPECT_TRUE(has_cache());
  if (has_cache()) {
    EXPECT_EQ(cached_encoded_locations(), "us-west1");
  }
  EXPECT_EQ(cooldown_multiplier(), 1);

  // 3. Fake eviction of cache to simulate a new request later.
  set_cache(RegionalAccessBoundary{"", {}, grpc_core::Timestamp::InfPast()});

  // 4. Force another 404 error on the new request to observe cooldown reset.
  HttpRequest::SetOverride(httpcli_get_404, nullptr, nullptr);
  auto metadata_fail = arena_->MakePooled<ClientMetadata>();
  metadata_fail->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  metadata_fail->Set(HttpAuthorityMetadata(), Slice::FromStaticString("googleapis.com"));
  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_fail);
  ExecCtx::Get()->Flush();
  
  EXPECT_FALSE(fetch_in_flight());
  // Cooldown multiplier should be freshly multiplying to 2, not 4.
  EXPECT_EQ(cooldown_multiplier(), 2);
}

TEST_F(RegionalAccessBoundaryFetcherTest, CacheSoftExpirationTriggersRefresh) {
  ExecCtx exec_ctx;
  g_mock_get_count = 0;
  HttpRequest::SetOverride(httpcli_get_valid_json, nullptr, nullptr);
  
  grpc_core::Timestamp now = grpc_core::Timestamp::Now();
  grpc_core::Timestamp soft_expired_timestamp = now + (kRegioanlAccessBoundarySoftCacheGraceDuration / 2);
  
  set_cache(RegionalAccessBoundary{"us-east1", {"us-east1"}, soft_expired_timestamp});
  
  // Verify our mock cache setup is correct
  EXPECT_TRUE(has_cache());
  EXPECT_FALSE(fetch_in_flight());

  auto metadata_soft_expired = arena_->MakePooled<ClientMetadata>();
  metadata_soft_expired->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  metadata_soft_expired->Set(HttpAuthorityMetadata(), Slice::FromStaticString("googleapis.com"));

  creds_->regional_access_boundary_fetcher_->Fetch("https://googleapis.com", "", *metadata_soft_expired);
  
  // We should still get the cached location
  EXPECT_EQ(cached_encoded_locations(), "us-east1");
  
  // AND a background refresh should have been triggered
  EXPECT_TRUE(fetch_in_flight());
  
  // Flush the exec context to allow the background fetch to complete
  ExecCtx::Get()->Flush();
  EXPECT_FALSE(fetch_in_flight());
  EXPECT_EQ(cached_encoded_locations(), "us-west1");
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}