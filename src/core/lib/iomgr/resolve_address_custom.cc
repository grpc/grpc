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

#include "src/core/lib/iomgr/resolve_address_custom.h"

#include <string.h>

#include <string>

#include "absl/strings/str_format.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/iomgr_custom.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"

struct grpc_custom_resolver {
  grpc_closure* on_done = nullptr;
  grpc_resolved_addresses** addresses = nullptr;
  std::string host;
  std::string port;
};

static grpc_custom_resolver_vtable* resolve_address_vtable = nullptr;

static int retry_named_port_failure(grpc_custom_resolver* r,
                                    grpc_resolved_addresses** res) {
  // This loop is copied from resolve_address_posix.c
  const char* svc[][2] = {{"http", "80"}, {"https", "443"}};
  for (size_t i = 0; i < GPR_ARRAY_SIZE(svc); i++) {
    if (r->port == svc[i][0]) {
      r->port = svc[i][1];
      if (res) {
        grpc_error* error = resolve_address_vtable->resolve(
            r->host.c_str(), r->port.c_str(), res);
        if (error != GRPC_ERROR_NONE) {
          GRPC_ERROR_UNREF(error);
          return 0;
        }
      } else {
        resolve_address_vtable->resolve_async(r, r->host.c_str(),
                                              r->port.c_str());
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
  delete r;
}

static grpc_error* try_split_host_port(const char* name,
                                       const char* default_port,
                                       std::string* host, std::string* port) {
  /* parse name, splitting it into host and port parts */
  grpc_core::SplitHostPort(name, host, port);
  if (host->empty()) {
    return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrFormat("unparseable host:port: '%s'", name).c_str());
  }
  if (port->empty()) {
    // TODO(murgatroid99): add tests for this case
    if (default_port == nullptr) {
      return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrFormat("no port in name '%s'", name).c_str());
    }
    *port = default_port;
  }
  return GRPC_ERROR_NONE;
}

static grpc_error* blocking_resolve_address_impl(
    const char* name, const char* default_port,
    grpc_resolved_addresses** addresses) {
  GRPC_CUSTOM_IOMGR_ASSERT_SAME_THREAD();

  grpc_custom_resolver resolver;
  grpc_error* err =
      try_split_host_port(name, default_port, &resolver.host, &resolver.port);
  if (err != GRPC_ERROR_NONE) {
    return err;
  }

  /* Call getaddrinfo */
  grpc_resolved_addresses* addrs;
  grpc_core::ExecCtx* curr = grpc_core::ExecCtx::Get();
  grpc_core::ExecCtx::Set(nullptr);
  err = resolve_address_vtable->resolve(resolver.host.c_str(),
                                        resolver.port.c_str(), &addrs);
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
  GRPC_CUSTOM_IOMGR_ASSERT_SAME_THREAD();
  std::string host;
  std::string port;
  grpc_error* err = try_split_host_port(name, default_port, &host, &port);
  if (err != GRPC_ERROR_NONE) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, err);
    return;
  }
  grpc_custom_resolver* r = new grpc_custom_resolver();
  r->on_done = on_done;
  r->addresses = addrs;
  r->host = std::move(host);
  r->port = std::move(port);

  /* Call getaddrinfo */
  resolve_address_vtable->resolve_async(r, r->host.c_str(), r->port.c_str());
}

static grpc_address_resolver_vtable custom_resolver_vtable = {
    resolve_address_impl, blocking_resolve_address_impl};

void grpc_custom_resolver_init(grpc_custom_resolver_vtable* impl) {
  resolve_address_vtable = impl;
  grpc_set_resolver_impl(&custom_resolver_vtable);
}
