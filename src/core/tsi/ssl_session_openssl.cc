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

#include <grpc/support/log.h>

#ifndef OPENSSL_IS_BORINGSSL

namespace grpc_core {

SslCachedSession::SslCachedSession(SslSessionPtr session) {
  int size = i2d_SSL_SESSION(session.get(), nullptr);
  GPR_ASSERT(size > 0);
  grpc_slice slice = grpc_slice_malloc(size_t(size));
  unsigned char* start = GRPC_SLICE_START_PTR(slice);
  int second_size = i2d_SSL_SESSION(session.get(), &start);
  GPR_ASSERT(size == second_size);
  serialized_session_ = slice;
}

SslCachedSession::SslCachedSession(SslCachedSession&& other) {
  serialized_session_ = grpc_empty_slice();
  *this = std::move(other);
}

SslCachedSession::~SslCachedSession() { grpc_slice_unref(serialized_session_); }

SslCachedSession& SslCachedSession::operator=(SslCachedSession&& other) {
  std::swap(serialized_session_, other.serialized_session_);
  return *this;
}

SslSessionPtr SslCachedSession::Get() const {
  const unsigned char* data = GRPC_SLICE_START_PTR(serialized_session_);
  size_t length = GRPC_SLICE_LENGTH(serialized_session_);
  SSL_SESSION* session = d2i_SSL_SESSION(nullptr, &data, length);
  if (session == nullptr) {
    return SslSessionPtr();
  }
  return SslSessionPtr(session);
}

}  // namespace grpc_core

#endif /* OPENSSL_IS_BORINGSSL */
