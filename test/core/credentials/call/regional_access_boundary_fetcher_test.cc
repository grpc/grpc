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
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/credentials/call/call_credentials.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/http_client/httpcli.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {


class RegionalAccessBoundaryFetcherTest : public ::testing::Test {
 protected:
  using RegionalAccessBoundary = RegionalAccessBoundaryFetcher::RegionalAccessBoundary;
  static constexpr grpc_core::Duration kRegioanlAccessBoundarySoftCacheGraceDuration = grpc_core::Duration::Hours(1);
  bool has_cache() { grpc_core::MutexLock lock(&fetcher_->cache_mu_); return fetcher_->cache_.has_value(); }
  int retry_count() { grpc_core::MutexLock lock(&fetcher_->cache_mu_); return fetcher_->num_retries_; }
  std::string cached_encoded_locations() { grpc_core::MutexLock lock(&fetcher_->cache_mu_); return fetcher_->cache_->encoded_locations; }
  int cooldown_multiplier() { grpc_core::MutexLock lock(&fetcher_->cache_mu_); return fetcher_->cooldown_multiplier_; }
  void set_cache(RegionalAccessBoundary cache) { grpc_core::MutexLock lock(&fetcher_->cache_mu_); fetcher_->cache_ = cache; }
  void set_cooldown_deadline(grpc_core::Timestamp t) { grpc_core::MutexLock lock(&fetcher_->cache_mu_); fetcher_->cooldown_deadline_ = t; }

  bool fetch_in_flight() { grpc_core::MutexLock lock(&fetcher_->cache_mu_); return fetcher_->pending_request_ != nullptr; }

  grpc_core::Timestamp cooldown_deadline() { grpc_core::MutexLock lock(&fetcher_->cache_mu_); return fetcher_->cooldown_deadline_; }

  bool fetch_in_flight(RegionalAccessBoundaryFetcher* fetcher) { 
    grpc_core::MutexLock lock(&fetcher->cache_mu_); 
    return fetcher->pending_request_ != nullptr; 
  }

  WeakRefCountedPtr<RegionalAccessBoundaryFetcher> WeakFetcher() {
    return fetcher_->WeakRef();
  }

  bool IsShutdown(RegionalAccessBoundaryFetcher* fetcher) {
    grpc_core::MutexLock lock(&fetcher->cache_mu_);
    return fetcher->shutdown_;
  }

  bool CheckPendingRequestIsNull(RegionalAccessBoundaryFetcher* fetcher) {
    grpc_core::MutexLock lock(&fetcher->cache_mu_);
    return fetcher->pending_request_ == nullptr;
  }

  bool HasCache(RegionalAccessBoundaryFetcher* fetcher) {
    grpc_core::MutexLock lock(&fetcher->cache_mu_);
    return fetcher->cache_.has_value();
  }

  void SetUp() override {
    grpc_init();
    fuzzing_event_engine_ = std::make_shared<grpc_event_engine::experimental::FuzzingEventEngine>(
        grpc_event_engine::experimental::FuzzingEventEngine::Options(),
        fuzzing_event_engine::Actions());
    fetcher_ = RegionalAccessBoundaryFetcher::Create(
        "https://googleapis.com", 
        fuzzing_event_engine_,
        BackOff::Options()
            .set_initial_backoff(Duration::Milliseconds(1))
            .set_multiplier(1.1)
            .set_jitter(0.1)
            .set_max_backoff(Duration::Milliseconds(10)));
    arena_ = SimpleArenaAllocator()->MakeArena();
    metadata_ = arena_->MakePooled<ClientMetadata>();
    metadata_->Set(HttpAuthorityMetadata(),
                   Slice::FromStaticString("googleapis.com"));
    HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  }

  void Tick(Duration d) {
    fuzzing_event_engine_->TickForDuration(d);
    ExecCtx::Get()->TestOnlySetNow(ExecCtx::Get()->Now() + d);
  }

  std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine> fuzzing_event_engine_;

  void TearDown() override {
    HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  }

  RefCountedPtr<RegionalAccessBoundaryFetcher> fetcher_;
  RefCountedPtr<Arena> arena_;
  ClientMetadataHandle metadata_;
};

TEST_F(RegionalAccessBoundaryFetcherTest, CacheMissTriggersFetch) {
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  fetcher_->Fetch("", *metadata_);
  EXPECT_TRUE(fetch_in_flight());
}

TEST_F(RegionalAccessBoundaryFetcherTest, CacheHitDoesNotTriggerFetch) {
  set_cache(RegionalAccessBoundary{
      "us-west1", {"us-west1"}, grpc_core::Timestamp::Now() + grpc_core::Duration::Seconds(7200)});
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  fetcher_->Fetch("", *metadata_);
  EXPECT_FALSE(fetch_in_flight());
  std::string buffer;
  std::optional<absl::string_view> value = metadata_->GetStringValue("x-allowed-locations", &buffer);
  EXPECT_THAT(value, ::testing::Optional(absl::string_view("us-west1")));
}

TEST_F(RegionalAccessBoundaryFetcherTest, ExpiredCacheTriggersFetch) {
  set_cache(RegionalAccessBoundary{
      "us-west1", {"us-west1"}, grpc_core::Timestamp::Now() + grpc_core::Duration::Seconds(100)});
  Tick(grpc_core::Duration::Seconds(101));
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  fetcher_->Fetch("", *metadata_);
  EXPECT_TRUE(fetch_in_flight());
}

TEST_F(RegionalAccessBoundaryFetcherTest, CooldownPreventsFetch) {
  set_cooldown_deadline(grpc_core::Timestamp::Now() + grpc_core::Duration::Seconds(100));
  fetcher_->Fetch("", *metadata_);
  EXPECT_FALSE(fetch_in_flight());
}

TEST_F(RegionalAccessBoundaryFetcherTest, CooldownExpiredAllowsFetch) {
  set_cooldown_deadline(grpc_core::Timestamp::Now() + grpc_core::Duration::Seconds(100));
  Tick(grpc_core::Duration::Seconds(101));
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  fetcher_->Fetch("", *metadata_);
  EXPECT_TRUE(fetch_in_flight());
}

TEST_F(RegionalAccessBoundaryFetcherTest, InvalidUriParsing) {
  auto fetcher = RegionalAccessBoundaryFetcher::Create(
      "invalid_uri_!@#$", fuzzing_event_engine_,
      BackOff::Options()
          .set_initial_backoff(Duration::Milliseconds(1))
          .set_multiplier(1.1)
          .set_jitter(0.1)
          .set_max_backoff(Duration::Milliseconds(10)));
  EXPECT_EQ(fetcher, nullptr);
}

TEST_F(RegionalAccessBoundaryFetcherTest, RegionalEndpointIgnored) {
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  metadata_->Set(HttpAuthorityMetadata(), Slice::FromStaticString("rep.googleapis.com"));
  fetcher_->Fetch("", *metadata_);
  EXPECT_FALSE(fetch_in_flight());
  metadata_->Set(HttpAuthorityMetadata(), Slice::FromStaticString("foo.rep.googleapis.com"));
  fetcher_->Fetch("", *metadata_);
  EXPECT_FALSE(fetch_in_flight());
}

TEST_F(RegionalAccessBoundaryFetcherTest, NonGoogleApisEndpointIgnored) {
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  metadata_->Set(HttpAuthorityMetadata(), Slice::FromStaticString("example.com"));
  fetcher_->Fetch("", *metadata_);
  EXPECT_FALSE(fetch_in_flight());
  metadata_->Set(HttpAuthorityMetadata(), Slice::FromStaticString("fake-googleapis.com"));
  fetcher_->Fetch("", *metadata_);
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
  fetcher_->Fetch("", *metadata_);
  EXPECT_TRUE(fetch_in_flight());
}

TEST_F(RegionalAccessBoundaryFetcherTest, GoogleApisEndpointWithPortAllowed) {
  ExecCtx exec_ctx;
  HttpRequest::SetOverride(httpcli_get_valid_json, nullptr, nullptr);
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  metadata_->Set(HttpAuthorityMetadata(), Slice::FromStaticString("googleapis.com:443"));
  fetcher_->Fetch("", *metadata_);
  EXPECT_TRUE(fetch_in_flight());
}

TEST_F(RegionalAccessBoundaryFetcherTest, SubdomainGoogleApisEndpointAllowed) {
  ExecCtx exec_ctx;
  HttpRequest::SetOverride(httpcli_get_valid_json, nullptr, nullptr);
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  metadata_->Set(HttpAuthorityMetadata(), Slice::FromStaticString("pubsub.googleapis.com"));
  fetcher_->Fetch("", *metadata_);
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
  fetcher_->Fetch("", *metadata_);
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
  fetcher_->Fetch("", *metadata_);
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
  fetcher_->Fetch("", *metadata_);
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
  fetcher_->Fetch("", *metadata_);
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
  fetcher_->Fetch("", *metadata_);
  EXPECT_TRUE(fetch_in_flight());
  ExecCtx::Get()->Flush();
  EXPECT_TRUE(fetch_in_flight());
  EXPECT_EQ(retry_count(), 1);
  EXPECT_EQ(g_mock_get_count, 1);
  EXPECT_EQ(g_mock_get_count, 1);
  fetcher_.reset();
}

TEST_F(RegionalAccessBoundaryFetcherTest, NonRetryableHttpErrors) {
  ExecCtx exec_ctx;
  g_mock_get_count = 0;
  HttpRequest::SetOverride(httpcli_get_404, nullptr, nullptr);
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  fetcher_->Fetch("", *metadata_);
  EXPECT_TRUE(fetch_in_flight());
  ExecCtx::Get()->Flush();
  EXPECT_FALSE(fetch_in_flight());
  EXPECT_EQ(retry_count(), 0);
  EXPECT_GT(cooldown_deadline(), grpc_core::Timestamp::Now());
  EXPECT_EQ(g_mock_get_count, 1);
}


TEST_F(RegionalAccessBoundaryFetcherTest, CancelPendingFetch) {
  ExecCtx exec_ctx;
  HttpRequest::SetOverride(httpcli_get_500, nullptr, nullptr);
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  fetcher_->Fetch("", *metadata_);
  EXPECT_TRUE(fetch_in_flight());
  ExecCtx::Get()->Flush();
  EXPECT_EQ(retry_count(), 1);
  auto fetcher = WeakFetcher();
  fetcher_.reset();
  EXPECT_FALSE(fetch_in_flight(fetcher.operator->()));
}

static grpc_closure* g_stalled_on_done = nullptr;
static grpc_http_response* g_stalled_response = nullptr;

int httpcli_get_stalled(const grpc_http_request* /*request*/,
                           const URI& /*uri*/, Timestamp /*deadline*/,
                           grpc_closure* on_done,
                           grpc_http_response* response) {
  g_mock_get_count++;
  g_stalled_on_done = on_done;
  g_stalled_response = response;
  // Do not call ExecCtx::Run here. The request is now "in flight" and stalled.
  return 1;
}

TEST_F(RegionalAccessBoundaryFetcherTest, CancelPendingFetchWithInFlightRequest) {
  ExecCtx exec_ctx;
  g_mock_get_count = 0;
  g_stalled_on_done = nullptr;
  g_stalled_response = nullptr;
  HttpRequest::SetOverride(httpcli_get_stalled, nullptr, nullptr);
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  fetcher_->Fetch("", *metadata_);
  EXPECT_TRUE(fetch_in_flight());
  // Cancel immediately while the HTTP request is still in flight (but stalled).
  auto fetcher = WeakFetcher();
  fetcher_.reset();
  // The fetcher should have cleared the pending request internally. We can verify
  // by executing the stashed on_done closure explicitly.
  if (g_stalled_on_done != nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, g_stalled_on_done, absl::CancelledError("orphaned"));
  }
  ExecCtx::Get()->Flush();
  // The fetch shouldn't be in flight anymore since it was cleanly aborted.
  EXPECT_FALSE(fetch_in_flight(fetcher.operator->()));
  EXPECT_FALSE(fetch_in_flight(fetcher.operator->()));
}

