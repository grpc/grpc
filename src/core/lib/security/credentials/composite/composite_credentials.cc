/*
 *
 * Copyright 2015-2016 gRPC authors.
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

#include "src/core/lib/security/credentials/composite/composite_credentials.h"

#include <cstring>
#include <new>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/surface/api_trace.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

/* -- Composite call credentials. -- */

static void composite_call_metadata_cb(void* arg, grpc_error* error);

namespace {
struct grpc_composite_call_credentials_metadata_context {
  grpc_composite_call_credentials_metadata_context(
      grpc_composite_call_credentials* composite_creds,
      grpc_polling_entity* pollent, grpc_auth_metadata_context auth_md_context,
      grpc_credentials_mdelem_array* md_array,
      grpc_closure* on_request_metadata)
      : composite_creds(composite_creds),
        pollent(pollent),
        auth_md_context(auth_md_context),
        md_array(md_array),
        on_request_metadata(on_request_metadata) {
    GRPC_CLOSURE_INIT(&internal_on_request_metadata, composite_call_metadata_cb,
                      this, grpc_schedule_on_exec_ctx);
  }

  grpc_composite_call_credentials* composite_creds;
  size_t creds_index = 0;
  grpc_polling_entity* pollent;
  grpc_auth_metadata_context auth_md_context;
  grpc_credentials_mdelem_array* md_array;
  grpc_closure* on_request_metadata;
  grpc_closure internal_on_request_metadata;
};
}  // namespace

static void composite_call_metadata_cb(void* arg, grpc_error* error) {
  grpc_composite_call_credentials_metadata_context* ctx =
      static_cast<grpc_composite_call_credentials_metadata_context*>(arg);
  if (error == GRPC_ERROR_NONE) {
    const grpc_composite_call_credentials::CallCredentialsList& inner =
        ctx->composite_creds->inner();
    /* See if we need to get some more metadata. */
    if (ctx->creds_index < inner.size()) {
      if (inner[ctx->creds_index++]->get_request_metadata(
              ctx->pollent, ctx->auth_md_context, ctx->md_array,
              &ctx->internal_on_request_metadata, &error)) {
        // Synchronous response, so call ourselves recursively.
        composite_call_metadata_cb(arg, error);
        GRPC_ERROR_UNREF(error);
      }
      return;
    }
    // We're done!
  }
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, ctx->on_request_metadata,
                          GRPC_ERROR_REF(error));
  delete ctx;
}

bool grpc_composite_call_credentials::get_request_metadata(
    grpc_polling_entity* pollent, grpc_auth_metadata_context auth_md_context,
    grpc_credentials_mdelem_array* md_array, grpc_closure* on_request_metadata,
    grpc_error** error) {
  grpc_composite_call_credentials_metadata_context* ctx;
  ctx = new grpc_composite_call_credentials_metadata_context(
      this, pollent, auth_md_context, md_array, on_request_metadata);
  bool synchronous = true;
  const CallCredentialsList& inner = ctx->composite_creds->inner();
  while (ctx->creds_index < inner.size()) {
    if (inner[ctx->creds_index++]->get_request_metadata(
            ctx->pollent, ctx->auth_md_context, ctx->md_array,
            &ctx->internal_on_request_metadata, error)) {
      if (*error != GRPC_ERROR_NONE) break;
    } else {
      synchronous = false;  // Async return.
      break;
    }
  }
  if (synchronous) delete ctx;
  return synchronous;
}

void grpc_composite_call_credentials::cancel_get_request_metadata(
    grpc_credentials_mdelem_array* md_array, grpc_error* error) {
  for (size_t i = 0; i < inner_.size(); ++i) {
    inner_[i]->cancel_get_request_metadata(md_array, GRPC_ERROR_REF(error));
  }
  GRPC_ERROR_UNREF(error);
}

std::string grpc_composite_call_credentials::debug_string() {
  std::vector<std::string> outputs;
  for (auto& inner_cred : inner_) {
    outputs.emplace_back(inner_cred->debug_string());
  }
  return absl::StrCat("CompositeCallCredentials{", absl::StrJoin(outputs, ","),
                      "}");
}

