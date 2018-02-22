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

#ifndef GRPC_CORE_TSI_SSL_SESSION_H
#define GRPC_CORE_TSI_SSL_SESSION_H

#include <grpc/support/port_platform.h>

#include <grpc/slice.h>

extern "C" {
#include <openssl/ssl.h>
}

#include "src/core/lib/gprpp/memory.h"

/// Cached SSL session.
///
/// BoringSSL and OpenSSL have different behavior regarding TLS ticket
/// resumption.
///
/// BoringSSL allows SSL_SESSION to outlive SSL and SSL_CTX objects which are
/// re-created by gRPC on every cert rotation/subchannel creation.
/// SSL_SESSION is also immutable in BoringSSL and it's safe to share
/// the same session between different threads and connections.
///
/// OpenSSL invalidates SSL_SESSION on SSL destruction making it pointless
/// to cache sessions. The workaround is to serialize (relatively expensive)
/// session into binary blob and re-create it from blob on every handshake.

namespace grpc_core {

struct SslSessionDeleter {
  void operator()(SSL_SESSION* session) { SSL_SESSION_free(session); }
};

typedef std::unique_ptr<SSL_SESSION, SslSessionDeleter> SslSessionPtr;

class SslCachedSession {
 public:
  /// Create cached representation of \a session.
  explicit SslCachedSession(SslSessionPtr session);
  SslCachedSession(SslCachedSession&& other);
  ~SslCachedSession();

  SslCachedSession& operator=(SslCachedSession&& other);

  /// Return previously cached session.
  SslSessionPtr Get() const;

 private:
#ifdef OPENSSL_IS_BORINGSSL
  SslSessionPtr session_;
#else
  grpc_slice serialized_session_;
#endif
};

}  // namespace grpc_core

#endif /* GRPC_CORE_TSI_SSL_SESSION_H */
