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

#include <grpc/support/port_platform.h>

#include "src/core/tsi/ssl/session_cache/ssl_session.h"

#ifdef OPENSSL_IS_BORINGSSL

#include "absl/memory/memory.h"

// BoringSSL allows SSL_SESSION to outlive SSL and SSL_CTX objects which are
// re-created by gRPC on every certificate rotation or subchannel creation.
// BoringSSL guarantees that SSL_SESSION is immutable so it's safe to share
// the same original session object between different threads and connections.

namespace tsi {
namespace {

class BoringSslCachedSession : public SslCachedSession {
 public:
  explicit BoringSslCachedSession(SslSessionPtr session)
      : session_(std::move(session)) {}

  SslSessionPtr CopySession() const override {
    // SslSessionPtr will dereference on destruction.
    SSL_SESSION_up_ref(session_.get());
    return SslSessionPtr(session_.get());
  }

 private:
  SslSessionPtr session_;
};

}  // namespace

std::unique_ptr<SslCachedSession> SslCachedSession::Create(
    SslSessionPtr session) {
  return std::make_unique<BoringSslCachedSession>(std::move(session));
}

}  // namespace tsi

#endif /* OPENSSL_IS_BORINGSSL */