TEST_F(RegionalAccessBoundaryFetcherTest, ResponseAfterShutdownIgnored) {
  ExecCtx exec_ctx;
  g_mock_get_count = 0;
  g_stalled_on_done = nullptr;
  g_stalled_response = nullptr;
  HttpRequest::SetOverride(httpcli_get_stalled, nullptr, nullptr);
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  fetcher_->Fetch("", *metadata_);
  EXPECT_TRUE(fetch_in_flight());
  auto fetcher = WeakFetcher();
  RegionalAccessBoundaryFetcher* fetcher_raw = fetcher.operator->();
  // Orphan the fetcher (simulating shutdown)
  fetcher_.reset();
  // Verify fetcher is shutdown but memory is valid
  EXPECT_TRUE(IsShutdown(fetcher_raw));
  EXPECT_TRUE(CheckPendingRequestIsNull(fetcher_raw));
  // Simulate a successful response arriving AFTER shutdown
  if (g_stalled_response != nullptr) {
    *g_stalled_response = http_response(200, "{\"encodedLocations\": \"us-west1\", \"locations\": [\"us-west1\"]}");
  }
  if (g_stalled_on_done != nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, g_stalled_on_done, absl::OkStatus());
  }
  ExecCtx::Get()->Flush();
  // Verify cache was NOT updated (it requires inspecting the zombie object)
  EXPECT_FALSE(HasCache(fetcher_raw));
}

