/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <string>
#include <unordered_set>

#include "src/core/tsi/ssl_session_cache.h"
#include "test/core/util/test_config.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>

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

  SslSessionPtr NewSession(long id) {
    static int ex_data_id = SSL_SESSION_get_ex_new_index(
        0, nullptr, nullptr, nullptr, DestroyExData);
    GPR_ASSERT(ex_data_id != -1);

    // OpenSSL and different version of BoringSSL don't agree on API
    // so try both.
    SslSessionPtr session = NewSessionInternal(SSL_SESSION_new);

    SessionExDataId* data = new SessionExDataId{this, id};
    int result = SSL_SESSION_set_ex_data(session.get(), ex_data_id, data);
    GPR_ASSERT(result == 1);

    alive_sessions_.insert(id);

    return session;
  }

  bool IsAlive(long id) const {
    return alive_sessions_.find(id) != alive_sessions_.end();
  }

  size_t AliveCount() const { return alive_sessions_.size(); }

 private:
  SslSessionPtr NewSessionInternal(SSL_SESSION* (*cb)()) {
    return SslSessionPtr(cb());
  }

  SslSessionPtr NewSessionInternal(SSL_SESSION* (*cb)(const SSL_CTX*)) {
    return SslSessionPtr(cb(ssl_context_));
  }

  static void DestroyExData(void* parent, void* ptr, CRYPTO_EX_DATA* ad,
                            int index, long argl, void* argp) {
    SessionExDataId* data = static_cast<SessionExDataId*>(ptr);
    data->tracker->alive_sessions_.erase(data->id);
    delete data;
  }

  SSL_CTX* ssl_context_;
  std::unordered_set<long> alive_sessions_;
};

void test_lru_cache() {
  SessionTracker tracker;

  // Verify session initial state.
  {
    SslSessionPtr tmp_sess = tracker.NewSession(1);
    GPR_ASSERT(tmp_sess->references == 1);
    GPR_ASSERT(tracker.IsAlive(1));
    GPR_ASSERT(tracker.AliveCount() == 1);
  }
  GPR_ASSERT(!tracker.IsAlive(1));
  GPR_ASSERT(tracker.AliveCount() == 0);

  {
    SslSessionLRUCache cache(3);

    SslSessionPtr sess2 = tracker.NewSession(2);
    SSL_SESSION* sess2_ptr = sess2.get();
    cache.PutLocked("first.dropbox.com", std::move(sess2));
    GPR_ASSERT(cache.GetLocked("first.dropbox.com").get() == sess2_ptr);
    GPR_ASSERT(tracker.IsAlive(2));
    GPR_ASSERT(tracker.AliveCount() == 1);

    // Putting element with the same key destroys old session.
    SslSessionPtr sess3 = tracker.NewSession(3);
    SSL_SESSION* sess3_ptr = sess3.get();
    cache.PutLocked("first.dropbox.com", std::move(sess3));
    GPR_ASSERT(!tracker.IsAlive(2));
    GPR_ASSERT(cache.GetLocked("first.dropbox.com").get() == sess3_ptr);
    GPR_ASSERT(tracker.IsAlive(3));
    GPR_ASSERT(tracker.AliveCount() == 1);

    // Putting three more elements discards current one.
    for (long id = 4; id < 7; id++) {
      GPR_ASSERT(tracker.IsAlive(3));
      std::string domain = std::to_string(id) + ".random.domain";
      cache.PutLocked(domain.c_str(), tracker.NewSession(id));
    }
    GPR_ASSERT(cache.Size() == 3);
    GPR_ASSERT(!tracker.IsAlive(3));
    GPR_ASSERT(tracker.AliveCount() == 3);

    // Accessing element moves it into front of the queue.
    GPR_ASSERT(cache.GetLocked("4.random.domain"));
    GPR_ASSERT(tracker.IsAlive(4));
    GPR_ASSERT(tracker.IsAlive(5));
    GPR_ASSERT(tracker.IsAlive(6));

    cache.PutLocked("7.random.domain", tracker.NewSession(7));
    GPR_ASSERT(tracker.IsAlive(4));
    GPR_ASSERT(!tracker.IsAlive(5));
    GPR_ASSERT(tracker.IsAlive(6));
    GPR_ASSERT(tracker.IsAlive(7));

    GPR_ASSERT(tracker.AliveCount() == 3);
  }

  // Cache destructor destroys all sessions.
  GPR_ASSERT(tracker.AliveCount() == 0);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();

  grpc_core::test_lru_cache();

  grpc_shutdown();
  return 0;
}
