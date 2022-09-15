/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_CORE_LIB_TRANSPORT_HANDSHAKER_FACTORY_H
#define GRPC_CORE_LIB_TRANSPORT_HANDSHAKER_FACTORY_H

#include <grpc/support/port_platform.h>

#include "absl/types/variant.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"

// A handshaker factory is used to create handshakers.

// TODO(ctiller): HandshakeManager is forward declared in this file. When
// EventEngine lands IO support we ought to be able to include
// handshake_manager.h here and eliminate the HandshakeManager dependency - we
// cannot right now because HandshakeManager names too many iomgr types.

namespace grpc_core {

class HandshakeManager;

// Enum representing the priority of the client handshakers.
// The order of the client handshakers is decided by the priority.
// For example kPreTCPConnect handshakers are called before kTCPConnect and so
// on.
enum class HandshakerClientPriority : int {
  kPreTCPConnect,
  kTCPConnect,
  kHTTPConnect,
  kSecurity,
};

// Enum representing the priority of the server handshakers.
// The order of the server handshakers is decided by the priority.
// For example kReadHeadSecurity handshakers are called before kSecurity and so
// on.
enum class HandshakerServerPriority : int {
  kReadHeadSecurity,
  kSecurity,
};

using HandshakerPriority =
    absl::variant<HandshakerClientPriority, HandshakerServerPriority>;

class HandshakerFactory {
 public:
  virtual void AddHandshakers(const ChannelArgs& args,
                              grpc_pollset_set* interested_parties,
                              HandshakeManager* handshake_mgr) = 0;
  // Return the priority associated with the handshaker.
  // The priority can be either HandshakerClientPriority or
  // HandshakerServerPriority depending on the type of handshaker.
  virtual HandshakerPriority Priority() = 0;
  virtual ~HandshakerFactory() = default;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_TRANSPORT_HANDSHAKER_FACTORY_H */
