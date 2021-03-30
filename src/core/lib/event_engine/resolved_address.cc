// Copyright 2021 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License. You may obtain a copy of
// the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.
#include <grpc/support/port_platform.h>

#include <string.h>

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/log.h>

#include "src/core/lib/iomgr/port.h"

namespace grpc_event_engine {
namespace experimental {

EventEngine::ResolvedAddress::ResolvedAddress(const sockaddr* addr,
                                              socklen_t len) {
  GPR_ASSERT(len <= sizeof(buffer_));
  memset(&buffer_, 0, MAX_SIZE_BYTES);
  memcpy(&buffer_, addr, len);
}

const struct sockaddr* EventEngine::ResolvedAddress::Sockaddr() const {
  return reinterpret_cast<const struct sockaddr*>(buffer_);
}

socklen_t EventEngine::ResolvedAddress::Length() const { return len_; }

}  // namespace experimental
}  // namespace grpc_event_engine
