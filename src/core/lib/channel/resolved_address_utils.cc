//
//
// Copyright 2022 gRPC authors.
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

#include "src/core/lib/channel/resolved_address_utils.h"

#include <string.h>

#include "src/core/lib/channel/channel_args.h"

namespace {

void* grpc_resolved_addr_copy(void* p) {
  if (p == nullptr) return nullptr;
  grpc_resolved_address* addr = new grpc_resolved_address;
  memcpy(addr, static_cast<grpc_resolved_address*>(p),
         sizeof(grpc_resolved_address));
  return addr;
}

int grpc_resolved_addr_cmp(void* a, void* b) {
  grpc_resolved_address* addr_a = static_cast<grpc_resolved_address*>(a);
  grpc_resolved_address* addr_b = static_cast<grpc_resolved_address*>(b);
  if (addr_a->len < addr_b->len) {
    return -1;
  }
  if (addr_a->len > addr_b->len) {
    return 1;
  }
  return strcmp(addr_a->addr, addr_b->addr);
}

void grpc_resolved_addr_destroy(void* p) {
  grpc_resolved_address* addr = static_cast<grpc_resolved_address*>(p);
  delete addr;
}

const grpc_arg_pointer_vtable connector_arg_vtable = {
    grpc_resolved_addr_copy, grpc_resolved_addr_destroy,
    grpc_resolved_addr_cmp};

}  // namespace

namespace grpc_core {

grpc_resolved_address* grpc_resolved_address_from_arg(const grpc_arg* arg) {
  if (arg == nullptr) return nullptr;
  if (arg->type != GRPC_ARG_POINTER) {
    return nullptr;
  }
  return static_cast<grpc_resolved_address*>(arg->value.pointer.p);
}

grpc_arg grpc_resolved_address_to_arg(const char* key,
                                      grpc_resolved_address* addr) {
  return grpc_channel_arg_pointer_create(const_cast<char*>(key), addr,
                                         &connector_arg_vtable);
}

}  // namespace grpc_core
