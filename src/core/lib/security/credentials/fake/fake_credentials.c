/*
 *
 * Copyright 2016 gRPC authors.
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

#include "src/core/lib/security/credentials/fake/fake_credentials.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/support/string.h"

/* -- Fake transport security credentials. -- */

#define GRPC_ARG_FAKE_SECURITY_EXPECTED_TARGETS \
  "grpc.fake_security.expected_targets"

static grpc_security_status fake_transport_security_create_security_connector(
    grpc_exec_ctx *exec_ctx, grpc_channel_credentials *c,
    grpc_call_credentials *call_creds, const char *target,
    const grpc_channel_args *args, grpc_channel_security_connector **sc,
    grpc_channel_args **new_args) {
  *sc = grpc_fake_channel_security_connector_create(call_creds, target, args);
  return GRPC_SECURITY_OK;
}

static grpc_security_status
fake_transport_security_server_create_security_connector(
    grpc_exec_ctx *exec_ctx, grpc_server_credentials *c,
    grpc_server_security_connector **sc) {
  *sc = grpc_fake_server_security_connector_create();
  return GRPC_SECURITY_OK;
}

static grpc_channel_credentials_vtable
    fake_transport_security_credentials_vtable = {
        NULL, fake_transport_security_create_security_connector, NULL};

static grpc_server_credentials_vtable
    fake_transport_security_server_credentials_vtable = {
        NULL, fake_transport_security_server_create_security_connector};

grpc_channel_credentials *grpc_fake_transport_security_credentials_create(
    void) {
  grpc_channel_credentials *c = gpr_zalloc(sizeof(grpc_channel_credentials));
  c->type = GRPC_CHANNEL_CREDENTIALS_TYPE_FAKE_TRANSPORT_SECURITY;
  c->vtable = &fake_transport_security_credentials_vtable;
  gpr_ref_init(&c->refcount, 1);
  return c;
}

grpc_server_credentials *grpc_fake_transport_security_server_credentials_create(
    void) {
  grpc_server_credentials *c = gpr_malloc(sizeof(grpc_server_credentials));
  memset(c, 0, sizeof(grpc_server_credentials));
  c->type = GRPC_CHANNEL_CREDENTIALS_TYPE_FAKE_TRANSPORT_SECURITY;
  gpr_ref_init(&c->refcount, 1);
  c->vtable = &fake_transport_security_server_credentials_vtable;
  return c;
}

grpc_arg grpc_fake_transport_expected_targets_arg(char *expected_targets) {
  return grpc_channel_arg_string_create(GRPC_ARG_FAKE_SECURITY_EXPECTED_TARGETS,
                                        expected_targets);
}

const char *grpc_fake_transport_get_expected_targets(
    const grpc_channel_args *args) {
  const grpc_arg *expected_target_arg =
      grpc_channel_args_find(args, GRPC_ARG_FAKE_SECURITY_EXPECTED_TARGETS);
  if (expected_target_arg != NULL &&
      expected_target_arg->type == GRPC_ARG_STRING) {
    return expected_target_arg->value.string;
  }
  return NULL;
}

/* -- Metadata-only test credentials. -- */

static void md_only_test_destruct(grpc_exec_ctx *exec_ctx,
                                  grpc_call_credentials *creds) {
  grpc_md_only_test_credentials *c = (grpc_md_only_test_credentials *)creds;
  grpc_credentials_md_store_unref(exec_ctx, c->md_store);
}

static void on_simulated_token_fetch_done(grpc_exec_ctx *exec_ctx,
                                          void *user_data, grpc_error *error) {
  grpc_credentials_metadata_request *r =
      (grpc_credentials_metadata_request *)user_data;
  grpc_md_only_test_credentials *c = (grpc_md_only_test_credentials *)r->creds;
  r->cb(exec_ctx, r->user_data, c->md_store->entries, c->md_store->num_entries,
        GRPC_CREDENTIALS_OK, NULL);
  grpc_credentials_metadata_request_destroy(exec_ctx, r);
}

static void md_only_test_get_request_metadata(
    grpc_exec_ctx *exec_ctx, grpc_call_credentials *creds,
    grpc_polling_entity *pollent, grpc_auth_metadata_context context,
    grpc_credentials_metadata_cb cb, void *user_data) {
  grpc_md_only_test_credentials *c = (grpc_md_only_test_credentials *)creds;

  if (c->is_async) {
    grpc_credentials_metadata_request *cb_arg =
        grpc_credentials_metadata_request_create(creds, cb, user_data);
    GRPC_CLOSURE_SCHED(exec_ctx,
                       GRPC_CLOSURE_CREATE(on_simulated_token_fetch_done,
                                           cb_arg, grpc_executor_scheduler),
                       GRPC_ERROR_NONE);
  } else {
    cb(exec_ctx, user_data, c->md_store->entries, 1, GRPC_CREDENTIALS_OK, NULL);
  }
}

static grpc_call_credentials_vtable md_only_test_vtable = {
    md_only_test_destruct, md_only_test_get_request_metadata};

grpc_call_credentials *grpc_md_only_test_credentials_create(
    const char *md_key, const char *md_value, int is_async) {
  grpc_md_only_test_credentials *c =
      gpr_zalloc(sizeof(grpc_md_only_test_credentials));
  c->base.type = GRPC_CALL_CREDENTIALS_TYPE_OAUTH2;
  c->base.vtable = &md_only_test_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  c->md_store = grpc_credentials_md_store_create(1);
  grpc_credentials_md_store_add_cstrings(c->md_store, md_key, md_value);
  c->is_async = is_async;
  return &c->base;
}
