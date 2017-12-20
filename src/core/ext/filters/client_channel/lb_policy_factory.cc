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

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"

#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/parse_address.h"

grpc_lb_addresses* grpc_lb_addresses_create(
    size_t num_addresses, const grpc_lb_user_data_vtable* user_data_vtable) {
  grpc_lb_addresses* addresses =
      (grpc_lb_addresses*)gpr_zalloc(sizeof(grpc_lb_addresses));
  addresses->num_addresses = num_addresses;
  addresses->user_data_vtable = user_data_vtable;
  const size_t addresses_size = sizeof(grpc_lb_address) * num_addresses;
  addresses->addresses = (grpc_lb_address*)gpr_zalloc(addresses_size);
  return addresses;
}

grpc_lb_addresses* grpc_lb_addresses_copy(const grpc_lb_addresses* addresses) {
  grpc_lb_addresses* new_addresses = grpc_lb_addresses_create(
      addresses->num_addresses, addresses->user_data_vtable);
  memcpy(new_addresses->addresses, addresses->addresses,
         sizeof(grpc_lb_address) * addresses->num_addresses);
  for (size_t i = 0; i < addresses->num_addresses; ++i) {
    if (new_addresses->addresses[i].balancer_name != nullptr) {
      new_addresses->addresses[i].balancer_name =
          gpr_strdup(new_addresses->addresses[i].balancer_name);
    }
    if (new_addresses->addresses[i].user_data != nullptr) {
      new_addresses->addresses[i].user_data = addresses->user_data_vtable->copy(
          new_addresses->addresses[i].user_data);
    }
  }
  return new_addresses;
}

void grpc_lb_addresses_set_address(grpc_lb_addresses* addresses, size_t index,
                                   const void* address, size_t address_len,
                                   bool is_balancer, const char* balancer_name,
                                   void* user_data) {
  GPR_ASSERT(index < addresses->num_addresses);
  if (user_data != nullptr) GPR_ASSERT(addresses->user_data_vtable != nullptr);
  grpc_lb_address* target = &addresses->addresses[index];
  memcpy(target->address.addr, address, address_len);
  target->address.len = address_len;
  target->is_balancer = is_balancer;
  target->balancer_name = gpr_strdup(balancer_name);
  target->user_data = user_data;
}

bool grpc_lb_addresses_set_address_from_uri(grpc_lb_addresses* addresses,
                                            size_t index, const grpc_uri* uri,
                                            bool is_balancer,
                                            const char* balancer_name,
                                            void* user_data) {
  grpc_resolved_address address;
  if (!grpc_parse_uri(uri, &address)) return false;
  grpc_lb_addresses_set_address(addresses, index, address.addr, address.len,
                                is_balancer, balancer_name, user_data);
  return true;
}

int grpc_lb_addresses_cmp(const grpc_lb_addresses* addresses1,
                          const grpc_lb_addresses* addresses2) {
  if (addresses1->num_addresses > addresses2->num_addresses) return 1;
  if (addresses1->num_addresses < addresses2->num_addresses) return -1;
  if (addresses1->user_data_vtable > addresses2->user_data_vtable) return 1;
  if (addresses1->user_data_vtable < addresses2->user_data_vtable) return -1;
  for (size_t i = 0; i < addresses1->num_addresses; ++i) {
    const grpc_lb_address* target1 = &addresses1->addresses[i];
    const grpc_lb_address* target2 = &addresses2->addresses[i];
    if (target1->address.len > target2->address.len) return 1;
    if (target1->address.len < target2->address.len) return -1;
    int retval = memcmp(target1->address.addr, target2->address.addr,
                        target1->address.len);
    if (retval != 0) return retval;
    if (target1->is_balancer > target2->is_balancer) return 1;
    if (target1->is_balancer < target2->is_balancer) return -1;
    const char* balancer_name1 =
        target1->balancer_name != nullptr ? target1->balancer_name : "";
    const char* balancer_name2 =
        target2->balancer_name != nullptr ? target2->balancer_name : "";
    retval = strcmp(balancer_name1, balancer_name2);
    if (retval != 0) return retval;
    if (addresses1->user_data_vtable != nullptr) {
      retval = addresses1->user_data_vtable->cmp(target1->user_data,
                                                 target2->user_data);
      if (retval != 0) return retval;
    }
  }
  return 0;
}

void grpc_lb_addresses_destroy(grpc_lb_addresses* addresses) {
  for (size_t i = 0; i < addresses->num_addresses; ++i) {
    gpr_free(addresses->addresses[i].balancer_name);
    if (addresses->addresses[i].user_data != nullptr) {
      addresses->user_data_vtable->destroy(addresses->addresses[i].user_data);
    }
  }
  gpr_free(addresses->addresses);
  gpr_free(addresses);
}

static void* lb_addresses_copy(void* addresses) {
  return grpc_lb_addresses_copy((grpc_lb_addresses*)addresses);
}
static void lb_addresses_destroy(void* addresses) {
  grpc_lb_addresses_destroy((grpc_lb_addresses*)addresses);
}
static int lb_addresses_cmp(void* addresses1, void* addresses2) {
  return grpc_lb_addresses_cmp((grpc_lb_addresses*)addresses1,
                               (grpc_lb_addresses*)addresses2);
}
static const grpc_arg_pointer_vtable lb_addresses_arg_vtable = {
    lb_addresses_copy, lb_addresses_destroy, lb_addresses_cmp};

grpc_arg grpc_lb_addresses_create_channel_arg(
    const grpc_lb_addresses* addresses) {
  return grpc_channel_arg_pointer_create(
      (char*)GRPC_ARG_LB_ADDRESSES, (void*)addresses, &lb_addresses_arg_vtable);
}

grpc_lb_addresses* grpc_lb_addresses_find_channel_arg(
    const grpc_channel_args* channel_args) {
  const grpc_arg* lb_addresses_arg =
      grpc_channel_args_find(channel_args, GRPC_ARG_LB_ADDRESSES);
  if (lb_addresses_arg == nullptr || lb_addresses_arg->type != GRPC_ARG_POINTER)
    return nullptr;
  return (grpc_lb_addresses*)lb_addresses_arg->value.pointer.p;
}

void grpc_lb_policy_factory_ref(grpc_lb_policy_factory* factory) {
  factory->vtable->ref(factory);
}

void grpc_lb_policy_factory_unref(grpc_lb_policy_factory* factory) {
  factory->vtable->unref(factory);
}

grpc_lb_policy* grpc_lb_policy_factory_create_lb_policy(
    grpc_lb_policy_factory* factory, grpc_lb_policy_args* args) {
  if (factory == nullptr) return nullptr;
  return factory->vtable->create_lb_policy(factory, args);
}
