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

#include <grpc/support/port_platform.h>

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

grpc_core::TraceFlag grpc_plugin_credentials_trace(false, "plugin_credentials");

grpc_plugin_credentials::~grpc_plugin_credentials() {
  gpr_mu_destroy(&mu_);
  if (plugin_.state != nullptr && plugin_.destroy != nullptr) {
    plugin_.destroy(plugin_.state);
  }
}

void grpc_plugin_credentials::pending_request_remove_locked(
    pending_request* pending_request) {
  if (pending_request->prev == nullptr) {
    pending_requests_ = pending_request->next;
  } else {
    pending_request->prev->next = pending_request->next;
  }
  if (pending_request->next != nullptr) {
    pending_request->next->prev = pending_request->prev;
  }
}

// Checks if the request has been cancelled.
// If not, removes it from the pending list, so that it cannot be
// cancelled out from under us.
// When this returns, r->cancelled indicates whether the request was
// cancelled before completion.
void grpc_plugin_credentials::pending_request_complete(pending_request* r) {
  GPR_DEBUG_ASSERT(r->creds == this);
  gpr_mu_lock(&mu_);
  if (!r->cancelled) pending_request_remove_locked(r);
  gpr_mu_unlock(&mu_);
  // Ref to credentials not needed anymore.
  Unref();
}

static grpc_error* process_plugin_result(
    grpc_plugin_credentials::pending_request* r, const grpc_metadata* md,
    size_t num_md, grpc_status_code status, const char* error_details) {
  grpc_error* error = GRPC_ERROR_NONE;
  if (status != GRPC_STATUS_OK) {
    char* msg;
    gpr_asprintf(&msg, "Getting metadata from plugin failed with error: %s",
                 error_details);
    error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
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
      error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Illegal metadata");
    } else {
      for (size_t i = 0; i < num_md; ++i) {
        grpc_mdelem mdelem =
            grpc_mdelem_create(md[i].key, md[i].value, nullptr);
        grpc_credentials_mdelem_array_add(r->md_array, mdelem);
        GRPC_MDELEM_UNREF(mdelem);
      }
    }
  }
  return error;
}

static void plugin_md_request_metadata_ready(void* request,
                                             const grpc_metadata* md,
                                             size_t num_md,
                                             grpc_status_code status,
                                             const char* error_details) {
  /* called from application code */
  grpc_core::ExecCtx exec_ctx(GRPC_EXEC_CTX_FLAG_IS_FINISHED |
                              GRPC_EXEC_CTX_FLAG_THREAD_RESOURCE_LOOP);
  grpc_plugin_credentials::pending_request* r =
      static_cast<grpc_plugin_credentials::pending_request*>(request);
  if (grpc_plugin_credentials_trace.enabled()) {
    gpr_log(GPR_INFO,
            "plugin_credentials[%p]: request %p: plugin returned "
            "asynchronously",
            r->creds, r);
  }
  // Remove request from pending list if not previously cancelled.
  r->creds->pending_request_complete(r);
  // If it has not been cancelled, process it.
  if (!r->cancelled) {
    grpc_error* error =
        process_plugin_result(r, md, num_md, status, error_details);
    GRPC_CLOSURE_SCHED(r->on_request_metadata, error);
  } else if (grpc_plugin_credentials_trace.enabled()) {
    gpr_log(GPR_INFO,
            "plugin_credentials[%p]: request %p: plugin was previously "
            "cancelled",
            r->creds, r);
  }
  gpr_free(r);
}

