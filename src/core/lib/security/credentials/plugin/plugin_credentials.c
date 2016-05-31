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

#include "src/core/lib/security/credentials/plugin/plugin_credentials.h"

#include <string.h>

#include "src/core/lib/surface/api_trace.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

typedef struct {
  void *user_data;
  grpc_credentials_metadata_cb cb;
} grpc_metadata_plugin_request;

static void plugin_destruct(grpc_call_credentials *creds) {
  grpc_plugin_credentials *c = (grpc_plugin_credentials *)creds;
  if (c->plugin.state != NULL && c->plugin.destroy != NULL) {
    c->plugin.destroy(c->plugin.state);
  }
}

static void plugin_md_request_metadata_ready(void *request,
                                             const grpc_metadata *md,
                                             size_t num_md,
                                             grpc_status_code status,
                                             const char *error_details) {
  /* called from application code */
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_metadata_plugin_request *r = (grpc_metadata_plugin_request *)request;
  if (status != GRPC_STATUS_OK) {
    if (error_details != NULL) {
      gpr_log(GPR_ERROR, "Getting metadata from plugin failed with error: %s",
              error_details);
    }
    r->cb(&exec_ctx, r->user_data, NULL, 0, GRPC_CREDENTIALS_ERROR);
  } else {
    size_t i;
    grpc_credentials_md *md_array = NULL;
    if (num_md > 0) {
      md_array = gpr_malloc(num_md * sizeof(grpc_credentials_md));
      for (i = 0; i < num_md; i++) {
        md_array[i].key = gpr_slice_from_copied_string(md[i].key);
        md_array[i].value =
            gpr_slice_from_copied_buffer(md[i].value, md[i].value_length);
      }
    }
    r->cb(&exec_ctx, r->user_data, md_array, num_md, GRPC_CREDENTIALS_OK);
    if (md_array != NULL) {
      for (i = 0; i < num_md; i++) {
        gpr_slice_unref(md_array[i].key);
        gpr_slice_unref(md_array[i].value);
      }
      gpr_free(md_array);
    }
  }
  gpr_free(r);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void plugin_get_request_metadata(grpc_exec_ctx *exec_ctx,
                                        grpc_call_credentials *creds,
                                        grpc_polling_entity *pollent,
                                        grpc_auth_metadata_context context,
                                        grpc_credentials_metadata_cb cb,
                                        void *user_data) {
  grpc_plugin_credentials *c = (grpc_plugin_credentials *)creds;
  if (c->plugin.get_metadata != NULL) {
    grpc_metadata_plugin_request *request = gpr_malloc(sizeof(*request));
    memset(request, 0, sizeof(*request));
    request->user_data = user_data;
    request->cb = cb;
    c->plugin.get_metadata(c->plugin.state, context,
                           plugin_md_request_metadata_ready, request);
  } else {
    cb(exec_ctx, user_data, NULL, 0, GRPC_CREDENTIALS_OK);
  }
}

static grpc_call_credentials_vtable plugin_vtable = {
    plugin_destruct, plugin_get_request_metadata};

grpc_call_credentials *grpc_metadata_credentials_create_from_plugin(
    grpc_metadata_credentials_plugin plugin, void *reserved) {
  grpc_plugin_credentials *c = gpr_malloc(sizeof(*c));
  GRPC_API_TRACE("grpc_metadata_credentials_create_from_plugin(reserved=%p)", 1,
                 (reserved));
  GPR_ASSERT(reserved == NULL);
  memset(c, 0, sizeof(*c));
  c->base.type = plugin.type;
  c->base.vtable = &plugin_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  c->plugin = plugin;
  return &c->base;
}
