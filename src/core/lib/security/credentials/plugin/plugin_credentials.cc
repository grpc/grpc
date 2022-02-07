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

#include <atomic>

#include "absl/strings/str_cat.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/validate_metadata.h"

grpc_core::TraceFlag grpc_plugin_credentials_trace(false, "plugin_credentials");

grpc_plugin_credentials::~grpc_plugin_credentials() {
  if (plugin_.state != nullptr && plugin_.destroy != nullptr) {
    plugin_.destroy(plugin_.state);
  }
}

std::string grpc_plugin_credentials::debug_string() {
  char* debug_c_str = nullptr;
  if (plugin_.debug_string != nullptr) {
    debug_c_str = plugin_.debug_string(plugin_.state);
  }
  std::string debug_str(
      debug_c_str != nullptr
          ? debug_c_str
          : "grpc_plugin_credentials did not provide a debug string");
  gpr_free(debug_c_str);
  return debug_str;
}

static absl::StatusOr<grpc_core::ClientInitialMetadata> process_plugin_result(
    grpc_plugin_credentials::pending_request* r, const grpc_metadata* md,
    size_t num_md, grpc_status_code status, const char* error_details) {
  if (status != GRPC_STATUS_OK) {
    return absl::UnavailableError(absl::StrCat(
        "Getting metadata from plugin failed with error: ", error_details));
  } else {
    bool seen_illegal_header = false;
    for (size_t i = 0; i < num_md; ++i) {
      if (!GRPC_LOG_IF_ERROR("validate_metadata_from_plugin",
                             grpc_validate_header_key_is_legal(md[i].key))) {
        seen_illegal_header = true;
        break;
      } else if (!grpc_is_binary_header_internal(md[i].key) &&
                 !GRPC_LOG_IF_ERROR(
                     "validate_metadata_from_plugin",
                     grpc_validate_header_nonbin_value_is_legal(md[i].value))) {
        gpr_log(GPR_ERROR, "Plugin added invalid metadata value.");
        seen_illegal_header = true;
        break;
      }
    }
    if (seen_illegal_header) {
      return absl::UnavailableError("Illegal metadata");
    } else {
      absl::Status error;
      for (size_t i = 0; i < num_md; ++i) {
        r->md->Append(
            grpc_core::StringViewFromSlice(md[i].key),
            grpc_core::Slice(grpc_slice_ref_internal(md[i].value)),
            [&error](absl::string_view message, const grpc_core::Slice&) {
              error = absl::UnavailableError(message);
            });
      }
      if (!error.ok()) return std::move(error);
      return grpc_core::ClientInitialMetadata(std::move(r->md));
    }
  }
}

static void plugin_md_request_metadata_ready(void* request,
                                             const grpc_metadata* md,
                                             size_t num_md,
                                             grpc_status_code status,
                                             const char* error_details) {
  /* called from application code */
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx(GRPC_EXEC_CTX_FLAG_IS_FINISHED |
                              GRPC_EXEC_CTX_FLAG_THREAD_RESOURCE_LOOP);
  grpc_core::RefCountedPtr<grpc_plugin_credentials::pending_request> r(
      static_cast<grpc_plugin_credentials::pending_request*>(request));
  if (GRPC_TRACE_FLAG_ENABLED(grpc_plugin_credentials_trace)) {
    gpr_log(GPR_INFO,
            "plugin_credentials[%p]: request %p: plugin returned "
            "asynchronously",
            r->creds, r.get());
  }
  for (size_t i = 0; i < num_md; i++) {
    r->metadata.push_back(grpc_metadata{grpc_slice_ref_internal(md[i].key),
                                        grpc_slice_ref_internal(md[i].value)});
  }
  r->error_details = error_details == nullptr ? "" : error_details;
  r->status = status;
  r->ready.store(true, std::memory_order_release);
  r->waker.Wakeup();
}

grpc_core::ArenaPromise<absl::StatusOr<grpc_core::ClientInitialMetadata>>
grpc_plugin_credentials::GetRequestMetadata(
    grpc_core::ClientInitialMetadata initial_metadata,
    grpc_core::AuthMetadataContext* auth_metadata_context) {
  if (plugin_.get_metadata == nullptr) {
    return grpc_core::Immediate(std::move(initial_metadata));
  }

  // Create pending_request object.
  auto request = grpc_core::MakeRefCounted<pending_request>();
  request->ready = false;
  request->waker = grpc_core::Activity::current()->MakeNonOwningWaker();
  request->creds = this;
  request->call_creds = Ref();
  request->context = auth_metadata_context->MakeLegacyContext(initial_metadata);
  request->md = std::move(initial_metadata);
  // Invoke the plugin.  The callback holds a ref to us.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_plugin_credentials_trace)) {
    gpr_log(GPR_INFO, "plugin_credentials[%p]: request %p: invoking plugin",
            this, request.get());
  }
  grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX];
  size_t num_creds_md = 0;
  grpc_status_code status = GRPC_STATUS_OK;
  const char* error_details = nullptr;
  auto child_request = request->Ref();
  if (!plugin_.get_metadata(plugin_.state, request->context,
                            plugin_md_request_metadata_ready,
                            child_request.get(), creds_md, &num_creds_md,
                            &status, &error_details)) {
    child_request.release();
    if (GRPC_TRACE_FLAG_ENABLED(grpc_plugin_credentials_trace)) {
      gpr_log(GPR_INFO,
              "plugin_credentials[%p]: request %p: plugin will return "
              "asynchronously",
              this, request.get());
    }
    return [request]() -> grpc_core::Poll<
                           absl::StatusOr<grpc_core::ClientInitialMetadata>> {
      if (!request->ready.load(std::memory_order_acquire)) {
        return grpc_core::Pending{};
      }
      auto result = process_plugin_result(
          request.get(), request->metadata.data(), request->metadata.size(),
          request->status, request->error_details.c_str());
      return std::move(result);
    };
  }
  // Synchronous return.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_plugin_credentials_trace)) {
    gpr_log(GPR_INFO,
            "plugin_credentials[%p]: request %p: plugin returned "
            "synchronously",
            this, request.get());
  }
  auto result = process_plugin_result(request.get(), creds_md, num_creds_md,
                                      status, error_details);
  // Clean up.
  for (size_t i = 0; i < num_creds_md; ++i) {
    grpc_slice_unref_internal(creds_md[i].key);
    grpc_slice_unref_internal(creds_md[i].value);
  }
  gpr_free(const_cast<char*>(error_details));

  return grpc_core::Immediate(std::move(result));
}

grpc_plugin_credentials::grpc_plugin_credentials(
    grpc_metadata_credentials_plugin plugin,
    grpc_security_level min_security_level)
    : grpc_call_credentials(plugin.type, min_security_level), plugin_(plugin) {}

grpc_call_credentials* grpc_metadata_credentials_create_from_plugin(
    grpc_metadata_credentials_plugin plugin,
    grpc_security_level min_security_level, void* reserved) {
  GRPC_API_TRACE("grpc_metadata_credentials_create_from_plugin(reserved=%p)", 1,
                 (reserved));
  GPR_ASSERT(reserved == nullptr);
  return new grpc_plugin_credentials(plugin, min_security_level);
}
