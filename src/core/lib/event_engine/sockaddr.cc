/*
 *
 * Copyright 2021 gRPC authors.
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

#include "src/core/lib/event_engine/sockaddr.h"

#include <string.h>

#include "grpc/support/log.h"

namespace grpc_io {

ResolvedAddress::ResolvedAddress(const void* addr, int len) {
  GPR_ASSERT(len <= 128);
  // TODO: elim magic number
  memset(&buffer_, 0, 128);
  memcpy(&buffer_, addr, len);
}

const struct sockaddr* ResolvedAddress::Sockaddr() const {
  return reinterpret_cast<const struct sockaddr*>(buffer_);
}

const int ResolvedAddress::Length() const { return len_; }

}  // namespace grpc_io
