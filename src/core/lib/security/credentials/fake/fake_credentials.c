/*
 *
 * Copyright 2016, Google Inc.
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

#include "src/core/lib/security/credentials/fake/fake_credentials.h"

#include <string.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/executor.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

/* -- Fake transport security credentials. -- */

static grpc_security_status fake_transport_security_create_security_connector(
    grpc_channel_credentials *c, grpc_call_credentials *call_creds,
    const char *target, const grpc_channel_args *args,
    grpc_channel_security_connector **sc, grpc_channel_args **new_args) {
  *sc = grpc_fake_channel_security_connector_create(call_creds);
  return GRPC_SECURITY_OK;
}

static grpc_security_status
fake_transport_security_server_create_security_connector(
    grpc_server_credentials *c, grpc_server_security_connector **sc) {
  *sc = grpc_fake_server_security_connector_create();
  return GRPC_SECURITY_OK;
}

static grpc_channel_credentials_vtable
    fake_transport_security_credentials_vtable = {
        NULL, fake_transport_security_create_security_connector};

static grpc_server_credentials_vtable
    fake_transport_security_server_credentials_vtable = {
        NULL, fake_transport_security_server_create_security_connector};

grpc_channel_credentials *grpc_fake_transport_security_credentials_create(
    void) {
  grpc_channel_credentials *c = gpr_malloc(sizeof(grpc_channel_credentials));
  memset(c, 0, sizeof(grpc_channel_credentials));
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

/* -- Metadata-only test credentials. -- */

static void md_only_test_destruct(grpc_call_credentials *creds) {
  grpc_md_only_test_credentials *c = (grpc_md_only_test_credentials *)creds;
  grpc_credentials_md_store_unref(c->md_store);
}

static void on_simulated_token_fetch_done(grpc_exec_ctx *exec_ctx,
                                          void *user_data, bool success) {
  grpc_credentials_metadata_request *r =
      (grpc_credentials_metadata_request *)user_data;
  grpc_md_only_test_credentials *c = (grpc_md_only_test_credentials *)r->creds;
  r->cb(exec_ctx, r->user_data, c->md_store->entries, c->md_store->num_entries,
        GRPC_CREDENTIALS_OK);
  grpc_credentials_metadata_request_destroy(r);
}

static void md_only_test_get_request_metadata(
    grpc_exec_ctx *exec_ctx, grpc_call_credentials *creds,
    grpc_polling_entity *pollent, grpc_auth_metadata_context context,
    grpc_credentials_metadata_cb cb, void *user_data) {
  grpc_md_only_test_credentials *c = (grpc_md_only_test_credentials *)creds;

  if (c->is_async) {
    grpc_credentials_metadata_request *cb_arg =
        grpc_credentials_metadata_request_create(creds, cb, user_data);
    grpc_executor_enqueue(
        grpc_closure_create(on_simulated_token_fetch_done, cb_arg), true);
  } else {
    cb(exec_ctx, user_data, c->md_store->entries, 1, GRPC_CREDENTIALS_OK);
  }
}

static grpc_call_credentials_vtable md_only_test_vtable = {
    md_only_test_destruct, md_only_test_get_request_metadata};

grpc_call_credentials *grpc_md_only_test_credentials_create(
    const char *md_key, const char *md_value, int is_async) {
  grpc_md_only_test_credentials *c =
      gpr_malloc(sizeof(grpc_md_only_test_credentials));
  memset(c, 0, sizeof(grpc_md_only_test_credentials));
  c->base.type = GRPC_CALL_CREDENTIALS_TYPE_OAUTH2;
  c->base.vtable = &md_only_test_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  c->md_store = grpc_credentials_md_store_create(1);
  grpc_credentials_md_store_add_cstrings(c->md_store, md_key, md_value);
  c->is_async = is_async;
  return &c->base;
}
