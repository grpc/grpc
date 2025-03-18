//
//
// Copyright 2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "src/core/credentials/call/plugin/plugin_credentials.h"

#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>

#include <atomic>
#include <memory>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/validate_metadata.h"

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

grpc_core::UniqueTypeName grpc_plugin_credentials::type() const {
  static grpc_core::UniqueTypeName::Factory kFactory("Plugin");
  return kFactory.Create();
}

absl::StatusOr<grpc_core::ClientMetadataHandle>
grpc_plugin_credentials::PendingRequest::ProcessPluginResult(
    const grpc_metadata* md, size_t num_md, grpc_status_code status,
    const char* error_details) {
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
        LOG(ERROR) << "Plugin added invalid metadata value.";
        seen_illegal_header = true;
        break;
      }
    }
    if (seen_illegal_header) {
      return absl::UnavailableError("Illegal metadata");
    } else {
      absl::Status error;
      for (size_t i = 0; i < num_md; ++i) {
        md_->Append(
            grpc_core::StringViewFromSlice(md[i].key),
            grpc_core::Slice(grpc_core::CSliceRef(md[i].value)),
            [&error](absl::string_view message, const grpc_core::Slice&) {
              error = absl::UnavailableError(message);
            });
      }
      if (!error.ok()) return std::move(error);
      return grpc_core::ClientMetadataHandle(std::move(md_));
    }
  }
}

grpc_core::Poll<absl::StatusOr<grpc_core::ClientMetadataHandle>>
grpc_plugin_credentials::PendingRequest::PollAsyncResult() {
  if (!ready_.load(std::memory_order_acquire)) {
    return grpc_core::Pending{};
  }
  return ProcessPluginResult(metadata_.data(), metadata_.size(), status_,
                             error_details_.c_str());
}

void grpc_plugin_credentials::PendingRequest::RequestMetadataReady(
    void* request, const grpc_metadata* md, size_t num_md,
    grpc_status_code status, const char* error_details) {
  // called from application code
  grpc_core::ExecCtx exec_ctx(GRPC_EXEC_CTX_FLAG_IS_FINISHED |
                              GRPC_EXEC_CTX_FLAG_THREAD_RESOURCE_LOOP);
  grpc_core::RefCountedPtr<grpc_plugin_credentials::PendingRequest> r(
      static_cast<grpc_plugin_credentials::PendingRequest*>(request));
  GRPC_TRACE_LOG(plugin_credentials, INFO)
      << "plugin_credentials[" << r->creds() << "]: request " << r.get()
      << ": plugin returned asynchronously";
  for (size_t i = 0; i < num_md; ++i) {
    grpc_metadata p;
    p.key = grpc_core::CSliceRef(md[i].key);
    p.value = grpc_core::CSliceRef(md[i].value);
    r->metadata_.push_back(p);
  }
  r->error_details_ = error_details == nullptr ? "" : error_details;
  r->status_ = status;
  r->ready_.store(true, std::memory_order_release);
  r->waker_.Wakeup();
}

grpc_core::ArenaPromise<absl::StatusOr<grpc_core::ClientMetadataHandle>>
grpc_plugin_credentials::GetRequestMetadata(
    grpc_core::ClientMetadataHandle initial_metadata,
    const grpc_call_credentials::GetRequestMetadataArgs* args) {
  if (plugin_.get_metadata == nullptr) {
    return grpc_core::Immediate(std::move(initial_metadata));
  }

  // Create pending_request object.
  auto request = grpc_core::MakeRefCounted<PendingRequest>(
      RefAsSubclass<grpc_plugin_credentials>(), std::move(initial_metadata),
      args);
  // Invoke the plugin.  The callback holds a ref to us.
  GRPC_TRACE_LOG(plugin_credentials, INFO)
      << "plugin_credentials[" << this << "]: request " << request.get()
      << ": invoking plugin";
  grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX];
  size_t num_creds_md = 0;
  grpc_status_code status = GRPC_STATUS_OK;
  const char* error_details = nullptr;
  // Add an extra ref to the request object for the async callback.
  // If the request completes synchronously, we'll drop this later.
  // If the request completes asynchronously, it will own a ref to the request
  // object (which we release from our ownership below).
  auto child_request = request->Ref();
  if (!plugin_.get_metadata(plugin_.state, request->context(),
                            PendingRequest::RequestMetadataReady,
                            child_request.get(), creds_md, &num_creds_md,
                            &status, &error_details)) {
    child_request.release();
    GRPC_TRACE_LOG(plugin_credentials, INFO)
        << "plugin_credentials[" << this << "]: request " << request.get()
        << ": plugin will return asynchronously";
    return [request] { return request->PollAsyncResult(); };
  }
  // Synchronous return.
  GRPC_TRACE_LOG(plugin_credentials, INFO)
      << "plugin_credentials[" << this << "]: request " << request.get()
      << ": plugin returned synchronously";
  auto result = request->ProcessPluginResult(creds_md, num_creds_md, status,
                                             error_details);
  // Clean up.
  for (size_t i = 0; i < num_creds_md; ++i) {
    grpc_core::CSliceUnref(creds_md[i].key);
    grpc_core::CSliceUnref(creds_md[i].value);
  }
  gpr_free(const_cast<char*>(error_details));

  return grpc_core::Immediate(std::move(result));
}

grpc_plugin_credentials::grpc_plugin_credentials(
    grpc_metadata_credentials_plugin plugin,
    grpc_security_level min_security_level)
    : grpc_call_credentials(min_security_level), plugin_(plugin) {}

grpc_call_credentials* grpc_metadata_credentials_create_from_plugin(
    grpc_metadata_credentials_plugin plugin,
    grpc_security_level min_security_level, void* reserved) {
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_metadata_credentials_create_from_plugin(reserved=" << reserved
      << ")";
  CHECK_EQ(reserved, nullptr);
  return new grpc_plugin_credentials(plugin, min_security_level);
}
