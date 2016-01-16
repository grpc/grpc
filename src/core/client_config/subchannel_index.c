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

/* a map of subchannel_key --> subchannel, used for detecting connections
   to the same destination in order to share them */
static gpr_avl g_subchannel_index;

static gpr_mu g_mu;

struct subchannel_key {
  size_t addr_len;
  struct sockaddr *addr;
  grpc_channel_args *normalized_args;
};

GPR_TLS_DECL(subchannel_index_exec_ctx);

static subchannel_key *subchannel_key_create(struct sockaddr *sockaddr, size_t addr_len, grpc_channel_args *args) {
  subchannel_key *k = gpr_malloc(sizeof(*k));
  k->addr_len = addr_len;
  k->addr = gpr_malloc(addr_len);
  memcpy(k->addr, addr, addr_len);
  k->normalized_args = grpc_channel_args_normalize(args);
  return k;
}

static subchannel_key *subchannel_key_copy(subchannel_key *k) {
  subchannel_key *k = gpr_malloc(sizeof(*k));
  k->addr_len = addr_len;
  k->addr = gpr_malloc(addr_len);
  memcpy(k->addr, addr, addr_len);
  k->normalized_args = grpc_channel_args_copy(args);
  return k;
}

static int subchannel_key_compare(subchannel_key *a, subchannel_key *b) {
  int c = GPR_ICMP(a->addr_len, b->addr_len);
  if (c != 0) return c;
  c = memcmp(a->addr, b->addr, a->addr_len);
  if (c != 0) return c;
  return grpc_channel_args_compare(a->normalized_args, b->normalized_args);
}

static void subchannel_key_destroy(subchannel_key *k) {
  gpr_free(k->addr);
  grpc_channel_args_destroy(k->normalized_args);
  gpr_free(k);
}

static void sck_avl_destroy(void *p) {
  subchannel_key_destroy(p);
}

static void *sck_avl_copy(void *p) {
  return subchannel_key_copy(p);
}

static void *sck_avl_compare(void *a, void *b) {
  return subchannel_key_compare(a, b);
}

static void scv_avl_destroy(void *p) {
  GRPC_SUBCHANNEL_UNREF(exec_ctx, p, "subchannel_index");
}

static void *scv_avl_copy(void *p) { 
  GRPC_SUBCHANNEL_REF(p, "subchannel_index");
  return p; 
}

static const gpr_avl_vtable subchannel_avl_vtable = {
  .destroy_key = sck_avl_destroy,
  .copy_key = sck_avl_copy,
  .compare_keys = sck_avl_compare,
  .destroy_value = scv_avl_destroy,
  .copy_value = scv_avl_copy  
};

grpc_subchannel *grpc_subchannel_index_find(
		grpc_exec_ctx *ctx,
		grpc_connector *connector,
		grpc_subchannel_args *args) {
	gpr_mu_lock(&g_mu);
	gpr_avl index = gpr_avl_ref(g_subchannel_index);
	gpr_mu_unlock(&g_mu);

	subchannel_key *key = subchannel_key_create(connector, args);
	grpc_subchannel *c = grpc_subchannel_ref(gpr_avl_get(index, key));
	subchannel_key_destroy(key);
	gpr_avl_unref(index);

	return c;
}

grpc_subchannel *grpc_subchannel_index_register(
	  grpc_exec_ctx *ctx,
		grpc_connector *connector, 
		grpc_subchannel_args *args, 
		grpc_subchannel *constructed) {
	subchannel_key *key = subchannel_key_create(connector, args);
	grpc_subchannel *c = NULL;

	while (c == NULL) {
		gpr_mu_lock(&g_mu);
		gpr_avl index = gpr_avl_ref(g_subchannel_index);
		gpr_mu_unlock(&g_mu);

		c = gpr_avl_get(index, key);
		if (c != NULL) {
			GRPC_SUBCHANNEL_UNREF(constructed);
		} else {
			gpr_avl updated = gpr_avl_add(index, key, constructed);

			gpr_mu_lock(&g_mu);
			if (index.root == g_subchannel_index.root) {
				GPR_SWAP(index, g_subchannel_index);
				c = constructed;
			}
			gpr_mu_unlock(&g_mu);
		}
		gpr_avl_unref(index);
	}

	return c;
}
