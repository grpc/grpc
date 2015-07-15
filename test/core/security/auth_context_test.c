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

#include "src/core/security/security_context.h"
#include "src/core/support/string.h"
#include "test/core/util/test_config.h"

#include <grpc/support/log.h>

static void test_empty_context(void) {
  grpc_auth_context *ctx = grpc_auth_context_create(NULL, 0);
  grpc_auth_property_iterator it;

  gpr_log(GPR_INFO, "test_empty_context");
  GPR_ASSERT(ctx != NULL);
  GPR_ASSERT(grpc_auth_context_peer_identity_property_name(ctx) == NULL);
  it = grpc_auth_context_peer_identity(ctx);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == NULL);
  it = grpc_auth_context_property_iterator(ctx);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == NULL);
  it = grpc_auth_context_find_properties_by_name(ctx, "foo");
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == NULL);
  GRPC_AUTH_CONTEXT_UNREF(ctx, "test");
}

static void test_simple_context(void) {
  grpc_auth_context *ctx = grpc_auth_context_create(NULL, 3);
  grpc_auth_property_iterator it;
  size_t i;

  gpr_log(GPR_INFO, "test_simple_context");
  GPR_ASSERT(ctx != NULL);
  GPR_ASSERT(ctx->property_count == 3);
  ctx->properties[0] = grpc_auth_property_init_from_cstring("name", "chapi");
  ctx->properties[1] = grpc_auth_property_init_from_cstring("name", "chapo");
  ctx->properties[2] = grpc_auth_property_init_from_cstring("foo", "bar");
  ctx->peer_identity_property_name = ctx->properties[0].name;

  GPR_ASSERT(
      strcmp(grpc_auth_context_peer_identity_property_name(ctx), "name") == 0);
  it = grpc_auth_context_property_iterator(ctx);
  for (i = 0; i < ctx->property_count; i++) {
    const grpc_auth_property *p = grpc_auth_property_iterator_next(&it);
    GPR_ASSERT(p == &ctx->properties[i]);
  }
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == NULL);

  it = grpc_auth_context_find_properties_by_name(ctx, "foo");
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == &ctx->properties[2]);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == NULL);

  it = grpc_auth_context_peer_identity(ctx);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == &ctx->properties[0]);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == &ctx->properties[1]);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == NULL);

  GRPC_AUTH_CONTEXT_UNREF(ctx, "test");
}

static void test_chained_context(void) {
  grpc_auth_context *chained = grpc_auth_context_create(NULL, 2);
  grpc_auth_context *ctx = grpc_auth_context_create(chained, 3);
  grpc_auth_property_iterator it;
  size_t i;

  gpr_log(GPR_INFO, "test_chained_context");
  GRPC_AUTH_CONTEXT_UNREF(chained, "chained");
  chained->properties[0] =
      grpc_auth_property_init_from_cstring("name", "padapo");
  chained->properties[1] = grpc_auth_property_init_from_cstring("foo", "baz");
  ctx->properties[0] = grpc_auth_property_init_from_cstring("name", "chapi");
  ctx->properties[1] = grpc_auth_property_init_from_cstring("name", "chapo");
  ctx->properties[2] = grpc_auth_property_init_from_cstring("foo", "bar");
  ctx->peer_identity_property_name = ctx->properties[0].name;

  GPR_ASSERT(
      strcmp(grpc_auth_context_peer_identity_property_name(ctx), "name") == 0);
  it = grpc_auth_context_property_iterator(ctx);
  for (i = 0; i < ctx->property_count; i++) {
    const grpc_auth_property *p = grpc_auth_property_iterator_next(&it);
    GPR_ASSERT(p == &ctx->properties[i]);
  }
  for (i = 0; i < chained->property_count; i++) {
    const grpc_auth_property *p = grpc_auth_property_iterator_next(&it);
    GPR_ASSERT(p == &chained->properties[i]);
  }
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == NULL);

  it = grpc_auth_context_find_properties_by_name(ctx, "foo");
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == &ctx->properties[2]);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == &chained->properties[1]);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == NULL);

  it = grpc_auth_context_peer_identity(ctx);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == &ctx->properties[0]);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == &ctx->properties[1]);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == &chained->properties[0]);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == NULL);

  GRPC_AUTH_CONTEXT_UNREF(ctx, "test");
}


int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_empty_context();
  test_simple_context();
  test_chained_context();
  return 0;
}

