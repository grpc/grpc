/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "test/core/end2end/fixtures/http_proxy.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/slice_buffer.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/useful.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "test/core/util/port.h"

struct grpc_end2end_http_proxy {
  char* proxy_name;
  gpr_thd_id thd;
  grpc_tcp_server* server;
  grpc_channel_args* channel_args;
  gpr_mu* mu;
  grpc_pollset* pollset;
  bool shutdown;
};

//
// Connection handling
//

typedef struct connection_data {
  grpc_endpoint* client_endpoint;
  grpc_endpoint* server_endpoint;

  grpc_pollset_set* pollset_set;

  grpc_closure on_read_request_done;
  grpc_closure on_server_connect_done;
  grpc_closure on_write_response_done;
  grpc_closure on_client_read_done;
  grpc_closure on_client_write_done;
  grpc_closure on_server_read_done;
  grpc_closure on_server_write_done;

  gpr_slice_buffer client_read_buffer;
  gpr_slice_buffer client_write_buffer;
  gpr_slice_buffer server_read_buffer;
  gpr_slice_buffer server_write_buffer;

  grpc_http_parser http_parser;
  grpc_http_request http_request;

  grpc_end2end_http_proxy* proxy;

  gpr_refcount refcount;
} connection_data;

static void connection_data_destroy(grpc_exec_ctx* exec_ctx,
                                    connection_data* cd) {
gpr_log(GPR_ERROR, "==> %s()", __func__);
  cd->proxy->shutdown = true;
  grpc_endpoint_destroy(exec_ctx, cd->client_endpoint);
  if (cd->server_endpoint != NULL)
    grpc_endpoint_destroy(exec_ctx, cd->server_endpoint);
  grpc_pollset_set_destroy(cd->pollset_set);
  gpr_slice_buffer_destroy(&cd->client_read_buffer);
  gpr_slice_buffer_destroy(&cd->client_write_buffer);
  gpr_slice_buffer_destroy(&cd->server_read_buffer);
  gpr_slice_buffer_destroy(&cd->server_write_buffer);
  grpc_http_parser_destroy(&cd->http_parser);
  grpc_http_request_destroy(&cd->http_request);
  gpr_free(cd);
}

static void connection_data_failed(grpc_exec_ctx* exec_ctx,
                                   connection_data* cd, const char* prefix,
                                   grpc_error* error) {
gpr_log(GPR_ERROR, "==> %s()", __func__);
  const char* msg = grpc_error_string(error);
  gpr_log(GPR_ERROR, "%s: %s", prefix, msg);
  grpc_error_free_string(msg);
  GRPC_ERROR_UNREF(error);
gpr_log(GPR_ERROR, "HERE 0");
  grpc_endpoint_shutdown(exec_ctx, cd->client_endpoint);
gpr_log(GPR_ERROR, "HERE 1");
  if (cd->server_endpoint != NULL)
    grpc_endpoint_shutdown(exec_ctx, cd->server_endpoint);
gpr_log(GPR_ERROR, "HERE 2");
  if (gpr_unref(&cd->refcount)) {
gpr_log(GPR_ERROR, "HERE 2.5");
    connection_data_destroy(exec_ctx, cd);
  }
gpr_log(GPR_ERROR, "HERE 3");
}

static void on_client_write_done(grpc_exec_ctx* exec_ctx, void* arg,
                                 grpc_error* error) {
gpr_log(GPR_ERROR, "==> %s()", __func__);
  connection_data* cd = arg;
  if (error != GRPC_ERROR_NONE) {
    connection_data_failed(exec_ctx, cd, "HTTP proxy client write", error);
    return;
  }
  // Clear write buffer.
  gpr_slice_buffer_reset_and_unref(&cd->client_write_buffer);
}

static void on_server_write_done(grpc_exec_ctx* exec_ctx, void* arg,
                                 grpc_error* error) {
gpr_log(GPR_ERROR, "==> %s()", __func__);
  connection_data* cd = arg;
  if (error != GRPC_ERROR_NONE) {
    connection_data_failed(exec_ctx, cd, "HTTP proxy server write", error);
    return;
  }
  // Clear write buffer.
  gpr_slice_buffer_reset_and_unref(&cd->server_write_buffer);
}

