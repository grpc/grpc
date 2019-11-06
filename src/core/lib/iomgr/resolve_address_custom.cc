/*
 *
 * Copyright 2018 gRPC authors.
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

#include "src/core/lib/iomgr/port.h"

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include <grpc/support/log.h>
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/host_port.h"

#include "src/core/lib/iomgr/iomgr_custom.h"
#include "src/core/lib/iomgr/resolve_address_custom.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"

#include <string.h>

typedef struct grpc_custom_resolver {
  grpc_closure* on_done;
  grpc_resolved_addresses** addresses;
  char* host;
  char* port;
} grpc_custom_resolver;

static grpc_custom_resolver_vtable* resolve_address_vtable = nullptr;

static int retry_named_port_failure(grpc_custom_resolver* r,
                                    grpc_resolved_addresses** res) {
  // This loop is copied from resolve_address_posix.c
  const char* svc[][2] = {{"http", "80"}, {"https", "443"}};
  for (size_t i = 0; i < GPR_ARRAY_SIZE(svc); i++) {
    if (strcmp(r->port, svc[i][0]) == 0) {
      gpr_free(r->port);
      r->port = gpr_strdup(svc[i][1]);
      if (res) {
        grpc_error* error =
            resolve_address_vtable->resolve(r->host, r->port, res);
        if (error != GRPC_ERROR_NONE) {
          GRPC_ERROR_UNREF(error);
          return 0;
        }
      } else {
        resolve_address_vtable->resolve_async(r, r->host, r->port);
      }
      return 1;
    }
  }
  return 0;
}

void grpc_custom_resolve_callback(grpc_custom_resolver* r,
                                  grpc_resolved_addresses* result,
                                  grpc_error* error) {
  GRPC_CUSTOM_IOMGR_ASSERT_SAME_THREAD();
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  if (error == GRPC_ERROR_NONE) {
    *r->addresses = result;
  } else if (retry_named_port_failure(r, nullptr)) {
    return;
  }
  if (r->on_done) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, r->on_done, error);
  }
  gpr_free(r->host);
  gpr_free(r->port);
  gpr_free(r);
}

static grpc_error* try_split_host_port(const char* name,
                                       const char* default_port,
                                       grpc_core::UniquePtr<char>* host,
                                       grpc_core::UniquePtr<char>* port) {
  /* parse name, splitting it into host and port parts */
  grpc_error* error;
  grpc_core::SplitHostPort(name, host, port);
  if (*host == nullptr) {
    char* msg;
    gpr_asprintf(&msg, "unparseable host:port: '%s'", name);
    error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    return error;
  }
  if (*port == nullptr) {
    // TODO(murgatroid99): add tests for this case
    if (default_port == nullptr) {
      char* msg;
      gpr_asprintf(&msg, "no port in name '%s'", name);
      error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
      gpr_free(msg);
      return error;
    }
    port->reset(gpr_strdup(default_port));
  }
  return GRPC_ERROR_NONE;
}

static grpc_error* blocking_resolve_address_impl(
    const char* name, const char* default_port,
    grpc_resolved_addresses** addresses) {
  grpc_core::UniquePtr<char> host;
  grpc_core::UniquePtr<char> port;
  grpc_error* err;

  GRPC_CUSTOM_IOMGR_ASSERT_SAME_THREAD();

  err = try_split_host_port(name, default_port, &host, &port);
  if (err != GRPC_ERROR_NONE) {
    return err;
  }

  /* Call getaddrinfo */
  grpc_custom_resolver resolver;
  resolver.host = host.get();
  resolver.port = port.get();

  grpc_resolved_addresses* addrs;
  grpc_core::ExecCtx* curr = grpc_core::ExecCtx::Get();
  grpc_core::ExecCtx::Set(nullptr);
  err = resolve_address_vtable->resolve(host.get(), port.get(), &addrs);
  if (err != GRPC_ERROR_NONE) {
    if (retry_named_port_failure(&resolver, &addrs)) {
      GRPC_ERROR_UNREF(err);
      err = GRPC_ERROR_NONE;
    }
  }
  grpc_core::ExecCtx::Set(curr);
  if (err == GRPC_ERROR_NONE) {
    *addresses = addrs;
  }
  return err;
}

static void resolve_address_impl(const char* name, const char* default_port,
                                 grpc_pollset_set* /*interested_parties*/,
                                 grpc_closure* on_done,
                                 grpc_resolved_addresses** addrs) {
  grpc_custom_resolver* r = nullptr;
  grpc_core::UniquePtr<char> host;
  grpc_core::UniquePtr<char> port;
  grpc_error* err;
  GRPC_CUSTOM_IOMGR_ASSERT_SAME_THREAD();
  err = try_split_host_port(name, default_port, &host, &port);
  if (err != GRPC_ERROR_NONE) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, err);
    return;
  }
  r = (grpc_custom_resolver*)gpr_malloc(sizeof(grpc_custom_resolver));
  r->on_done = on_done;
  r->addresses = addrs;
  r->host = host.release();
  r->port = port.release();

  /* Call getaddrinfo */
  resolve_address_vtable->resolve_async(r, r->host, r->port);
}

static grpc_address_resolver_vtable custom_resolver_vtable = {
    resolve_address_impl, blocking_resolve_address_impl};

void grpc_custom_resolver_init(grpc_custom_resolver_vtable* impl) {
  resolve_address_vtable = impl;
  grpc_set_resolver_impl(&custom_resolver_vtable);
}
