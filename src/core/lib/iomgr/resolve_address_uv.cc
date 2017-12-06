/*
 *
 * Copyright 2016 gRPC authors.
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

#include "src/core/lib/iomgr/port.h"
#ifdef GRPC_UV

#include <uv.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_uv.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"

#include <string.h>

typedef struct request {
  grpc_closure* on_done;
  grpc_resolved_addresses** addresses;
  struct addrinfo* hints;
  char* host;
  char* port;
} request;

static int retry_named_port_failure(int status, request* r,
                                    uv_getaddrinfo_cb getaddrinfo_cb) {
  if (status != 0) {
    // This loop is copied from resolve_address_posix.c
    const char* svc[][2] = {{"http", "80"}, {"https", "443"}};
    for (size_t i = 0; i < GPR_ARRAY_SIZE(svc); i++) {
      if (strcmp(r->port, svc[i][0]) == 0) {
        int retry_status;
        uv_getaddrinfo_t* req =
            (uv_getaddrinfo_t*)gpr_malloc(sizeof(uv_getaddrinfo_t));
        req->data = r;
        r->port = gpr_strdup(svc[i][1]);
        retry_status = uv_getaddrinfo(uv_default_loop(), req, getaddrinfo_cb,
                                      r->host, r->port, r->hints);
        if (retry_status < 0 || getaddrinfo_cb == NULL) {
          // The callback will not be called
          gpr_free(req);
        }
        return retry_status;
      }
    }
  }
  /* If this function calls uv_getaddrinfo, it will return that function's
     return value. That function only returns numbers <=0, so we can safely
     return 1 to indicate that we never retried */
  return 1;
}

static grpc_error* handle_addrinfo_result(int status, struct addrinfo* result,
                                          grpc_resolved_addresses** addresses) {
  struct addrinfo* resp;
  size_t i;
  if (status != 0) {
    grpc_error* error;
    *addresses = NULL;
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("getaddrinfo failed");
    error =
        grpc_error_set_str(error, GRPC_ERROR_STR_OS_ERROR,
                           grpc_slice_from_static_string(uv_strerror(status)));
    return error;
  }
  (*addresses) =
      (grpc_resolved_addresses*)gpr_malloc(sizeof(grpc_resolved_addresses));
  (*addresses)->naddrs = 0;
  for (resp = result; resp != NULL; resp = resp->ai_next) {
    (*addresses)->naddrs++;
  }
  (*addresses)->addrs = (grpc_resolved_address*)gpr_malloc(
      sizeof(grpc_resolved_address) * (*addresses)->naddrs);
  i = 0;
  for (resp = result; resp != NULL; resp = resp->ai_next) {
    memcpy(&(*addresses)->addrs[i].addr, resp->ai_addr, resp->ai_addrlen);
    (*addresses)->addrs[i].len = resp->ai_addrlen;
    i++;
  }

  {
    for (i = 0; i < (*addresses)->naddrs; i++) {
      char* buf;
      grpc_sockaddr_to_string(&buf, &(*addresses)->addrs[i], 0);
      gpr_free(buf);
    }
  }
  return GRPC_ERROR_NONE;
}

static void getaddrinfo_callback(uv_getaddrinfo_t* req, int status,
                                 struct addrinfo* res) {
  request* r = (request*)req->data;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_error* error;
  int retry_status;
  char* port = r->port;

  gpr_free(req);
  retry_status = retry_named_port_failure(status, r, getaddrinfo_callback);
  if (retry_status == 0) {
    /* The request is being retried. It is using its own port string, so we free
     * the original one */
    gpr_free(port);
    return;
  }
  /* Either no retry was attempted, or the retry failed. Either way, the
     original error probably has more interesting information */
  error = handle_addrinfo_result(status, res, r->addresses);
  GRPC_CLOSURE_SCHED(&exec_ctx, r->on_done, error);
  grpc_exec_ctx_finish(&exec_ctx);
  gpr_free(r->hints);
  gpr_free(r->host);
  gpr_free(r->port);
  gpr_free(r);
  uv_freeaddrinfo(res);
}

static grpc_error* try_split_host_port(const char* name,
                                       const char* default_port, char** host,
                                       char** port) {
  /* parse name, splitting it into host and port parts */
  grpc_error* error;
  gpr_split_host_port(name, host, port);
  if (*host == NULL) {
    char* msg;
    gpr_asprintf(&msg, "unparseable host:port: '%s'", name);
    error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    return error;
  }
  if (*port == NULL) {
    // TODO(murgatroid99): add tests for this case
    if (default_port == NULL) {
      char* msg;
      gpr_asprintf(&msg, "no port in name '%s'", name);
      error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
      gpr_free(msg);
      return error;
    }
    *port = gpr_strdup(default_port);
  }
  return GRPC_ERROR_NONE;
}