static void on_client_read_done(grpc_exec_ctx* exec_ctx, void* arg,
                                grpc_error* error) {
gpr_log(GPR_ERROR, "==> %s()", __func__);
  connection_data* cd = arg;
  if (error != GRPC_ERROR_NONE) {
    connection_data_failed(exec_ctx, cd, "HTTP proxy client read", error);
    return;
  }
  // Move read data into write buffer and write it.
  gpr_slice_buffer_move_into(&cd->client_read_buffer, &cd->server_write_buffer);
  grpc_endpoint_write(exec_ctx, cd->server_endpoint, &cd->server_write_buffer,
                      &cd->on_server_write_done);
  // Read more data.
  grpc_endpoint_read(exec_ctx, cd->client_endpoint, &cd->client_read_buffer,
                     &cd->on_client_read_done);
}

static void on_server_read_done(grpc_exec_ctx* exec_ctx, void* arg,
                                grpc_error* error) {
gpr_log(GPR_ERROR, "==> %s()", __func__);
  connection_data* cd = arg;
  if (error != GRPC_ERROR_NONE) {
    connection_data_failed(exec_ctx, cd, "HTTP proxy server read", error);
    return;
  }
  // Move read data into write buffer and write it.
  gpr_slice_buffer_move_into(&cd->server_read_buffer, &cd->client_write_buffer);
  grpc_endpoint_write(exec_ctx, cd->client_endpoint, &cd->client_write_buffer,
                      &cd->on_client_write_done);
  // Read more data.
  grpc_endpoint_read(exec_ctx, cd->server_endpoint, &cd->server_read_buffer,
                     &cd->on_server_read_done);
}

static void on_write_response_done(grpc_exec_ctx* exec_ctx, void* arg,
                                   grpc_error* error) {
gpr_log(GPR_ERROR, "==> %s()", __func__);
  connection_data* cd = arg;
  if (error != GRPC_ERROR_NONE) {
    connection_data_failed(exec_ctx, cd, "HTTP proxy write response", error);
    return;
  }
  // Clear write buffer.
  gpr_slice_buffer_reset_and_unref(&cd->client_write_buffer);
  // Start reading from both client and server.
  // We increase the refcount by one, since we already held one reference
  // for ourselves, and there will now be two pending callbacks.
  gpr_ref(&cd->refcount);
  grpc_endpoint_read(exec_ctx, cd->client_endpoint, &cd->client_read_buffer,
                     &cd->on_client_read_done);
  grpc_endpoint_read(exec_ctx, cd->server_endpoint, &cd->server_read_buffer,
                     &cd->on_server_read_done);
}

static void on_server_connect_done(grpc_exec_ctx* exec_ctx, void* arg,
                                   grpc_error* error) {
gpr_log(GPR_ERROR, "==> %s()", __func__);
  connection_data* cd = arg;
  if (error != GRPC_ERROR_NONE) {
    connection_data_failed(exec_ctx, cd, "HTTP proxy server connect", error);
    return;
  }
  // We've established a connection, so send back a 200 response code to
  // the client.
  gpr_slice slice = gpr_slice_from_copied_string("200 connected\r\n\r\n");
  gpr_slice_buffer_add(&cd->client_write_buffer, slice);
  grpc_endpoint_write(exec_ctx, cd->client_endpoint, &cd->client_write_buffer,
                      &cd->on_write_response_done);
}

