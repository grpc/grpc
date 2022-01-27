/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/ext/filters/client_channel/client_channel_factory.h"

#include "src/core/lib/channel/channel_args.h"

// Channel arg key for client channel factory.
#define GRPC_ARG_CLIENT_CHANNEL_FACTORY "grpc.client_channel_factory"

namespace grpc_core {

namespace {

void* factory_arg_copy(void* f) { return f; }
void factory_arg_destroy(void* /*f*/) {}
int factory_arg_cmp(void* factory1, void* factory2) {
  return QsortCompare(factory1, factory2);
}
const grpc_arg_pointer_vtable factory_arg_vtable = {
    factory_arg_copy, factory_arg_destroy, factory_arg_cmp};

}  // namespace

grpc_arg ClientChannelFactory::CreateChannelArg(ClientChannelFactory* factory) {
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_ARG_CLIENT_CHANNEL_FACTORY), factory,
      &factory_arg_vtable);
}

ClientChannelFactory* ClientChannelFactory::GetFromChannelArgs(
    const grpc_channel_args* args) {
  const grpc_arg* arg =
      grpc_channel_args_find(args, GRPC_ARG_CLIENT_CHANNEL_FACTORY);
  if (arg == nullptr || arg->type != GRPC_ARG_POINTER) return nullptr;
  return static_cast<ClientChannelFactory*>(arg->value.pointer.p);
}

}  // namespace grpc_core
