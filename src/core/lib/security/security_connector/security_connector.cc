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

#include "src/core/lib/security/security_connector/security_connector.h"

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/transport/chttp2/alpn/alpn.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/load_system_roots.h"
#include "src/core/lib/security/transport/security_handshaker.h"

grpc_core::DebugOnlyTraceFlag grpc_trace_security_connector_refcount(
    false, "security_connector_refcount");

grpc_server_security_connector::grpc_server_security_connector(
    const char* url_scheme, grpc_server_credentials* server_creds)
    : grpc_security_connector(url_scheme), server_creds_(server_creds->Ref()) {}

grpc_server_security_connector::~grpc_server_security_connector() {
  server_creds_->Unref();
}

grpc_channel_security_connector::grpc_channel_security_connector(
    const char* url_scheme, grpc_channel_credentials* channel_creds,
    grpc_call_credentials* request_metadata_creds)
    : grpc_security_connector(url_scheme),
      channel_creds_(channel_creds ? channel_creds->Ref() : nullptr),
      request_metadata_creds_(
          request_metadata_creds ? request_metadata_creds->Ref() : nullptr) {}

grpc_channel_security_connector::~grpc_channel_security_connector() {
  if (request_metadata_creds_) request_metadata_creds_->Unref();
  if (channel_creds_) channel_creds_->Unref();
}

int grpc_security_connector_cmp(const grpc_security_connector* sc,
                                const grpc_security_connector* other) {
  if (sc == nullptr || other == nullptr) return GPR_ICMP(sc, other);
  return sc->cmp(other);
}

int grpc_channel_security_connector_cmp(
    const grpc_channel_security_connector* sc1,
    const grpc_channel_security_connector* sc2) {
  return sc1 == sc2;
  GPR_ASSERT(sc1->channel_creds() != nullptr);
  GPR_ASSERT(sc2->channel_creds() != nullptr);
  int c = GPR_ICMP(sc1->channel_creds(), sc2->channel_creds());
  if (c != 0) return c;
  return GPR_ICMP(sc1->request_metadata_creds(), sc2->request_metadata_creds());
}

int grpc_server_security_connector_cmp(
    const grpc_server_security_connector* sc1,
    const grpc_server_security_connector* sc2) {
  GPR_ASSERT(sc1->server_creds() != nullptr);
  GPR_ASSERT(sc2->server_creds() != nullptr);
  int c = GPR_ICMP(sc1->server_creds(), sc2->server_creds());
  if (c != 0) return c;
  // FIXME(soheil)
  return GPR_ICMP((void*)sc1, (void*)sc2);
}

#ifndef NDEBUG
grpc_security_connector* grpc_security_connector_ref(
    grpc_security_connector* sc, const char* file, int line,
    const char* reason) {
  if (sc == nullptr) return nullptr;
  if (grpc_trace_security_connector_refcount.enabled()) {
    const grpc_core::RefCount::Value val = sc->refcount_.get();
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "SECURITY_CONNECTOR:%p   ref %" PRIdPTR " -> %" PRIdPTR " %s", sc,
            val, val + 1, reason);
  }
  return sc->Ref();
}

void grpc_security_connector_unref(grpc_security_connector* sc,
                                   const char* file, int line,
                                   const char* reason) {
  if (sc == nullptr) return;
  if (grpc_trace_security_connector_refcount.enabled()) {
    const grpc_core::RefCount::Value val = sc->refcount_.get();
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "SECURITY_CONNECTOR:%p unref %" PRIdPTR " -> %" PRIdPTR " %s", sc,
            val, val - 1, reason);
  }
  sc->Unref();
}
#endif

static void connector_arg_destroy(void* p) {
  GRPC_SECURITY_CONNECTOR_UNREF((grpc_security_connector*)p,
                                "connector_arg_destroy");
}

static void* connector_arg_copy(void* p) {
  return GRPC_SECURITY_CONNECTOR_REF((grpc_security_connector*)p,
                                     "connector_arg_copy");
}

static int connector_cmp(void* a, void* b) {
  return grpc_security_connector_cmp(static_cast<grpc_security_connector*>(a),
                                     static_cast<grpc_security_connector*>(b));
}

static const grpc_arg_pointer_vtable connector_arg_vtable = {
    connector_arg_copy, connector_arg_destroy, connector_cmp};

grpc_arg grpc_security_connector_to_arg(grpc_security_connector* sc) {
  return grpc_channel_arg_pointer_create((char*)GRPC_ARG_SECURITY_CONNECTOR, sc,
                                         &connector_arg_vtable);
}

grpc_security_connector* grpc_security_connector_from_arg(const grpc_arg* arg) {
  if (strcmp(arg->key, GRPC_ARG_SECURITY_CONNECTOR)) return nullptr;
  if (arg->type != GRPC_ARG_POINTER) {
    gpr_log(GPR_ERROR, "Invalid type %d for arg %s", arg->type,
            GRPC_ARG_SECURITY_CONNECTOR);
    return nullptr;
  }
  return static_cast<grpc_security_connector*>(arg->value.pointer.p);
}

grpc_security_connector* grpc_security_connector_find_in_args(
    const grpc_channel_args* args) {
  size_t i;
  if (args == nullptr) return nullptr;
  for (i = 0; i < args->num_args; i++) {
    grpc_security_connector* sc =
        grpc_security_connector_from_arg(&args->args[i]);
    if (sc != nullptr) return sc;
  }
  return nullptr;
}
