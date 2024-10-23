//
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
//

#include "src/core/lib/security/credentials/credentials.h"

#include <grpc/support/port_platform.h>
#include <stdint.h>
#include <string.h>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/util/useful.h"

// -- Common. --

void grpc_channel_credentials_release(grpc_channel_credentials* creds) {
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_channel_credentials_release(creds=" << creds << ")";
  grpc_core::ExecCtx exec_ctx;
  if (creds) creds->Unref();
}

void grpc_call_credentials_release(grpc_call_credentials* creds) {
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_call_credentials_release(creds=" << creds << ")";
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
  return static_cast<const grpc_channel_credentials*>(a)->cmp(
      static_cast<const grpc_channel_credentials*>(b));
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
    LOG(ERROR) << "Invalid type " << arg->type << " for arg "
               << GRPC_ARG_CHANNEL_CREDENTIALS;
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
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_server_credentials_release(creds=" << creds << ")";
  grpc_core::ExecCtx exec_ctx;
  if (creds) creds->Unref();
}

void grpc_server_credentials::set_auth_metadata_processor(
    const grpc_auth_metadata_processor& processor) {
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_server_credentials_set_auth_metadata_processor(creds=" << this
      << ", processor=grpc_auth_metadata_processor { process: "
      << (void*)(intptr_t)processor.process << ", state: " << processor.state
      << " })";
  DestroyProcessor();
  processor_ = processor;
}

void grpc_server_credentials_set_auth_metadata_processor(
    grpc_server_credentials* creds, grpc_auth_metadata_processor processor) {
  DCHECK_NE(creds, nullptr);
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
    LOG(ERROR) << "Invalid type " << arg->type << " for arg "
               << GRPC_SERVER_CREDENTIALS_ARG;
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
