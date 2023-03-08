//
//
// Copyright 2018 gRPC authors.
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

#include "src/core/tsi/ssl/session_cache/ssl_session_cache.h"

#include <string>
#include <unordered_set>

#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/crash.h"
#include "test/core/util/test_config.h"

namespace grpc_core {

namespace {

class SessionTracker;

struct SessionExDataId {
  SessionTracker* tracker;
  long id;
};

class SessionTracker {
 public:
  SessionTracker() { ssl_context_ = SSL_CTX_new(TLSv1_2_method()); }

  ~SessionTracker() { SSL_CTX_free(ssl_context_); }

  tsi::SslSessionPtr NewSession(long id) {
    static int ex_data_id = SSL_SESSION_get_ex_new_index(
        0, nullptr, nullptr, nullptr, DestroyExData);
    GPR_ASSERT(ex_data_id != -1);
    // OpenSSL and different version of BoringSSL don't agree on API
    // so try both.
    tsi::SslSessionPtr session = NewSessionInternal(SSL_SESSION_new);
    SessionExDataId* data = new SessionExDataId{this, id};
    int result = SSL_SESSION_set_ex_data(session.get(), ex_data_id, data);
    EXPECT_EQ(result, 1);
    alive_sessions_.insert(id);
    return session;
  }

  bool IsAlive(long id) const {
    return alive_sessions_.find(id) != alive_sessions_.end();
  }

  size_t AliveCount() const { return alive_sessions_.size(); }

 private:
  tsi::SslSessionPtr NewSessionInternal(SSL_SESSION* (*cb)()) {
    return tsi::SslSessionPtr(cb());
  }

  tsi::SslSessionPtr NewSessionInternal(SSL_SESSION* (*cb)(const SSL_CTX*)) {
    return tsi::SslSessionPtr(cb(ssl_context_));
  }

  static void DestroyExData(void* /*parent*/, void* ptr, CRYPTO_EX_DATA* /*ad*/,
                            int /*index*/, long /*argl*/, void* /*argp*/) {
    SessionExDataId* data = static_cast<SessionExDataId*>(ptr);
    data->tracker->alive_sessions_.erase(data->id);
    delete data;
  }

  SSL_CTX* ssl_context_;
  std::unordered_set<long> alive_sessions_;
};

TEST(SslSessionCacheTest, InitialState) {
  SessionTracker tracker;
  // Verify session initial state.
  {
    tsi::SslSessionPtr tmp_sess = tracker.NewSession(1);
    EXPECT_TRUE(tracker.IsAlive(1));
    EXPECT_EQ(tracker.AliveCount(), 1);
  }
  EXPECT_FALSE(tracker.IsAlive(1));
  EXPECT_EQ(tracker.AliveCount(), 0);
}

TEST(SslSessionCacheTest, LruCache) {
  SessionTracker tracker;
  {
    RefCountedPtr<tsi::SslSessionLRUCache> cache =
        tsi::SslSessionLRUCache::Create(3);
    tsi::SslSessionPtr sess2 = tracker.NewSession(2);
    SSL_SESSION* sess2_ptr = sess2.get();
    cache->Put("first.dropbox.com", std::move(sess2));
    EXPECT_EQ(cache->Get("first.dropbox.com").get(), sess2_ptr);
    EXPECT_TRUE(tracker.IsAlive(2));
    EXPECT_EQ(tracker.AliveCount(), 1);
    // Putting element with the same key destroys old session.
    tsi::SslSessionPtr sess3 = tracker.NewSession(3);
    SSL_SESSION* sess3_ptr = sess3.get();
    cache->Put("first.dropbox.com", std::move(sess3));
    EXPECT_FALSE(tracker.IsAlive(2));
    EXPECT_EQ(cache->Get("first.dropbox.com").get(), sess3_ptr);
    EXPECT_TRUE(tracker.IsAlive(3));
    EXPECT_EQ(tracker.AliveCount(), 1);
    // Putting three more elements discards current one.
    for (long id = 4; id < 7; id++) {
      EXPECT_TRUE(tracker.IsAlive(3));
      std::string domain = std::to_string(id) + ".random.domain";
      cache->Put(domain.c_str(), tracker.NewSession(id));
    }
    EXPECT_EQ(cache->Size(), 3);
    EXPECT_FALSE(tracker.IsAlive(3));
    EXPECT_EQ(tracker.AliveCount(), 3);
    // Accessing element moves it into front of the queue.
    EXPECT_TRUE(cache->Get("4.random.domain"));
    EXPECT_TRUE(tracker.IsAlive(4));
    EXPECT_TRUE(tracker.IsAlive(5));
    EXPECT_TRUE(tracker.IsAlive(6));
    // One element has to be evicted from cache->
    cache->Put("7.random.domain", tracker.NewSession(7));
    EXPECT_TRUE(tracker.IsAlive(4));
    EXPECT_FALSE(tracker.IsAlive(5));
    EXPECT_TRUE(tracker.IsAlive(6));
    EXPECT_TRUE(tracker.IsAlive(7));
    EXPECT_EQ(tracker.AliveCount(), 3);
  }
  // Cache destructor destroys all sessions.
  EXPECT_EQ(tracker.AliveCount(), 0);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
