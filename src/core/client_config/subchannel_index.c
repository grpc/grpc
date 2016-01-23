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

#include "src/core/client_config/subchannel_index.h"

#include <stdbool.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/avl.h>
#include <grpc/support/tls.h>

#include "src/core/channel/channel_args.h"

/* a map of subchannel_key --> subchannel, used for detecting connections
   to the same destination in order to share them */
static gpr_avl g_subchannel_index;

static gpr_mu g_mu;

struct grpc_subchannel_key {
	grpc_connector *connector;
	grpc_subchannel_args args;
};

GPR_TLS_DECL(subchannel_index_exec_ctx);

static void enter_ctx(grpc_exec_ctx *exec_ctx) {
	GPR_ASSERT(gpr_tls_get(&subchannel_index_exec_ctx) == 0);
	gpr_tls_set(&subchannel_index_exec_ctx, (intptr_t)exec_ctx);
}

static void leave_ctx(grpc_exec_ctx *exec_ctx) {
	GPR_ASSERT(gpr_tls_get(&subchannel_index_exec_ctx) == (intptr_t)exec_ctx);
	gpr_tls_set(&subchannel_index_exec_ctx, 0);
}

static grpc_exec_ctx *current_ctx() {
	grpc_exec_ctx *c = (grpc_exec_ctx *)gpr_tls_get(&subchannel_index_exec_ctx);
	GPR_ASSERT(c != NULL);
	return c;
}

static grpc_subchannel_key *create_key(grpc_connector *connector, grpc_subchannel_args *args, grpc_channel_args *(*copy_channel_args)(const grpc_channel_args *args)) {
  grpc_subchannel_key *k = gpr_malloc(sizeof(*k));
  k->connector = grpc_connector_ref(connector);
  k->args.filter_count = args->filter_count;
  k->args.filters = gpr_malloc(sizeof(*k->args.filters) * k->args.filter_count);
  memcpy(k->args.filters, args->filters, sizeof(*k->args.filters) * k->args.filter_count);
  k->args.addr_len = args->addr_len;
  k->args.addr = gpr_malloc(args->addr_len);
  memcpy(k->args.addr, args->addr, k->args.addr_len);
  k->args.args = copy_channel_args(args->args);
  return k;
}

grpc_subchannel_key *grpc_subchannel_key_create(grpc_connector *connector, grpc_subchannel_args *args) {
	return create_key(connector, args, grpc_channel_args_normalize);
}

static grpc_subchannel_key *subchannel_key_copy(grpc_subchannel_key *k) {
	return create_key(k->connector, &k->args, grpc_channel_args_copy);
}

static int subchannel_key_compare(grpc_subchannel_key *a, grpc_subchannel_key *b) {
  int c = GPR_ICMP(a->connector, b->connector);
  if (c != 0) return c;
  c = GPR_ICMP(a->args.addr_len, b->args.addr_len);
  if (c != 0) return c;
  c = GPR_ICMP(a->args.filter_count, b->args.filter_count);
  if (c != 0) return c;
  c = memcmp(a->args.addr, b->args.addr, a->args.addr_len);
  if (c != 0) return c;
  c = memcmp(a->args.filters, b->args.filters, a->args.filter_count * sizeof(*a->args.filters));
  return grpc_channel_args_compare(a->args.args, b->args.args);
}

void grpc_subchannel_key_destroy(grpc_subchannel_key *k) {
  gpr_free(k->args.addr);
  gpr_free(k->args.filters);
  grpc_channel_args_destroy((grpc_channel_args*)k->args.args);
  gpr_free(k);
}

static void sck_avl_destroy(void *p) {
  grpc_subchannel_key_destroy(p);
}

static void *sck_avl_copy(void *p) {
  return subchannel_key_copy(p);
}

static long sck_avl_compare(void *a, void *b) {
  return subchannel_key_compare(a, b);
}

static void scv_avl_destroy(void *p) {
	grpc_exec_ctx *exec_ctx = current_ctx();
  GRPC_SUBCHANNEL_WEAK_UNREF(exec_ctx, p, "subchannel_index");
}

static void *scv_avl_copy(void *p) { 
  GRPC_SUBCHANNEL_WEAK_REF(p, "subchannel_index");
  return p; 
}

static const gpr_avl_vtable subchannel_avl_vtable = {
  .destroy_key = sck_avl_destroy,
  .copy_key = sck_avl_copy,
  .compare_keys = sck_avl_compare,
  .destroy_value = scv_avl_destroy,
  .copy_value = scv_avl_copy  
};

void grpc_subchannel_index_init(void) {
	g_subchannel_index = gpr_avl_create(&subchannel_avl_vtable);
	gpr_mu_init(&g_mu);
}

void grpc_subchannel_index_shutdown(void) {
	gpr_mu_destroy(&g_mu);
	gpr_avl_unref(g_subchannel_index);
}

grpc_subchannel *grpc_subchannel_index_find(
		grpc_exec_ctx *exec_ctx,
		grpc_subchannel_key *key) {
	enter_ctx(exec_ctx);

	gpr_mu_lock(&g_mu);
	gpr_avl index = gpr_avl_ref(g_subchannel_index);
	gpr_mu_unlock(&g_mu);

	grpc_subchannel *c = GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(gpr_avl_get(index, key), "index_find");
	gpr_avl_unref(index);

	leave_ctx(exec_ctx);
	return c;
}

grpc_subchannel *grpc_subchannel_index_register(
	  grpc_exec_ctx *exec_ctx,
		grpc_subchannel_key *key, 
		grpc_subchannel *constructed) {
	enter_ctx(exec_ctx);

	grpc_subchannel *c = NULL;

	while (c == NULL) {
		gpr_mu_lock(&g_mu);
		gpr_avl index = gpr_avl_ref(g_subchannel_index);
		gpr_mu_unlock(&g_mu);

		c = gpr_avl_get(index, key);
		if (c != NULL) {
			GRPC_SUBCHANNEL_WEAK_UNREF(exec_ctx, constructed, "index_register");
		} else {
			gpr_avl updated = gpr_avl_add(index, key, constructed);

			gpr_mu_lock(&g_mu);
			if (index.root == g_subchannel_index.root) {
				GPR_SWAP(gpr_avl, updated, g_subchannel_index);
				c = constructed;
			}
			gpr_mu_unlock(&g_mu);
		}
		gpr_avl_unref(index);
	}

	leave_ctx(exec_ctx);

	return c;
}

void grpc_subchannel_index_unregister(
    grpc_exec_ctx *exec_ctx,
    grpc_subchannel_key *key,
    grpc_subchannel *constructed) {
	enter_ctx(exec_ctx);

	bool done = false;
	while (!done) {
		gpr_mu_lock(&g_mu);
		gpr_avl index = gpr_avl_ref(g_subchannel_index);
		gpr_mu_unlock(&g_mu);

		grpc_subchannel *c = gpr_avl_get(index, key);
		if (c != constructed) {
			break;
		}

		gpr_avl updated = gpr_avl_remove(index, key);

		gpr_mu_lock(&g_mu);
		if (index.root == g_subchannel_index.root) {
			GPR_SWAP(gpr_avl, updated, g_subchannel_index);
			done = true;
		} else {
			GPR_SWAP(gpr_avl, updated, index);
		}
		gpr_mu_unlock(&g_mu);

		gpr_avl_unref(index);
	}

	leave_ctx(exec_ctx);
}
