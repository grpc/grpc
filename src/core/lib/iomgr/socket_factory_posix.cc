//
//
// Copyright 2017 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_SOCKET_FACTORY

#include <grpc/impl/grpc_types.h>
#include <grpc/support/sync.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/socket_factory_posix.h"
#include "src/core/util/useful.h"

void grpc_socket_factory_init(grpc_socket_factory* factory,
                              const grpc_socket_factory_vtable* vtable) {
  factory->vtable = vtable;
  gpr_ref_init(&factory->refcount, 1);
}

int grpc_socket_factory_socket(grpc_socket_factory* factory, int domain,
                               int type, int protocol) {
  return factory->vtable->socket(factory, domain, type, protocol);
}

int grpc_socket_factory_bind(grpc_socket_factory* factory, int sockfd,
                             const grpc_resolved_address* addr) {
  return factory->vtable->bind(factory, sockfd, addr);
}

int grpc_socket_factory_compare(grpc_socket_factory* a,
                                grpc_socket_factory* b) {
  int c = grpc_core::QsortCompare(a, b);
  if (c != 0) {
    grpc_socket_factory* sma = a;
    grpc_socket_factory* smb = b;
    c = grpc_core::QsortCompare(sma->vtable, smb->vtable);
    if (c == 0) {
      c = sma->vtable->compare(sma, smb);
    }
  }
  return c;
}

grpc_socket_factory* grpc_socket_factory_ref(grpc_socket_factory* factory) {
  gpr_ref(&factory->refcount);
  return factory;
}

void grpc_socket_factory_unref(grpc_socket_factory* factory) {
  if (gpr_unref(&factory->refcount)) {
    factory->vtable->destroy(factory);
  }
}

static void* socket_factory_arg_copy(void* p) {
  return grpc_socket_factory_ref(static_cast<grpc_socket_factory*>(p));
}

static void socket_factory_arg_destroy(void* p) {
  grpc_socket_factory_unref(static_cast<grpc_socket_factory*>(p));
}

static int socket_factory_cmp(void* a, void* b) {
  return grpc_socket_factory_compare(static_cast<grpc_socket_factory*>(a),
                                     static_cast<grpc_socket_factory*>(b));
}

static const grpc_arg_pointer_vtable socket_factory_arg_vtable = {
    socket_factory_arg_copy, socket_factory_arg_destroy, socket_factory_cmp};

grpc_arg grpc_socket_factory_to_arg(grpc_socket_factory* factory) {
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_ARG_SOCKET_FACTORY), factory,
      &socket_factory_arg_vtable);
}

#endif