static void on_read_request_done(grpc_exec_ctx* exec_ctx, void* arg,
                                 grpc_error* error) {
gpr_log(GPR_ERROR, "==> %s()", __func__);
  connection_data* cd = arg;
  if (error != GRPC_ERROR_NONE) {
    connection_data_failed(exec_ctx, cd, "HTTP proxy read request", error);
    return;
  }
  // Read request and feed it to the parser.
  for (size_t i = 0; i < cd->client_read_buffer.count; ++i) {
    if (GPR_SLICE_LENGTH(cd->client_read_buffer.slices[i]) > 0) {
      error = grpc_http_parser_parse(
          &cd->http_parser, cd->client_read_buffer.slices[i]);
      if (error != GRPC_ERROR_NONE) {
        connection_data_failed(exec_ctx, cd, "HTTP proxy request parse",
                               error);
        return;
      }
    }
  }
  gpr_slice_buffer_reset_and_unref(&cd->client_read_buffer);
  // If we're not done reading the request, read more data.
  if (cd->http_parser.state != GRPC_HTTP_BODY) {
    grpc_endpoint_read(exec_ctx, cd->client_endpoint, &cd->client_read_buffer,
                       &cd->on_read_request_done);
    return;
  }
  // Make sure we got a CONNECT request.
  if (strcmp(cd->http_request.method, "CONNECT") != 0) {
    char* msg;
    gpr_asprintf(&msg, "HTTP proxy got request method %s",
                 cd->http_request.method);
    error = GRPC_ERROR_CREATE(msg);
    gpr_free(msg);
    connection_data_failed(exec_ctx, cd, "HTTP proxy read request", error);
    return;
  }
  // Resolve address.
  grpc_resolved_addresses* resolved_addresses = NULL;
  error = grpc_blocking_resolve_address(cd->http_request.path, "80",
                                        &resolved_addresses);
  if (error != GRPC_ERROR_NONE) {
    connection_data_failed(exec_ctx, cd, "HTTP proxy DNS lookup", error);
    return;
  }
  GPR_ASSERT(resolved_addresses->naddrs >= 1);
  // Connect to requested address.
  const gpr_timespec deadline = gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC), gpr_time_from_seconds(10, GPR_TIMESPAN));
  grpc_tcp_client_connect(exec_ctx, &cd->on_server_connect_done,
                          &cd->server_endpoint, cd->pollset_set,
                          (struct sockaddr*)&resolved_addresses->addrs[0].addr,
                          resolved_addresses->addrs[0].len, deadline);
  grpc_resolved_addresses_destroy(resolved_addresses);
}

static void on_accept(grpc_exec_ctx* exec_ctx, void* arg,
                      grpc_endpoint* ep, grpc_pollset* accepting_pollset,
                      grpc_tcp_server_acceptor* acceptor) {
// FIXME: remove
gpr_log(GPR_ERROR, "==> %s()", __func__);
  grpc_end2end_http_proxy* proxy = arg;
  // Instantiate connection_data.
  connection_data* cd = gpr_malloc(sizeof(*cd));
  memset(cd, 0, sizeof(*cd));
  cd->client_endpoint = ep;
  cd->pollset_set = grpc_pollset_set_create();
  grpc_pollset_set_add_pollset(exec_ctx, cd->pollset_set, proxy->pollset);
  grpc_closure_init(&cd->on_read_request_done, on_read_request_done, cd);
  grpc_closure_init(&cd->on_server_connect_done, on_server_connect_done, cd);
  grpc_closure_init(&cd->on_write_response_done, on_write_response_done, cd);
  grpc_closure_init(&cd->on_client_read_done, on_client_read_done, cd);
  grpc_closure_init(&cd->on_client_write_done, on_client_write_done, cd);
  grpc_closure_init(&cd->on_server_read_done, on_server_read_done, cd);
  grpc_closure_init(&cd->on_server_write_done, on_server_write_done, cd);
  gpr_slice_buffer_init(&cd->client_read_buffer);
  gpr_slice_buffer_init(&cd->client_write_buffer);
  gpr_slice_buffer_init(&cd->server_read_buffer);
  gpr_slice_buffer_init(&cd->server_write_buffer);
  grpc_http_parser_init(&cd->http_parser, GRPC_HTTP_REQUEST,
                        &cd->http_request);
  cd->proxy = proxy;
  gpr_ref_init(&cd->refcount, 1);
  grpc_endpoint_read(exec_ctx, cd->client_endpoint, &cd->client_read_buffer,
                     &cd->on_read_request_done);
}

//
// Proxy class
//