static size_t get_creds_array_size(const grpc_call_credentials* creds,
                                   bool is_composite) {
  return is_composite
             ? static_cast<const grpc_composite_call_credentials*>(creds)
                   ->inner()
                   .size()
             : 1;
}

void grpc_composite_call_credentials::push_to_inner(
    grpc_core::RefCountedPtr<grpc_call_credentials> creds, bool is_composite) {
  if (!is_composite) {
    inner_.push_back(std::move(creds));
    return;
  }
  auto composite_creds =
      static_cast<grpc_composite_call_credentials*>(creds.get());
  for (size_t i = 0; i < composite_creds->inner().size(); ++i) {
    inner_.push_back(std::move(composite_creds->inner_[i]));
  }
}

grpc_composite_call_credentials::grpc_composite_call_credentials(
    grpc_core::RefCountedPtr<grpc_call_credentials> creds1,
    grpc_core::RefCountedPtr<grpc_call_credentials> creds2)
    : grpc_call_credentials(GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE) {
  const bool creds1_is_composite =
      strcmp(creds1->type(), GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE) == 0;
  const bool creds2_is_composite =
      strcmp(creds2->type(), GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE) == 0;
  const size_t size = get_creds_array_size(creds1.get(), creds1_is_composite) +
                      get_creds_array_size(creds2.get(), creds2_is_composite);
  inner_.reserve(size);
  push_to_inner(std::move(creds1), creds1_is_composite);
  push_to_inner(std::move(creds2), creds2_is_composite);
  min_security_level_ = GRPC_SECURITY_NONE;
  for (size_t i = 0; i < inner_.size(); ++i) {
    if (static_cast<int>(min_security_level_) <
        static_cast<int>(inner_[i]->min_security_level())) {
      min_security_level_ = inner_[i]->min_security_level();
    }
  }
}

static grpc_core::RefCountedPtr<grpc_call_credentials>
composite_call_credentials_create(
    grpc_core::RefCountedPtr<grpc_call_credentials> creds1,
    grpc_core::RefCountedPtr<grpc_call_credentials> creds2) {
  return grpc_core::MakeRefCounted<grpc_composite_call_credentials>(
      std::move(creds1), std::move(creds2));
}

grpc_call_credentials* grpc_composite_call_credentials_create(
    grpc_call_credentials* creds1, grpc_call_credentials* creds2,
    void* reserved) {
  GRPC_API_TRACE(
      "grpc_composite_call_credentials_create(creds1=%p, creds2=%p, "
      "reserved=%p)",
      3, (creds1, creds2, reserved));
  GPR_ASSERT(reserved == nullptr);
  GPR_ASSERT(creds1 != nullptr);
  GPR_ASSERT(creds2 != nullptr);

  return composite_call_credentials_create(creds1->Ref(), creds2->Ref())
      .release();
}

/* -- Composite channel credentials. -- */

grpc_core::RefCountedPtr<grpc_channel_security_connector>
grpc_composite_channel_credentials::create_security_connector(
    grpc_core::RefCountedPtr<grpc_call_credentials> call_creds,
    const char* target, const grpc_channel_args* args,
    grpc_channel_args** new_args) {
  GPR_ASSERT(inner_creds_ != nullptr && call_creds_ != nullptr);
  /* If we are passed a call_creds, create a call composite to pass it
     downstream. */
  if (call_creds != nullptr) {
    return inner_creds_->create_security_connector(
        composite_call_credentials_create(call_creds_, std::move(call_creds)),
        target, args, new_args);
  } else {
    return inner_creds_->create_security_connector(call_creds_, target, args,
                                                   new_args);
  }
}

grpc_channel_credentials* grpc_composite_channel_credentials_create(
    grpc_channel_credentials* channel_creds, grpc_call_credentials* call_creds,
    void* reserved) {
  GPR_ASSERT(channel_creds != nullptr && call_creds != nullptr &&
             reserved == nullptr);
  GRPC_API_TRACE(
      "grpc_composite_channel_credentials_create(channel_creds=%p, "
      "call_creds=%p, reserved=%p)",
      3, (channel_creds, call_creds, reserved));
  return new grpc_composite_channel_credentials(channel_creds->Ref(),
                                                call_creds->Ref());
}
