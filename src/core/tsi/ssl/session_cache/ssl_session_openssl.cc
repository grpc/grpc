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

#include <grpc/support/log.h>

#ifndef OPENSSL_IS_BORINGSSL

// OpenSSL invalidates SSL_SESSION on SSL destruction making it pointless
// to cache sessions. The workaround is to serialize (relatively expensive)
// session into binary blob and re-create it from blob on every handshake.
// Note that it's safe to keep serialized session outside of SSL lifetime
// as openssl performs all necessary validation while attempting to use a
// session and creates a new one if something is wrong (e.g. server changed
// set of allowed codecs).

namespace tsi {
namespace {

class OpenSslCachedSession : public SslCachedSession {
 public:
  OpenSslCachedSession(SslSessionPtr session)
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

grpc_core::UniquePtr<SslCachedSession> SslCachedSession::Create(
    SslSessionPtr session) {
  return grpc_core::UniquePtr<SslCachedSession>(
      grpc_core::New<OpenSslCachedSession>(std::move(session)));
}

}  // namespace tsi

#endif /* OPENSSL_IS_BORINGSSL */
