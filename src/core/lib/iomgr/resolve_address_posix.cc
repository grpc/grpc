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

#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/block_annotate.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/resolve_address_posix.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {
namespace {

using ::grpc_event_engine::experimental::GetDefaultEventEngine;

class NativeDNSRequest {
 public:
  NativeDNSRequest(
      absl::string_view name, absl::string_view default_port,
      std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
          on_done)
      : name_(name), default_port_(default_port), on_done_(std::move(on_done)) {
    GRPC_CLOSURE_INIT(&request_closure_, DoRequestThread, this, nullptr);
    Executor::Run(&request_closure_, GRPC_ERROR_NONE, ExecutorType::RESOLVER);
  }

 private:
  // Callback to be passed to grpc Executor to asynch-ify
  // LookupHostnameBlocking
  static void DoRequestThread(void* rp, grpc_error_handle /*error*/) {
    NativeDNSRequest* r = static_cast<NativeDNSRequest*>(rp);
    auto result =
        GetDNSResolver()->LookupHostnameBlocking(r->name_, r->default_port_);
    // running inline is safe since we've already been scheduled on the executor
    r->on_done_(std::move(result));
    delete r;
  }

  const std::string name_;
  const std::string default_port_;
  const std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
      on_done_;
  grpc_closure request_closure_;
};

}  // namespace

NativeDNSResolver* NativeDNSResolver::GetOrCreate() {
  static NativeDNSResolver* instance = new NativeDNSResolver();
  return instance;
}

DNSResolver::TaskHandle NativeDNSResolver::LookupHostname(
    std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
        on_done,
    absl::string_view name, absl::string_view default_port,
    Duration /* timeout */, grpc_pollset_set* /* interested_parties */,
    absl::string_view /* name_server */) {
  // self-deleting class
  new NativeDNSRequest(name, default_port, std::move(on_done));
  return kNullHandle;
}

absl::StatusOr<std::vector<grpc_resolved_address>>
NativeDNSResolver::LookupHostnameBlocking(absl::string_view name,
                                          absl::string_view default_port) {
  ExecCtx exec_ctx;
  struct addrinfo hints;
  struct addrinfo *result = nullptr, *resp;
  int s;
  size_t i;
  grpc_error_handle err;
  std::vector<grpc_resolved_address> addresses;
  std::string host;
  std::string port;
  // parse name, splitting it into host and port parts
  SplitHostPort(name, &host, &port);
  if (host.empty()) {
    err = grpc_error_set_str(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("unparseable host:port"),
        GRPC_ERROR_STR_TARGET_ADDRESS, name);
    goto done;
  }
  if (port.empty()) {
    if (default_port.empty()) {
      err = grpc_error_set_str(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("no port in name"),
          GRPC_ERROR_STR_TARGET_ADDRESS, name);
      goto done;
    }
    port = std::string(default_port);
  }
  // Call getaddrinfo
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;     /* ipv4 or ipv6 */
  hints.ai_socktype = SOCK_STREAM; /* stream socket */
  hints.ai_flags = AI_PASSIVE;     /* for wildcard IP address */
  GRPC_SCHEDULING_START_BLOCKING_REGION;
  s = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
  GRPC_SCHEDULING_END_BLOCKING_REGION;
  if (s != 0) {
    // Retry if well-known service name is recognized
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
  // Success path: fill in addrs
  for (resp = result; resp != nullptr; resp = resp->ai_next) {
    grpc_resolved_address addr;
    memcpy(&addr.addr, resp->ai_addr, resp->ai_addrlen);
    addr.len = resp->ai_addrlen;
    addresses.push_back(addr);
  }
  err = GRPC_ERROR_NONE;
done:
  if (result) {
    freeaddrinfo(result);
  }
  if (GRPC_ERROR_IS_NONE(err)) {
    return addresses;
  }
  auto error_result = grpc_error_to_absl_status(err);
  GRPC_ERROR_UNREF(err);
  return error_result;
}

DNSResolver::TaskHandle NativeDNSResolver::LookupSRV(
    std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
        on_resolved,
    absl::string_view /* name */, Duration /* timeout */,
    grpc_pollset_set* /* interested_parties */,
    absl::string_view /* name_server */) {
  GetDefaultEventEngine()->Run([on_resolved] {
    ApplicationCallbackExecCtx app_exec_ctx;
    ExecCtx exec_ctx;
    on_resolved(absl::UnimplementedError(
        "The Native resolver does not support looking up SRV records"));
  });
  return {-1, -1};
};

DNSResolver::TaskHandle NativeDNSResolver::LookupTXT(
    std::function<void(absl::StatusOr<std::string>)> on_resolved,
    absl::string_view /* name */, Duration /* timeout */,
    grpc_pollset_set* /* interested_parties */,
    absl::string_view /* name_server */) {
  // Not supported
  GetDefaultEventEngine()->Run([on_resolved] {
    ApplicationCallbackExecCtx app_exec_ctx;
    ExecCtx exec_ctx;
    on_resolved(absl::UnimplementedError(
        "The Native resolver does not support looking up TXT records"));
  });
  return {-1, -1};
};

bool NativeDNSResolver::Cancel(TaskHandle /*handle*/) { return false; }

}  // namespace grpc_core

#endif
