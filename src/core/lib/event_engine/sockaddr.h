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
#ifndef GRPC_CORE_LIB_EVENT_ENGINE_SOCKADDR_H
#define GRPC_CORE_LIB_EVENT_ENGINE_SOCKADDR_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/port.h"

// Platforms are expected to provide definitions for:
// * sockaddr
// * sockaddr_in
// * sockaddr_in6
namespace grpc_io {

// A thin wrapper around a platform-specific sockaddr type. A sockaddr struct
// exists on all platforms that gRPC supports.
class ResolvedAddress {
 public:
  ResolvedAddress(const void* addr, int len);
  const struct sockaddr* Sockaddr() const;
  const int Length() const;

 private:
  char buffer_[128];
  int len_;
};

}  // namespace grpc_io

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_SOCKADDR_H
