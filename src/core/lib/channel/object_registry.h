/*
 *
 * Copyright 2017 gRPC authors.
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

#ifndef GRPC_CORE_LIB_CHANNEL_OBJECT_REGISTRY_H
#define GRPC_CORE_LIB_CHANNEL_OBJECT_REGISTRY_H

#include <grpc/impl/codegen/port_platform.h>

#include <stdint.h>

// TODO(ncteisen): convert this file to C++

// Different types that may be stored in the general object registry. For now,
// the only use case is channel tracers, but the design has been left general.
typedef enum {
  // Used to hold uuid -> ChannelTracer mappings to allow for the trace data
  // to be looked up by uuid, rather then have to walk the entire tree of
  // trace.
  GRPC_OBJECT_REGISTRY_CHANNEL_TRACER,
  // Usually represents an error has occurred in the object lookup.
  GRPC_OBJECT_REGISTRY_UNKNOWN,
} grpc_object_registry_type;

void grpc_object_registry_init();
void grpc_object_registry_shutdown();

// globally registers the object. Returns its unique uuid
intptr_t grpc_object_registry_register_object(void* object,
                                              grpc_object_registry_type type);
// globally unregisters the object that is associated to uuid.
void grpc_object_registry_unregister_object(intptr_t uuid);
// if object with uuid has previously been registered, stores it in *object.
// if not, returns GRPC_OBJECT_REGISTRY_UNKNOWN and sets *object unchanged.
grpc_object_registry_type grpc_object_registry_get_object(intptr_t uuid,
                                                          void** object);

#endif /* GRPC_CORE_LIB_CHANNEL_OBJECT_REGISTRY_H */