static grpc_error* blocking_resolve_address_impl(
    const char* name, const char* default_port,
    grpc_resolved_addresses** addresses) {
  char* host;
  char* port;
  struct addrinfo hints;
  uv_getaddrinfo_t req;
  int s;
  grpc_error* err;
  int retry_status;
  request r;

  GRPC_UV_ASSERT_SAME_THREAD();

  req.addrinfo = NULL;

  err = try_split_host_port(name, default_port, &host, &port);
  if (err != GRPC_ERROR_NONE) {
    goto done;
  }

  /* Call getaddrinfo */
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;     /* ipv4 or ipv6 */
  hints.ai_socktype = SOCK_STREAM; /* stream socket */
  hints.ai_flags = AI_PASSIVE;     /* for wildcard IP address */

  s = uv_getaddrinfo(uv_default_loop(), &req, NULL, host, port, &hints);
  r.addresses = addresses;
  r.hints = &hints;
  r.host = host;
  r.port = port;
  retry_status = retry_named_port_failure(s, &r, NULL);
  if (retry_status <= 0) {
    s = retry_status;
  }
  err = handle_addrinfo_result(s, req.addrinfo, addresses);

done:
  gpr_free(host);
  gpr_free(port);
  if (req.addrinfo) {
    uv_freeaddrinfo(req.addrinfo);
  }
  return err;
}

grpc_error* (*grpc_blocking_resolve_address)(
    const char* name, const char* default_port,
    grpc_resolved_addresses** addresses) = blocking_resolve_address_impl;

void grpc_resolved_addresses_destroy(grpc_resolved_addresses* addrs) {
  if (addrs != NULL) {
    gpr_free(addrs->addrs);
  }
  gpr_free(addrs);
}

static void resolve_address_impl(grpc_exec_ctx* exec_ctx, const char* name,
                                 const char* default_port,
                                 grpc_pollset_set* interested_parties,
                                 grpc_closure* on_done,
                                 grpc_resolved_addresses** addrs) {
  uv_getaddrinfo_t* req = NULL;
  request* r = NULL;
  struct addrinfo* hints = NULL;
  char* host = NULL;
  char* port = NULL;
  grpc_error* err;
  int s;
  GRPC_UV_ASSERT_SAME_THREAD();
  err = try_split_host_port(name, default_port, &host, &port);
  if (err != GRPC_ERROR_NONE) {
    GRPC_CLOSURE_SCHED(exec_ctx, on_done, err);
    gpr_free(host);
    gpr_free(port);
    return;
  }
  r = (request*)gpr_malloc(sizeof(request));
  r->on_done = on_done;
  r->addresses = addrs;
  r->host = host;
  r->port = port;
  req = (uv_getaddrinfo_t*)gpr_malloc(sizeof(uv_getaddrinfo_t));
  req->data = r;

  /* Call getaddrinfo */
  hints = (addrinfo*)gpr_malloc(sizeof(struct addrinfo));
  memset(hints, 0, sizeof(struct addrinfo));
  hints->ai_family = AF_UNSPEC;     /* ipv4 or ipv6 */
  hints->ai_socktype = SOCK_STREAM; /* stream socket */
  hints->ai_flags = AI_PASSIVE;     /* for wildcard IP address */
  r->hints = hints;

  s = uv_getaddrinfo(uv_default_loop(), req, getaddrinfo_callback, host, port,
                     hints);

  if (s != 0) {
    *addrs = NULL;
    err = GRPC_ERROR_CREATE_FROM_STATIC_STRING("getaddrinfo failed");
    err = grpc_error_set_str(err, GRPC_ERROR_STR_OS_ERROR,
                             grpc_slice_from_static_string(uv_strerror(s)));
    GRPC_CLOSURE_SCHED(exec_ctx, on_done, err);
    gpr_free(r);
    gpr_free(req);
    gpr_free(hints);
    gpr_free(host);
    gpr_free(port);
  }
}

void (*grpc_resolve_address)(
    grpc_exec_ctx* exec_ctx, const char* name, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    grpc_resolved_addresses** addrs) = resolve_address_impl;

#endif /* GRPC_UV */