grpc_end2end_http_proxy* grpc_end2end_http_proxy_create() {
  grpc_end2end_http_proxy* proxy = gpr_malloc(sizeof(*proxy));
  memset(proxy, 0, sizeof(*proxy));
  // Construct proxy address.
  const int proxy_port = grpc_pick_unused_port_or_die();
  gpr_join_host_port(&proxy->proxy_name, "localhost", proxy_port);
  gpr_log(GPR_INFO, "Proxy address: %s", proxy->proxy_name);
// FIXME: remove
gpr_log(GPR_ERROR, "Proxy address: %s", proxy->proxy_name);
  // Create TCP server.
  proxy->channel_args = grpc_channel_args_copy(NULL);
  grpc_error* error = grpc_tcp_server_create(
      NULL, proxy->channel_args, &proxy->server);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  // Bind to port.
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  grpc_sockaddr_set_port((struct sockaddr*)&addr, proxy_port);
  int port;
  error = grpc_tcp_server_add_port(
      proxy->server, (struct sockaddr*)&addr, sizeof(addr), &port);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(port == proxy_port);
  // Start server.
  proxy->pollset = gpr_malloc(grpc_pollset_size());
  grpc_pollset_init(proxy->pollset, &proxy->mu);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_tcp_server_start(&exec_ctx, proxy->server, &proxy->pollset, 1,
                        on_accept, proxy);
  grpc_exec_ctx_finish(&exec_ctx);
#if 0
  // Start proxy thread.
  gpr_thd_options opt = gpr_thd_options_default();
  gpr_thd_options_set_joinable(&opt);
  GPR_ASSERT(gpr_thd_new(&proxy->thd, thread_main, proxy, &opt));
#endif
  return proxy;
}

static void destroy_pollset(grpc_exec_ctx *exec_ctx, void *p,
                            grpc_error *error) {
  grpc_pollset_destroy(p);
}

// FIXME: remove (including all references below)
//#define USE_THREAD 1

void grpc_end2end_http_proxy_destroy(grpc_end2end_http_proxy* proxy) {
gpr_log(GPR_ERROR, "==> %s()", __func__);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_tcp_server_shutdown_listeners(&exec_ctx, proxy->server);
  grpc_tcp_server_unref(&exec_ctx, proxy->server);
#ifdef USE_THREAD
  gpr_thd_join(proxy->thd);
#endif
  gpr_free(proxy->proxy_name);
  grpc_channel_args_destroy(proxy->channel_args);
  grpc_closure destroyed;
  grpc_closure_init(&destroyed, destroy_pollset, proxy->pollset);
  grpc_pollset_shutdown(&exec_ctx, proxy->pollset, &destroyed);
  gpr_free(proxy);
  grpc_exec_ctx_finish(&exec_ctx);
}

const char *grpc_end2end_http_proxy_get_proxy_name(
    grpc_end2end_http_proxy *proxy) {
  return proxy->proxy_name;
}

static void thread_main(void* arg) {
gpr_log(GPR_ERROR, "==> %s()", __func__);
  grpc_end2end_http_proxy *proxy = arg;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  do {
gpr_log(GPR_ERROR, "HERE a");
    const gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
    const gpr_timespec deadline =
        gpr_time_add(now, gpr_time_from_seconds(5, GPR_TIMESPAN));
    grpc_pollset_worker *worker = NULL;
gpr_log(GPR_ERROR, "HERE b");
    gpr_mu_lock(proxy->mu);
gpr_log(GPR_ERROR, "HERE c");
    GRPC_LOG_IF_ERROR("grpc_pollset_work",
                      grpc_pollset_work(&exec_ctx, proxy->pollset, &worker,
                      now, deadline));
gpr_log(GPR_ERROR, "HERE d");
    gpr_mu_unlock(proxy->mu);
gpr_log(GPR_ERROR, "HERE e");
    grpc_exec_ctx_flush(&exec_ctx);
gpr_log(GPR_ERROR, "HERE f");
  } while (!proxy->shutdown);
gpr_log(GPR_ERROR, "HERE g");
  grpc_exec_ctx_finish(&exec_ctx);
}

void grpc_end2end_http_proxy_start_thread(grpc_end2end_http_proxy *proxy) {
#ifdef USE_THREAD
  gpr_thd_options opt = gpr_thd_options_default();
  gpr_thd_options_set_joinable(&opt);
  GPR_ASSERT(gpr_thd_new(&proxy->thd, thread_main, proxy, &opt));
#else
  thread_main(proxy);
#endif
}
