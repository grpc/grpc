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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CLIENT_CHANNEL_FACTORY_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CLIENT_CHANNEL_FACTORY_H

#include <grpc/support/port_platform.h>

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/lib/channel/channel_stack.h"

// Channel arg key for client channel factory.
#define GRPC_ARG_CLIENT_CHANNEL_FACTORY "grpc.client_channel_factory"

typedef struct grpc_client_channel_factory grpc_client_channel_factory;
typedef struct grpc_client_channel_factory_vtable
    grpc_client_channel_factory_vtable;

typedef enum {
  GRPC_CLIENT_CHANNEL_TYPE_REGULAR, /** for the user-level regular calls */
  GRPC_CLIENT_CHANNEL_TYPE_LOAD_BALANCING, /** for communication with a load
                                              balancing service */
} grpc_client_channel_type;

/** Constructor for new configured channels.
    Creating decorators around this type is encouraged to adapt behavior. */
struct grpc_client_channel_factory {
  const grpc_client_channel_factory_vtable* vtable;
};

struct grpc_client_channel_factory_vtable {
  void (*ref)(grpc_client_channel_factory* factory);
  void (*unref)(grpc_client_channel_factory* factory);
  grpc_subchannel* (*create_subchannel)(grpc_client_channel_factory* factory,
                                        const grpc_subchannel_args* args);
  grpc_channel* (*create_client_channel)(grpc_client_channel_factory* factory,
                                         const char* target,
                                         grpc_client_channel_type type,
                                         const grpc_channel_args* args);
};

void grpc_client_channel_factory_ref(grpc_client_channel_factory* factory);
void grpc_client_channel_factory_unref(grpc_client_channel_factory* factory);

/** Create a new grpc_subchannel */
grpc_subchannel* grpc_client_channel_factory_create_subchannel(
    grpc_client_channel_factory* factory, const grpc_subchannel_args* args);

/** Create a new grpc_channel */
grpc_channel* grpc_client_channel_factory_create_channel(
    grpc_client_channel_factory* factory, const char* target,
    grpc_client_channel_type type, const grpc_channel_args* args);

grpc_arg grpc_client_channel_factory_create_channel_arg(
    grpc_client_channel_factory* factory);

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CLIENT_CHANNEL_FACTORY_H */
