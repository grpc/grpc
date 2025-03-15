//
// Copyright 2015 gRPC authors.
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

#include "src/core/call/security_context.h"

#include <grpc/credentials.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <string.h>

#include <algorithm>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/credentials/call/call_credentials.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/surface/call.h"
#include "src/core/util/ref_counted_ptr.h"

// --- grpc_call ---

grpc_call_error grpc_call_set_credentials(grpc_call* call,
                                          grpc_call_credentials* creds) {
  grpc_core::ExecCtx exec_ctx;
  grpc_client_security_context* ctx = nullptr;
  GRPC_TRACE_LOG(api, INFO) << "grpc_call_set_credentials(call=" << call
                            << ", creds=" << creds << ")";
  if (!grpc_call_is_client(call)) {
    LOG(ERROR) << "Method is client-side only.";
    return GRPC_CALL_ERROR_NOT_ON_SERVER;
  }
  auto* arena = grpc_call_get_arena(call);
  ctx = grpc_core::DownCast<grpc_client_security_context*>(
      arena->GetContext<grpc_core::SecurityContext>());
  if (ctx == nullptr) {
    ctx = grpc_client_security_context_create(arena, creds);
    arena->SetContext<grpc_core::SecurityContext>(ctx);
  } else {
    ctx->creds = creds != nullptr ? creds->Ref() : nullptr;
  }
  return GRPC_CALL_OK;
}

grpc_auth_context* grpc_call_auth_context(grpc_call* call) {
  auto* sec_ctx =
      grpc_call_get_arena(call)->GetContext<grpc_core::SecurityContext>();
  GRPC_TRACE_LOG(api, INFO) << "grpc_call_auth_context(call=" << call << ")";
  if (sec_ctx == nullptr) return nullptr;
  if (grpc_call_is_client(call)) {
    auto* sc = grpc_core::DownCast<grpc_client_security_context*>(sec_ctx);
    if (sc->auth_context == nullptr) {
      return nullptr;
    } else {
      return sc->auth_context
          ->Ref(DEBUG_LOCATION, "grpc_call_auth_context client")
          .release();
    }
  } else {
    auto* sc = grpc_core::DownCast<grpc_server_security_context*>(sec_ctx);
    if (sc->auth_context == nullptr) {
      return nullptr;
    } else {
      return sc->auth_context
          ->Ref(DEBUG_LOCATION, "grpc_call_auth_context server")
          .release();
    }
  }
}

// --- grpc_client_security_context ---
grpc_client_security_context::~grpc_client_security_context() {
  auth_context.reset(DEBUG_LOCATION, "client_security_context");
  if (extension.instance != nullptr && extension.destroy != nullptr) {
    extension.destroy(extension.instance);
  }
}

grpc_client_security_context* grpc_client_security_context_create(
    grpc_core::Arena* arena, grpc_call_credentials* creds) {
  return arena->New<grpc_client_security_context>(
      creds != nullptr ? creds->Ref() : nullptr);
}

void grpc_client_security_context_destroy(void* ctx) {
  grpc_client_security_context* c =
      static_cast<grpc_client_security_context*>(ctx);
  c->~grpc_client_security_context();
}

// --- grpc_server_security_context ---
grpc_server_security_context::~grpc_server_security_context() {
  auth_context.reset(DEBUG_LOCATION, "server_security_context");
  if (extension.instance != nullptr && extension.destroy != nullptr) {
    extension.destroy(extension.instance);
  }
}

grpc_server_security_context* grpc_server_security_context_create(
    grpc_core::Arena* arena) {
  return arena->New<grpc_server_security_context>();
}

void grpc_server_security_context_destroy(void* ctx) {
  grpc_server_security_context* c =
      static_cast<grpc_server_security_context*>(ctx);
  c->~grpc_server_security_context();
}
