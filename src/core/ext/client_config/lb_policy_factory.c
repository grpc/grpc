/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/client_config/lb_policy_factory.h"

grpc_lb_addresses* grpc_lb_addresses_create(size_t num_addresses) {
  grpc_lb_addresses* addresses = gpr_malloc(sizeof(grpc_lb_addresses));
  addresses->num_addresses = num_addresses;
  const size_t addresses_size = sizeof(grpc_lb_address) * num_addresses;
  addresses->addresses = gpr_malloc(addresses_size);
  memset(addresses->addresses, 0, addresses_size);
  return addresses;
}

grpc_lb_addresses* grpc_lb_addresses_copy(grpc_lb_addresses* addresses,
                                          void* (*user_data_copy)(void*)) {
  grpc_lb_addresses* new_addresses =
      grpc_lb_addresses_create(addresses->num_addresses);
  memcpy(new_addresses->addresses, addresses->addresses,
         sizeof(grpc_lb_address) * addresses->num_addresses);
  for (size_t i = 0; i < addresses->num_addresses; ++i) {
    if (new_addresses->addresses[i].balancer_name != NULL) {
      new_addresses->addresses[i].balancer_name =
          gpr_strdup(new_addresses->addresses[i].balancer_name);
    }
    if (user_data_copy != NULL) {
      new_addresses->addresses[i].user_data =
          user_data_copy(new_addresses->addresses[i].user_data);
    }
  }
  return new_addresses;
}

void grpc_lb_addresses_set_address(grpc_lb_addresses* addresses, size_t index,
                                   void* address, size_t address_len,
                                   bool is_balancer, char* balancer_name,
                                   void* user_data) {
  GPR_ASSERT(index < addresses->num_addresses);
  grpc_lb_address* target = &addresses->addresses[index];
  memcpy(target->address.addr, address, address_len);
  target->address.len = address_len;
  target->is_balancer = is_balancer;
  target->balancer_name = balancer_name;
  target->user_data = user_data;
}

void grpc_lb_addresses_destroy(grpc_lb_addresses* addresses,
                               void (*user_data_destroy)(void*)) {
  for (size_t i = 0; i < addresses->num_addresses; ++i) {
    gpr_free(addresses->addresses[i].balancer_name);
    if (user_data_destroy != NULL) {
      user_data_destroy(addresses->addresses[i].user_data);
    }
  }
  gpr_free(addresses->addresses);
  gpr_free(addresses);
}

void grpc_lb_policy_factory_ref(grpc_lb_policy_factory* factory) {
  factory->vtable->ref(factory);
}

void grpc_lb_policy_factory_unref(grpc_lb_policy_factory* factory) {
  factory->vtable->unref(factory);
}

grpc_lb_policy* grpc_lb_policy_factory_create_lb_policy(
    grpc_exec_ctx* exec_ctx, grpc_lb_policy_factory* factory,
    grpc_lb_policy_args* args) {
  if (factory == NULL) return NULL;
  return factory->vtable->create_lb_policy(exec_ctx, factory, args);
}
