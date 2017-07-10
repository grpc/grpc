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

#include "src/core/lib/security/credentials/plugin/plugin_credentials.h"

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/validate_metadata.h"

typedef struct {
  grpc_credentials_mdelem_list *md_list;
  grpc_closure *on_request_metadata;
} grpc_metadata_plugin_request;

static void plugin_destruct(grpc_exec_ctx *exec_ctx,
                            grpc_call_credentials *creds) {
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
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INITIALIZER(
      GRPC_EXEC_CTX_FLAG_IS_FINISHED | GRPC_EXEC_CTX_FLAG_THREAD_RESOURCE_LOOP,
      NULL, NULL);
  grpc_metadata_plugin_request *r = (grpc_metadata_plugin_request *)request;
  if (status != GRPC_STATUS_OK) {
    char *msg;
    gpr_asprintf(&msg, "Getting metadata from plugin failed with error: %s",
                 error_details);
    GRPC_CLOSURE_SCHED(&exec_ctx, r->on_request_metadata,
                       GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg));
    gpr_free(msg);
  } else {
    bool seen_illegal_header = false;
    for (size_t i = 0; i < num_md; ++i) {
      if (!GRPC_LOG_IF_ERROR("validate_metadata_from_plugin",
                             grpc_validate_header_key_is_legal(md[i].key))) {
        seen_illegal_header = true;
        break;
      } else if (!grpc_is_binary_header(md[i].key) &&
                 !GRPC_LOG_IF_ERROR(
                     "validate_metadata_from_plugin",
                     grpc_validate_header_nonbin_value_is_legal(md[i].value))) {
        gpr_log(GPR_ERROR, "Plugin added invalid metadata value.");
        seen_illegal_header = true;
        break;
      }
    }
    if (seen_illegal_header) {
      GRPC_CLOSURE_SCHED(&exec_ctx, r->on_request_metadata,
                         GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                             "Illegal metadata"));
    } else {
      for (size_t i = 0; i < num_md; ++i) {
        grpc_mdelem mdelem =
            grpc_mdelem_from_slices(&exec_ctx, md[i].key, md[i].value);
        grpc_credentials_mdelem_list_add(r->md_list, mdelem);
        GRPC_MDELEM_UNREF(&exec_ctx, mdelem);
      }
      GRPC_CLOSURE_SCHED(&exec_ctx, r->on_request_metadata, GRPC_ERROR_NONE);
    }
  }
  gpr_free(r);
  grpc_exec_ctx_finish(&exec_ctx);
}

static bool plugin_get_request_metadata(grpc_exec_ctx *exec_ctx,
                                        grpc_call_credentials *creds,
                                        grpc_polling_entity *pollent,
                                        grpc_auth_metadata_context context,
                                        grpc_credentials_mdelem_list *md_list,
                                        grpc_closure *on_request_metadata,
                                        grpc_error **error) {
  grpc_plugin_credentials *c = (grpc_plugin_credentials *)creds;
  if (c->plugin.get_metadata != NULL) {
    grpc_metadata_plugin_request *request = gpr_zalloc(sizeof(*request));
    request->md_list = md_list;
    request->on_request_metadata = on_request_metadata;
    c->plugin.get_metadata(c->plugin.state, context,
                           plugin_md_request_metadata_ready, request);
    return false;
  }
  return true;
}

static grpc_call_credentials_vtable plugin_vtable = {
    plugin_destruct, plugin_get_request_metadata};

grpc_call_credentials *grpc_metadata_credentials_create_from_plugin(
    grpc_metadata_credentials_plugin plugin, void *reserved) {
  grpc_plugin_credentials *c = gpr_zalloc(sizeof(*c));
  GRPC_API_TRACE("grpc_metadata_credentials_create_from_plugin(reserved=%p)", 1,
                 (reserved));
  GPR_ASSERT(reserved == NULL);
  c->base.type = plugin.type;
  c->base.vtable = &plugin_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  c->plugin = plugin;
  return &c->base;
}
