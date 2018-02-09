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

#include "src/core/ext/filters/client_channel/client_channel_factory.h"
#include "src/core/lib/channel/channel_args.h"

void grpc_client_channel_factory_ref(grpc_client_channel_factory* factory) {
  factory->vtable->ref(factory);
}

void grpc_client_channel_factory_unref(grpc_client_channel_factory* factory) {
  factory->vtable->unref(factory);
}

grpc_subchannel* grpc_client_channel_factory_create_subchannel(
    grpc_client_channel_factory* factory, const grpc_subchannel_args* args) {
  return factory->vtable->create_subchannel(factory, args);
}

grpc_channel* grpc_client_channel_factory_create_channel(
    grpc_client_channel_factory* factory, const char* target,
    grpc_client_channel_type type, const grpc_channel_args* args) {
  return factory->vtable->create_client_channel(factory, target, type, args);
}

static void* factory_arg_copy(void* factory) {
  grpc_client_channel_factory_ref(
      static_cast<grpc_client_channel_factory*>(factory));
  return factory;
}

static void factory_arg_destroy(void* factory) {
  grpc_client_channel_factory_unref(
      static_cast<grpc_client_channel_factory*>(factory));
}

static int factory_arg_cmp(void* factory1, void* factory2) {
  if (factory1 < factory2) return -1;
  if (factory1 > factory2) return 1;
  return 0;
}

static const grpc_arg_pointer_vtable factory_arg_vtable = {
    factory_arg_copy, factory_arg_destroy, factory_arg_cmp};

grpc_arg grpc_client_channel_factory_create_channel_arg(
    grpc_client_channel_factory* factory) {
  return grpc_channel_arg_pointer_create((char*)GRPC_ARG_CLIENT_CHANNEL_FACTORY,
                                         factory, &factory_arg_vtable);
}