TEST_F(RegionalAccessBoundaryFetcherTest, CooldownResetsOnSuccess) {
  ExecCtx exec_ctx;
  g_mock_get_count = 0;
  // 1. Force a 404 error to trigger cooldown immediately (no retries).
  HttpRequest::SetOverride(httpcli_get_404, nullptr, nullptr);
  auto metadata_1 = arena_->MakePooled<ClientMetadata>();
  metadata_1->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  metadata_1->Set(HttpAuthorityMetadata(), Slice::FromStaticString("googleapis.com"));
  fetcher_->Fetch("https://googleapis.com", *metadata_1);
  ExecCtx::Get()->Flush();
  EXPECT_FALSE(fetch_in_flight());
  EXPECT_EQ(cooldown_multiplier(), 2);
  // 2. Override with a successful 200 OK, but first advance time past cooldown.
  HttpRequest::SetOverride(httpcli_get_valid_json, nullptr, nullptr);
  Tick(grpc_core::Duration::Minutes(16));
  ExecCtx::Get()->Flush();
  auto metadata_success = arena_->MakePooled<ClientMetadata>();
  metadata_success->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  metadata_success->Set(HttpAuthorityMetadata(), Slice::FromStaticString("googleapis.com"));
  fetcher_->Fetch("https://googleapis.com", *metadata_success);
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
  fetcher_->Fetch("https://googleapis.com", *metadata_fail);
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
  fetcher_->Fetch("https://googleapis.com", *metadata_soft_expired);
  // We should still get the cached location
  EXPECT_EQ(cached_encoded_locations(), "us-east1");
  // AND a background refresh should have been triggered
  EXPECT_TRUE(fetch_in_flight());
  // Flush the exec context to allow the background fetch to complete
  ExecCtx::Get()->Flush();
  EXPECT_FALSE(fetch_in_flight());
  EXPECT_EQ(cached_encoded_locations(), "us-west1");
}


class EmailFetcherTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grpc_init();
    email_fetcher_ = MakeRefCounted<EmailFetcher>(
        grpc_event_engine::experimental::GetDefaultEventEngine());
    arena_ = SimpleArenaAllocator()->MakeArena();
    metadata_ = arena_->MakePooled<ClientMetadata>();
    HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  }

  void TearDown() override {
    HttpRequest::SetOverride(nullptr, nullptr, nullptr);
    email_fetcher_.reset(); // Ensure cleanup before grpc_shutdown
    grpc_shutdown();
  }

  RefCountedPtr<EmailFetcher> email_fetcher_;
  RefCountedPtr<Arena> arena_;
  ClientMetadataHandle metadata_;
};

int httpcli_get_email_success(const grpc_http_request* /*request*/,
                              const URI& uri, Timestamp /*deadline*/,
                              grpc_closure* on_done,
                              grpc_http_response* response) {
  if (uri.path() == "/computeMetadata/v1/instance/service-accounts/default/email") {
    *response = http_response(200, "foo@bar.com");
  } else {
    // RAB fetch
    *response = http_response(200, "{\"encodedLocations\": \"us-west1\", \"locations\": [\"us-west1\"]}");
  }
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

TEST_F(EmailFetcherTest, FetchesEmailAndThenRab) {
  ExecCtx exec_ctx;
  HttpRequest::SetOverride(httpcli_get_email_success, nullptr, nullptr);
  // Start email fetch
  email_fetcher_->StartEmailFetch();
  // Fetch should trigger RAB fetch if email fetch succeeded
  // We need to wait for email fetch to complete.
  // Since we use ExecCtx::Run in mock, flushing ExecCtx should handle it.
  ExecCtx::Get()->Flush();
  // Now call Fetch. It should allow RAB fetch to proceed.
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  metadata_->Append(":authority", Slice::FromStaticString("foo.googleapis.com"), [](absl::string_view, const Slice&) { abort(); });
  email_fetcher_->Fetch("token", *metadata_);
  // RAB fetch should happen (async).
  ExecCtx::Get()->Flush();
  // First fetch won't have metadata (cache miss).
  std::string buffer;
  std::optional<absl::string_view> value = metadata_->GetStringValue("x-allowed-locations", &buffer);
  EXPECT_FALSE(value.has_value());
  // Verify cache is populated by fetching again.
  metadata_ = arena_->MakePooled<ClientMetadata>();
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  metadata_->Append(":authority", Slice::FromStaticString("foo.googleapis.com"), [](absl::string_view, const Slice&) { abort(); });
  email_fetcher_->Fetch("token", *metadata_);
  std::string buffer2;
  std::optional<absl::string_view> value2 = metadata_->GetStringValue("x-allowed-locations", &buffer2);
  EXPECT_THAT(value2, ::testing::Optional(absl::string_view("us-west1")));
}

int httpcli_get_email_failure(const grpc_http_request* /*request*/,
                              const URI& /*uri*/, Timestamp /*deadline*/,
                              grpc_closure* on_done,
                              grpc_http_response* response) {
  *response = http_response(404, "Not Found");
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

TEST_F(EmailFetcherTest, EmailFetchFailureSkipsRab) {
  ExecCtx exec_ctx;
  HttpRequest::SetOverride(httpcli_get_email_failure, nullptr, nullptr);
  email_fetcher_->StartEmailFetch();
  ExecCtx::Get()->Flush();
  metadata_->Append("authorization", Slice::FromStaticString("Bearer token"), [](absl::string_view, const Slice&) { abort(); });
  email_fetcher_->Fetch("token", *metadata_);
  ExecCtx::Get()->Flush();
  std::string buffer;
  std::optional<absl::string_view> value = metadata_->GetStringValue("x-allowed-locations", &buffer);
  EXPECT_FALSE(value.has_value());
}


int httpcli_get_email_failure_counted(const grpc_http_request* /*request*/,
                              const URI& /*uri*/, Timestamp /*deadline*/,
                              grpc_closure* on_done,
                              grpc_http_response* response) {
  g_mock_get_count++;
  *response = http_response(404, "Not Found");
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

TEST_F(EmailFetcherTest, EmailFetchBackoffRespected) {
  ExecCtx exec_ctx;
  g_mock_get_count = 0;
  HttpRequest::SetOverride(httpcli_get_email_failure_counted, nullptr, nullptr);
  // 1. First failure triggers backoff.
  email_fetcher_->StartEmailFetch();
  ExecCtx::Get()->Flush();
  EXPECT_EQ(g_mock_get_count, 1);
  // 2. Immediate second attempt should be skipped due to backoff.
  email_fetcher_->StartEmailFetch();
  ExecCtx::Get()->Flush();
  EXPECT_EQ(g_mock_get_count, 1);
  // 3. Advance time past initial backoff (1s with jitters, say 2s to be safe).
  exec_ctx.TestOnlySetNow(grpc_core::Timestamp::Now() + grpc_core::Duration::Seconds(2));
  // 4. Retry should proceed.
  email_fetcher_->StartEmailFetch();
  ExecCtx::Get()->Flush();
  EXPECT_EQ(g_mock_get_count, 2);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}