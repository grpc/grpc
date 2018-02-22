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

#include "src/core/tsi/ssl_session.h"

#ifdef OPENSSL_IS_BORINGSSL

namespace grpc_core {

SslCachedSession::SslCachedSession(SslSessionPtr session)
    : session_(std::move(session)) {}

SslCachedSession::SslCachedSession(SslCachedSession&& other)
    : session_(std::move(other.session_)) {}

SslCachedSession::~SslCachedSession() {}

SslCachedSession& SslCachedSession::operator=(SslCachedSession&& other) {
  session_ = std::move(other.session_);
  return *this;
}

SslSessionPtr SslCachedSession::Get() const {
  // SslSessionPtr will dereference on destruction.
  SSL_SESSION_up_ref(session_.get());
  return SslSessionPtr(session_.get());
}

}  // namespace grpc_core

#endif /* OPENSSL_IS_BORINGSSL */
