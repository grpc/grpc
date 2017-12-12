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

#include "src/core/ext/filters/client_channel/resolver_factory.h"

void grpc_resolver_factory_ref(grpc_resolver_factory* factory) {
  factory->vtable->ref(factory);
}

void grpc_resolver_factory_unref(grpc_resolver_factory* factory) {
  factory->vtable->unref(factory);
}

/** Create a resolver instance for a name */
grpc_resolver* grpc_resolver_factory_create_resolver(
    grpc_resolver_factory* factory, grpc_resolver_args* args) {
  if (factory == nullptr) return nullptr;
  return factory->vtable->create_resolver(factory, args);
}

char* grpc_resolver_factory_get_default_authority(
    grpc_resolver_factory* factory, grpc_uri* uri) {
  if (factory == nullptr) return nullptr;
  return factory->vtable->get_default_authority(factory, uri);
}