bool grpc_plugin_credentials::get_request_metadata(
    grpc_polling_entity* pollent, grpc_auth_metadata_context context,
    grpc_credentials_mdelem_array* md_array, grpc_closure* on_request_metadata,
    grpc_error** error) {
  bool retval = true;  // Synchronous return.
  if (plugin_.get_metadata != nullptr) {
    // Create pending_request object.
    pending_request* request =
        static_cast<pending_request*>(gpr_zalloc(sizeof(*request)));
    request->creds = this;
    request->md_array = md_array;
    request->on_request_metadata = on_request_metadata;
    // Add it to the pending list.
    gpr_mu_lock(&mu_);
    if (pending_requests_ != nullptr) {
      pending_requests_->prev = request;
    }
    request->next = pending_requests_;
    pending_requests_ = request;
    gpr_mu_unlock(&mu_);
    // Invoke the plugin.  The callback holds a ref to us.
    if (grpc_plugin_credentials_trace.enabled()) {
      gpr_log(GPR_INFO, "plugin_credentials[%p]: request %p: invoking plugin",
              this, request);
    }
    Ref().release();
    grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX];
    size_t num_creds_md = 0;
    grpc_status_code status = GRPC_STATUS_OK;
    const char* error_details = nullptr;
    if (!plugin_.get_metadata(
            plugin_.state, context, plugin_md_request_metadata_ready, request,
            creds_md, &num_creds_md, &status, &error_details)) {
      if (grpc_plugin_credentials_trace.enabled()) {
        gpr_log(GPR_INFO,
                "plugin_credentials[%p]: request %p: plugin will return "
                "asynchronously",
                this, request);
      }
      return false;  // Asynchronous return.
    }
    // Returned synchronously.
    // Remove request from pending list if not previously cancelled.
    request->creds->pending_request_complete(request);
    // If the request was cancelled, the error will have been returned
    // asynchronously by plugin_cancel_get_request_metadata(), so return
    // false.  Otherwise, process the result.
    if (request->cancelled) {
      if (grpc_plugin_credentials_trace.enabled()) {
        gpr_log(GPR_INFO,
                "plugin_credentials[%p]: request %p was cancelled, error "
                "will be returned asynchronously",
                this, request);
      }
      retval = false;
    } else {
      if (grpc_plugin_credentials_trace.enabled()) {
        gpr_log(GPR_INFO,
                "plugin_credentials[%p]: request %p: plugin returned "
                "synchronously",
                this, request);
      }
      *error = process_plugin_result(request, creds_md, num_creds_md, status,
                                     error_details);
    }
    // Clean up.
    for (size_t i = 0; i < num_creds_md; ++i) {
      grpc_slice_unref_internal(creds_md[i].key);
      grpc_slice_unref_internal(creds_md[i].value);
    }
    gpr_free((void*)error_details);
    gpr_free(request);
  }
  return retval;
}

void grpc_plugin_credentials::cancel_get_request_metadata(
    grpc_credentials_mdelem_array* md_array, grpc_error* error) {
  gpr_mu_lock(&mu_);
  for (pending_request* pending_request = pending_requests_;
       pending_request != nullptr; pending_request = pending_request->next) {
    if (pending_request->md_array == md_array) {
      if (grpc_plugin_credentials_trace.enabled()) {
        gpr_log(GPR_INFO, "plugin_credentials[%p]: cancelling request %p", this,
                pending_request);
      }
      pending_request->cancelled = true;
      GRPC_CLOSURE_SCHED(pending_request->on_request_metadata,
                         GRPC_ERROR_REF(error));
      pending_request_remove_locked(pending_request);
      break;
    }
  }
  gpr_mu_unlock(&mu_);
  GRPC_ERROR_UNREF(error);
}

grpc_plugin_credentials::grpc_plugin_credentials(
    grpc_metadata_credentials_plugin plugin)
    : grpc_call_credentials(plugin.type), plugin_(plugin) {
  gpr_mu_init(&mu_);
}

grpc_call_credentials* grpc_metadata_credentials_create_from_plugin(
    grpc_metadata_credentials_plugin plugin, void* reserved) {
  GRPC_API_TRACE("grpc_metadata_credentials_create_from_plugin(reserved=%p)", 1,
                 (reserved));
  GPR_ASSERT(reserved == nullptr);
  return grpc_core::New<grpc_plugin_credentials>(plugin);
}
