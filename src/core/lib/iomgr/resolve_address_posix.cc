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

#include "src/core/lib/iomgr/port.h"
#ifdef GRPC_POSIX_SOCKET_RESOLVE_ADDRESS

#include <string.h>
#include <sys/types.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/block_annotate.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"

namespace grpc_core {

OrphanablePtr<Request> NativeDNSResolver::ResolveAddress(
    absl::string_view name, absl::string_view default_port,
    grpc_pollset_set* interested_parties,
    std::function<void(absl::StatusOr<grpc_resolved_addresses*>)> on_done) {
  NativeRequest* r = new NativeRequest();
  r->name = name;
  r->default_port = default_port;
  r->on_done = std::move(on_done);
  GRPC_CLOSURE_INIT(&r->request_closure, DoRequestThread, r, nullptr);
  grpc_core::Executor::Run(&r->request_closure, GRPC_ERROR_NONE,
                           grpc_core::ExecutorType::RESOLVER);
  // Force caller to wait for the callback's completion. Note
  // that no I/O polling is required for the resolution to finish.
  return nullptr;
}

absl::StatusOr<grpc_resolved_addresses*>
NativeDNSResolver::BlockingResolveAddress(absl::string_view name,
                                          absl::string_view default_port) {
  grpc_core::ExecCtx exec_ctx;
  struct addrinfo hints;
  struct addrinfo *result = nullptr, *resp;
  int s;
  size_t i;
  grpc_error_handle err;
  grpc_resolved_addresses* addresses = nullptr;
  std::string host;
  std::string port;
  /* parse name, splitting it into host and port parts */
  grpc_core::SplitHostPort(name, &host, &port);
  if (host.empty()) {
    err = grpc_error_set_str(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("unparseable host:port"),
        GRPC_ERROR_STR_TARGET_ADDRESS, name);
    goto done;
  }
  if (port.empty()) {
    if (default_port == nullptr) {
      err = grpc_error_set_str(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("no port in name"),
          GRPC_ERROR_STR_TARGET_ADDRESS, name);
      goto done;
    }
    port = default_port;
  }
  /* Call getaddrinfo */
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;     /* ipv4 or ipv6 */
  hints.ai_socktype = SOCK_STREAM; /* stream socket */
  hints.ai_flags = AI_PASSIVE;     /* for wildcard IP address */
  GRPC_SCHEDULING_START_BLOCKING_REGION;
  s = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
  GRPC_SCHEDULING_END_BLOCKING_REGION;
  if (s != 0) {
    /* Retry if well-known service name is recognized */
    const char* svc[][2] = {{"http", "80"}, {"https", "443"}};
    for (i = 0; i < GPR_ARRAY_SIZE(svc); i++) {
      if (port == svc[i][0]) {
        GRPC_SCHEDULING_START_BLOCKING_REGION;
        s = getaddrinfo(host.c_str(), svc[i][1], &hints, &result);
        GRPC_SCHEDULING_END_BLOCKING_REGION;
        break;
      }
    }
  }
  if (s != 0) {
    err = grpc_error_set_str(
        grpc_error_set_str(
            grpc_error_set_str(
                grpc_error_set_int(
                    GRPC_ERROR_CREATE_FROM_STATIC_STRING(gai_strerror(s)),
                    GRPC_ERROR_INT_ERRNO, s),
                GRPC_ERROR_STR_OS_ERROR, gai_strerror(s)),
            GRPC_ERROR_STR_SYSCALL, "getaddrinfo"),
        GRPC_ERROR_STR_TARGET_ADDRESS, name);
    goto done;
  }
  /* Success path: set addrs non-NULL, fill it in */
  addresses = static_cast<grpc_resolved_addresses*>(
      gpr_malloc(sizeof(grpc_resolved_addresses)));
  addresses->naddrs = 0;
  for (resp = result; resp != nullptr; resp = resp->ai_next) {
    addresses->naddrs++;
  }
  addresses->addrs = static_cast<grpc_resolved_address*>(
      gpr_malloc(sizeof(grpc_resolved_address) * addresses->naddrs));
  i = 0;
  for (resp = result; resp != nullptr; resp = resp->ai_next) {
    memcpy(&addresses->addrs[i].addr, resp->ai_addr, resp->ai_addrlen);
    addresses->addrs[i].len = resp->ai_addrlen;
    i++;
  }
  err = GRPC_ERROR_NONE;
done:
  if (result) {
    freeaddrinfo(result);
  }
  if (err == GRPC_ERROR_NONE) {
    return addresses;
  }
  return grpc_error_to_absl_status(err);
}

}  // namespace grpc_core

#endif
