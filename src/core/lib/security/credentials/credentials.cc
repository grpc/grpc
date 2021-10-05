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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/credentials.h"

#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/surface/api_trace.h"

/* -- Common. -- */

void grpc_channel_credentials_release(grpc_channel_credentials* creds) {
  GRPC_API_TRACE("grpc_channel_credentials_release(creds=%p)", 1, (creds));
  grpc_core::ExecCtx exec_ctx;
  if (creds) creds->Unref();
}

void grpc_call_credentials_release(grpc_call_credentials* creds) {
  GRPC_API_TRACE("grpc_call_credentials_release(creds=%p)", 1, (creds));
  grpc_core::ExecCtx exec_ctx;
  if (creds) creds->Unref();
}

static void credentials_pointer_arg_destroy(void* p) {
  static_cast<grpc_channel_credentials*>(p)->Unref();
}

static void* credentials_pointer_arg_copy(void* p) {
  return static_cast<grpc_channel_credentials*>(p)->Ref().release();
}

static int credentials_pointer_cmp(void* a, void* b) {
  return grpc_core::QsortCompare(a, b);
}

static const grpc_arg_pointer_vtable credentials_pointer_vtable = {
    credentials_pointer_arg_copy, credentials_pointer_arg_destroy,
    credentials_pointer_cmp};

grpc_arg grpc_channel_credentials_to_arg(
    grpc_channel_credentials* credentials) {
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_ARG_CHANNEL_CREDENTIALS), credentials,
      &credentials_pointer_vtable);
}

grpc_channel_credentials* grpc_channel_credentials_from_arg(
    const grpc_arg* arg) {
  if (strcmp(arg->key, GRPC_ARG_CHANNEL_CREDENTIALS) != 0) return nullptr;
  if (arg->type != GRPC_ARG_POINTER) {
    gpr_log(GPR_ERROR, "Invalid type %d for arg %s", arg->type,
            GRPC_ARG_CHANNEL_CREDENTIALS);
    return nullptr;
  }
  return static_cast<grpc_channel_credentials*>(arg->value.pointer.p);
}

grpc_channel_credentials* grpc_channel_credentials_find_in_args(
    const grpc_channel_args* args) {
  size_t i;
  if (args == nullptr) return nullptr;
  for (i = 0; i < args->num_args; i++) {
    grpc_channel_credentials* credentials =
        grpc_channel_credentials_from_arg(&args->args[i]);
    if (credentials != nullptr) return credentials;
  }
  return nullptr;
}

void grpc_server_credentials_release(grpc_server_credentials* creds) {
  GRPC_API_TRACE("grpc_server_credentials_release(creds=%p)", 1, (creds));
  grpc_core::ExecCtx exec_ctx;
  if (creds) creds->Unref();
}

void grpc_server_credentials::set_auth_metadata_processor(
    const grpc_auth_metadata_processor& processor) {
  GRPC_API_TRACE(
      "grpc_server_credentials_set_auth_metadata_processor("
      "creds=%p, "
      "processor=grpc_auth_metadata_processor { process: %p, state: %p })",
      3, (this, (void*)(intptr_t)processor.process, processor.state));
  DestroyProcessor();
  processor_ = processor;
}

void grpc_server_credentials_set_auth_metadata_processor(
    grpc_server_credentials* creds, grpc_auth_metadata_processor processor) {
  GPR_DEBUG_ASSERT(creds != nullptr);
  creds->set_auth_metadata_processor(processor);
}

static void server_credentials_pointer_arg_destroy(void* p) {
  static_cast<grpc_server_credentials*>(p)->Unref();
}

static void* server_credentials_pointer_arg_copy(void* p) {
  return static_cast<grpc_server_credentials*>(p)->Ref().release();
}

static int server_credentials_pointer_cmp(void* a, void* b) {
  return grpc_core::QsortCompare(a, b);
}

static const grpc_arg_pointer_vtable cred_ptr_vtable = {
    server_credentials_pointer_arg_copy, server_credentials_pointer_arg_destroy,
    server_credentials_pointer_cmp};

grpc_arg grpc_server_credentials_to_arg(grpc_server_credentials* c) {
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_SERVER_CREDENTIALS_ARG), c, &cred_ptr_vtable);
}

grpc_server_credentials* grpc_server_credentials_from_arg(const grpc_arg* arg) {
  if (strcmp(arg->key, GRPC_SERVER_CREDENTIALS_ARG) != 0) return nullptr;
  if (arg->type != GRPC_ARG_POINTER) {
    gpr_log(GPR_ERROR, "Invalid type %d for arg %s", arg->type,
            GRPC_SERVER_CREDENTIALS_ARG);
    return nullptr;
  }
  return static_cast<grpc_server_credentials*>(arg->value.pointer.p);
}

grpc_server_credentials* grpc_find_server_credentials_in_args(
    const grpc_channel_args* args) {
  size_t i;
  if (args == nullptr) return nullptr;
  for (i = 0; i < args->num_args; i++) {
    grpc_server_credentials* p =
        grpc_server_credentials_from_arg(&args->args[i]);
    if (p != nullptr) return p;
  }
  return nullptr;
}
