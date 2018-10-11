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

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/handshaker_factory.h"

#include <grpc/support/log.h>

void grpc_handshaker_factory_add_handshakers(
    grpc_handshaker_factory* handshaker_factory, const grpc_channel_args* args,
    grpc_pollset_set* interested_parties,
    grpc_handshake_manager* handshake_mgr) {
  if (handshaker_factory != nullptr) {
    GPR_ASSERT(handshaker_factory->vtable != nullptr);
    handshaker_factory->vtable->add_handshakers(
        handshaker_factory, args, interested_parties, handshake_mgr);
  }
}

void grpc_handshaker_factory_destroy(
    grpc_handshaker_factory* handshaker_factory) {
  if (handshaker_factory != nullptr) {
    GPR_ASSERT(handshaker_factory->vtable != nullptr);
    handshaker_factory->vtable->destroy(handshaker_factory);
  }
}
