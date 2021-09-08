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

#ifndef GRPC_CORE_LIB_CHANNEL_HANDSHAKER_FACTORY_H
#define GRPC_CORE_LIB_CHANNEL_HANDSHAKER_FACTORY_H

#include <grpc/support/port_platform.h>

#include <grpc/impl/codegen/grpc_types.h>

// A handshaker factory is used to create handshakers.

// TODO(ctiller): grpc_pollset_set and HandshakeManager are forward declared in
// this file. grpc_pollset_set ought to be eliminated when EventEngine lands IO
// support. At the same time, we ought to be able to include handshake_manager.h
// here and eliminate the HandshakeManager dependency - we cannot right now
// because HandshakeManager names too many iomgr types.

typedef struct grpc_pollset_set grpc_pollset_set;

namespace grpc_core {

class HandshakeManager;

class HandshakerFactory {
 public:
  virtual void AddHandshakers(const grpc_channel_args* args,
                              grpc_pollset_set* interested_parties,
                              HandshakeManager* handshake_mgr) = 0;
  virtual ~HandshakerFactory() = default;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_CHANNEL_HANDSHAKER_FACTORY_H */
