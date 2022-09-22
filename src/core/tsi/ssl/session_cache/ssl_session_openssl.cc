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

#include <grpc/support/log.h>

#include "src/core/tsi/ssl/session_cache/ssl_session.h"

#ifndef OPENSSL_IS_BORINGSSL

#include "absl/memory/memory.h"

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
  OpenSslCachedSession(SslSessionPtr session) {
    int size = i2d_SSL_SESSION(session.get(), nullptr);
    GPR_ASSERT(size > 0);
    grpc_slice slice = grpc_slice_malloc(size_t(size));
    unsigned char* start = GRPC_SLICE_START_PTR(slice);
    int second_size = i2d_SSL_SESSION(session.get(), &start);
    GPR_ASSERT(size == second_size);
    serialized_session_ = slice;
  }

  virtual ~OpenSslCachedSession() { grpc_slice_unref(serialized_session_); }

  SslSessionPtr CopySession() const override {
    const unsigned char* data = GRPC_SLICE_START_PTR(serialized_session_);
    size_t length = GRPC_SLICE_LENGTH(serialized_session_);
    SSL_SESSION* session = d2i_SSL_SESSION(nullptr, &data, length);
    if (session == nullptr) {
      return SslSessionPtr();
    }
    return SslSessionPtr(session);
  }

 private:
  grpc_slice serialized_session_;
};

}  // namespace

std::unique_ptr<SslCachedSession> SslCachedSession::Create(
    SslSessionPtr session) {
  return absl::make_unique<OpenSslCachedSession>(std::move(session));
}

}  // namespace tsi

#endif /* OPENSSL_IS_BORINGSSL */
