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

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/security/context/security_context.h"
#include "test/core/util/test_config.h"

#include <grpc/support/log.h>

static void test_empty_context(void) {
  grpc_auth_context* ctx = grpc_auth_context_create(nullptr);
  grpc_auth_property_iterator it;

  gpr_log(GPR_INFO, "test_empty_context");
  GPR_ASSERT(ctx != nullptr);
  GPR_ASSERT(grpc_auth_context_peer_identity_property_name(ctx) == nullptr);
  it = grpc_auth_context_peer_identity(ctx);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == nullptr);
  it = grpc_auth_context_property_iterator(ctx);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == nullptr);
  it = grpc_auth_context_find_properties_by_name(ctx, "foo");
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == nullptr);
  GPR_ASSERT(grpc_auth_context_set_peer_identity_property_name(ctx, "bar") ==
             0);
  GPR_ASSERT(grpc_auth_context_peer_identity_property_name(ctx) == nullptr);
  GRPC_AUTH_CONTEXT_UNREF(ctx, "test");
}

static void test_simple_context(void) {
  grpc_auth_context* ctx = grpc_auth_context_create(nullptr);
  grpc_auth_property_iterator it;
  size_t i;

  gpr_log(GPR_INFO, "test_simple_context");
  GPR_ASSERT(ctx != nullptr);
  grpc_auth_context_add_cstring_property(ctx, "name", "chapi");
  grpc_auth_context_add_cstring_property(ctx, "name", "chapo");
  grpc_auth_context_add_cstring_property(ctx, "foo", "bar");
  GPR_ASSERT(ctx->properties.count == 3);
  GPR_ASSERT(grpc_auth_context_set_peer_identity_property_name(ctx, "name") ==
             1);

  GPR_ASSERT(
      strcmp(grpc_auth_context_peer_identity_property_name(ctx), "name") == 0);
  it = grpc_auth_context_property_iterator(ctx);
  for (i = 0; i < ctx->properties.count; i++) {
    const grpc_auth_property* p = grpc_auth_property_iterator_next(&it);
    GPR_ASSERT(p == &ctx->properties.array[i]);
  }
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == nullptr);

  it = grpc_auth_context_find_properties_by_name(ctx, "foo");
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) ==
             &ctx->properties.array[2]);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == nullptr);

  it = grpc_auth_context_peer_identity(ctx);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) ==
             &ctx->properties.array[0]);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) ==
             &ctx->properties.array[1]);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == nullptr);

  GRPC_AUTH_CONTEXT_UNREF(ctx, "test");
}

static void test_chained_context(void) {
  grpc_auth_context* chained = grpc_auth_context_create(nullptr);
  grpc_auth_context* ctx = grpc_auth_context_create(chained);
  grpc_auth_property_iterator it;
  size_t i;

  gpr_log(GPR_INFO, "test_chained_context");
  GRPC_AUTH_CONTEXT_UNREF(chained, "chained");
  grpc_auth_context_add_cstring_property(chained, "name", "padapo");
  grpc_auth_context_add_cstring_property(chained, "foo", "baz");
  grpc_auth_context_add_cstring_property(ctx, "name", "chapi");
  grpc_auth_context_add_cstring_property(ctx, "name", "chap0");
  grpc_auth_context_add_cstring_property(ctx, "foo", "bar");
  GPR_ASSERT(grpc_auth_context_set_peer_identity_property_name(ctx, "name") ==
             1);

  GPR_ASSERT(
      strcmp(grpc_auth_context_peer_identity_property_name(ctx), "name") == 0);
  it = grpc_auth_context_property_iterator(ctx);
  for (i = 0; i < ctx->properties.count; i++) {
    const grpc_auth_property* p = grpc_auth_property_iterator_next(&it);
    GPR_ASSERT(p == &ctx->properties.array[i]);
  }
  for (i = 0; i < chained->properties.count; i++) {
    const grpc_auth_property* p = grpc_auth_property_iterator_next(&it);
    GPR_ASSERT(p == &chained->properties.array[i]);
  }
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == nullptr);

  it = grpc_auth_context_find_properties_by_name(ctx, "foo");
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) ==
             &ctx->properties.array[2]);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) ==
             &chained->properties.array[1]);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == nullptr);

  it = grpc_auth_context_peer_identity(ctx);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) ==
             &ctx->properties.array[0]);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) ==
             &ctx->properties.array[1]);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) ==
             &chained->properties.array[0]);
  GPR_ASSERT(grpc_auth_property_iterator_next(&it) == nullptr);

  GRPC_AUTH_CONTEXT_UNREF(ctx, "test");
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  test_empty_context();
  test_simple_context();
  test_chained_context();
  return 0;
}
