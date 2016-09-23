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

#ifndef GRPC_CORE_EXT_CLIENT_CONFIG_LB_POLICY_FACTORY_H
#define GRPC_CORE_EXT_CLIENT_CONFIG_LB_POLICY_FACTORY_H

#include "src/core/ext/client_config/client_channel_factory.h"
#include "src/core/ext/client_config/lb_policy.h"

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/resolve_address.h"

typedef struct grpc_lb_policy_factory grpc_lb_policy_factory;
typedef struct grpc_lb_policy_factory_vtable grpc_lb_policy_factory_vtable;

struct grpc_lb_policy_factory {
  const grpc_lb_policy_factory_vtable *vtable;
};

/** A resolved address alongside any LB related information associated with it.
 * \a user_data, if not NULL, contains opaque data meant to be consumed by the
 * gRPC LB policy. Note that no all LB policies support \a user_data as input.
 * Those who don't will simply ignore it and will correspondingly return NULL in
 * their namesake pick() output argument. */
typedef struct grpc_lb_address {
  grpc_resolved_address address;
  bool is_balancer;
  char *balancer_name; /* For secure naming. */
  void *user_data;
} grpc_lb_address;

typedef struct grpc_lb_addresses {
  size_t num_addresses;
  grpc_lb_address *addresses;
} grpc_lb_addresses;

/** Returns a grpc_addresses struct with enough space for
 * \a num_addresses addresses. */
grpc_lb_addresses *grpc_lb_addresses_create(size_t num_addresses);

/** Creates a copy of \a addresses.  If \a user_data_copy is not NULL,
 * it will be invoked to copy the \a user_data field of each address. */
grpc_lb_addresses *grpc_lb_addresses_copy(grpc_lb_addresses *addresses,
                                          void *(*user_data_copy)(void *));

/** Sets the value of the address at index \a index of \a addresses.
 * \a address is a socket address of length \a address_len.
 * Takes ownership of \a balancer_name. */
void grpc_lb_addresses_set_address(grpc_lb_addresses *addresses, size_t index,
                                   void *address, size_t address_len,
                                   bool is_balancer, char *balancer_name,
                                   void *user_data);

/** Destroys \a addresses.  If \a user_data_destroy is not NULL, it will
 * be invoked to destroy the \a user_data field of each address. */
void grpc_lb_addresses_destroy(grpc_lb_addresses *addresses,
                               void (*user_data_destroy)(void *));

/** Arguments passed to LB policies. */
/* TODO(roth, ctiller): Consider replacing this struct with
   grpc_channel_args.  See comment in resolver_result.h for details. */
typedef struct grpc_lb_policy_args {
  grpc_lb_addresses *addresses;
  grpc_client_channel_factory *client_channel_factory;
  /* Can be used to pass implementation-specific parameters to the LB policy. */
  grpc_channel_args *additional_args;
} grpc_lb_policy_args;

struct grpc_lb_policy_factory_vtable {
  void (*ref)(grpc_lb_policy_factory *factory);
  void (*unref)(grpc_lb_policy_factory *factory);

  /** Implementation of grpc_lb_policy_factory_create_lb_policy */
  grpc_lb_policy *(*create_lb_policy)(grpc_exec_ctx *exec_ctx,
                                      grpc_lb_policy_factory *factory,
                                      grpc_lb_policy_args *args);

  /** Name for the LB policy this factory implements */
  const char *name;
};

void grpc_lb_policy_factory_ref(grpc_lb_policy_factory *factory);
void grpc_lb_policy_factory_unref(grpc_lb_policy_factory *factory);

/** Create a lb_policy instance. */
grpc_lb_policy *grpc_lb_policy_factory_create_lb_policy(
    grpc_exec_ctx *exec_ctx, grpc_lb_policy_factory *factory,
    grpc_lb_policy_args *args);

#endif /* GRPC_CORE_EXT_CLIENT_CONFIG_LB_POLICY_FACTORY_H */
