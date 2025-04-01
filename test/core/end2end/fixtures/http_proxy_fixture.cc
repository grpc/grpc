//
//
// Copyright 2016 gRPC authors.
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

#include "test/core/end2end/fixtures/http_proxy_fixture.h"

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/sync.h>
#include <limits.h>
#include <string.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/resolved_address_internal.h"
#include "src/core/lib/event_engine/utils.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/util/host_port.h"
#include "src/core/util/http_client/parser.h"
#include "src/core/util/memory.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/thd.h"
#include "src/core/util/time.h"
#include "test/core/test_util/port.h"

struct grpc_end2end_http_proxy {
  grpc_end2end_http_proxy()
      : combiner(grpc_combiner_create(
            grpc_event_engine::experimental::GetDefaultEventEngine())) {}
  int port;
  std::string proxy_name;
  std::atomic<bool> is_shutdown{false};
  std::atomic<size_t> users{1};
  grpc_core::Thread thd;
  grpc_tcp_server* server = nullptr;
  const grpc_channel_args* channel_args = nullptr;
  gpr_mu* mu = nullptr;
  std::vector<grpc_pollset*> pollset;
  grpc_core::Combiner* combiner = nullptr;
  std::atomic<size_t> num_connections{0};
};

namespace {

// Sometimes, on_accept may be called after thread_main has returned, and the
// proxy will have already been destroyed. This value is reset every time a
// proxy fixture is created, and it prevents a crash due to a repeated unref.
std::atomic<bool> proxy_destroyed{false};

void proxy_ref(grpc_end2end_http_proxy* proxy) { proxy->users.fetch_add(1); }

// Returns the remaining number of outstanding refs
size_t proxy_unref(grpc_end2end_http_proxy* proxy) {
  if (proxy_destroyed.load()) return -1;
  size_t ref_count = proxy->users.fetch_sub(1) - 1;
  if (ref_count == 0) {
    proxy_destroyed.store(true);
    GRPC_COMBINER_UNREF(proxy->combiner, "test");
    delete proxy;
  }
  return ref_count;
}

}  // namespace

//
// Connection handling
//

// proxy_connection structure is only accessed in the closures which are all
// scheduled under the same combiner lock. So there is no need for a mutex to
// protect this structure.
typedef struct proxy_connection {
  grpc_end2end_http_proxy* proxy;

  grpc_endpoint* client_endpoint;
  grpc_endpoint* server_endpoint;

  gpr_refcount refcount;

  grpc_pollset_set* pollset_set;

  // NOTE: All the closures execute under proxy->combiner lock. Which means
  // there will not be any data-races between the closures
  grpc_closure on_read_request_done;
  grpc_closure on_server_connect_done;
  grpc_closure on_write_response_done;
  grpc_closure on_client_read_done;
  grpc_closure on_client_write_done;
  grpc_closure on_server_read_done;
  grpc_closure on_server_write_done;

  bool client_read_failed : 1;
  bool client_write_failed : 1;
  bool client_shutdown : 1;
  bool server_read_failed : 1;
  bool server_write_failed : 1;
  bool server_shutdown : 1;

  grpc_slice_buffer client_read_buffer;
  grpc_slice_buffer client_deferred_write_buffer;
  bool client_is_writing;
  grpc_slice_buffer client_write_buffer;
  grpc_slice_buffer server_read_buffer;
  grpc_slice_buffer server_deferred_write_buffer;
  bool server_is_writing;
  grpc_slice_buffer server_write_buffer;

  grpc_http_parser http_parser;
  grpc_http_request http_request;
} proxy_connection;

static void proxy_connection_ref(proxy_connection* conn,
                                 const char* /*reason*/) {
  gpr_ref(&conn->refcount);
}

// Helper function to destroy the proxy connection.
static void proxy_connection_unref(proxy_connection* conn,
                                   const char* /*reason*/) {
  if (gpr_unref(&conn->refcount)) {
    if (conn->client_endpoint != nullptr) {
      grpc_endpoint_destroy(conn->client_endpoint);
    }
    if (conn->server_endpoint != nullptr) {
      grpc_endpoint_destroy(conn->server_endpoint);
    }
    grpc_pollset_set_destroy(conn->pollset_set);
    grpc_slice_buffer_destroy(&conn->client_read_buffer);
    grpc_slice_buffer_destroy(&conn->client_deferred_write_buffer);
    grpc_slice_buffer_destroy(&conn->client_write_buffer);
    grpc_slice_buffer_destroy(&conn->server_read_buffer);
    grpc_slice_buffer_destroy(&conn->server_deferred_write_buffer);
    grpc_slice_buffer_destroy(&conn->server_write_buffer);
    grpc_http_parser_destroy(&conn->http_parser);
    grpc_http_request_destroy(&conn->http_request);
    proxy_unref(conn->proxy);
    gpr_free(conn);
  }
}

enum failure_type {
  SETUP_FAILED,  // To be used before we start proxying.
  CLIENT_READ_FAILED,
  CLIENT_WRITE_FAILED,
  SERVER_READ_FAILED,
  SERVER_WRITE_FAILED,
};

// Forward declarations
static void on_client_write_done(void* arg, grpc_error_handle error);
static void on_server_write_done(void* arg, grpc_error_handle error);
static void on_client_read_done(void* arg, grpc_error_handle error);
static void on_server_read_done(void* arg, grpc_error_handle error);
static void on_server_connect_done(void* arg, grpc_error_handle error);
static void on_read_request_done(void* arg, grpc_error_handle error);

static void on_client_write_done_locked(void* arg, grpc_error_handle error);
static void on_server_write_done_locked(void* arg, grpc_error_handle error);
static void on_client_read_done_locked(void* arg, grpc_error_handle error);
static void on_server_read_done_locked(void* arg, grpc_error_handle error);
static void on_server_connect_done_locked(void* arg, grpc_error_handle error);
static void on_read_request_done_locked(void* arg, grpc_error_handle error);

// Helper function to shut down the proxy connection.
static void proxy_connection_failed(proxy_connection* conn,
                                    failure_type failure, const char* prefix,
                                    grpc_error_handle error) {
  LOG(INFO) << prefix << ": " << grpc_core::StatusToString(error);
  // Decide whether we should shut down the client and server.
  bool shutdown_client = false;
  bool shutdown_server = false;
  if (failure == SETUP_FAILED) {
    shutdown_client = true;
    shutdown_server = true;
  } else {
    if ((failure == CLIENT_READ_FAILED && conn->client_write_failed) ||
        (failure == CLIENT_WRITE_FAILED && conn->client_read_failed) ||
        (failure == SERVER_READ_FAILED && !conn->client_is_writing)) {
      shutdown_client = true;
    }
    if ((failure == SERVER_READ_FAILED && conn->server_write_failed) ||
        (failure == SERVER_WRITE_FAILED && conn->server_read_failed) ||
        (failure == CLIENT_READ_FAILED && !conn->server_is_writing)) {
      shutdown_server = true;
    }
  }
  // If we decided to shut down either one and have not yet done so, do so.
  if (shutdown_client && !conn->client_shutdown &&
      conn->client_endpoint != nullptr) {
    grpc_endpoint_destroy(conn->client_endpoint);
    conn->client_endpoint = nullptr;
    conn->client_shutdown = true;
  }
  if (shutdown_server && !conn->server_shutdown &&
      conn->server_endpoint != nullptr) {
    grpc_endpoint_destroy(conn->server_endpoint);
    conn->server_endpoint = nullptr;
    conn->server_shutdown = true;
  }
  // Unref the connection.
  proxy_connection_unref(conn, "conn_failed");
}

// Callback for writing proxy data to the client.
static void on_client_write_done_locked(void* arg, grpc_error_handle error) {
  proxy_connection* conn = static_cast<proxy_connection*>(arg);
  conn->client_is_writing = false;
  if (!error.ok()) {
    conn->client_write_failed = true;
    proxy_connection_failed(conn, CLIENT_WRITE_FAILED,
                            "HTTP proxy client write", error);
    return;
  }
  if (conn->server_read_failed) {
    grpc_endpoint_destroy(conn->client_endpoint);
    conn->client_endpoint = nullptr;
    // No more writes.  Unref the connection.
    proxy_connection_unref(conn, "client_write");
    return;
  }
  // Clear write buffer (the data we just wrote).
  grpc_slice_buffer_reset_and_unref(&conn->client_write_buffer);
  // If more data was read from the server since we started this write,
  // write that data now.
  if (conn->client_deferred_write_buffer.length > 0 &&
      conn->client_endpoint != nullptr) {
    grpc_slice_buffer_move_into(&conn->client_deferred_write_buffer,
                                &conn->client_write_buffer);
    conn->client_is_writing = true;
    GRPC_CLOSURE_INIT(&conn->on_client_write_done, on_client_write_done, conn,
                      grpc_schedule_on_exec_ctx);
    grpc_endpoint_write(conn->client_endpoint, &conn->client_write_buffer,
                        &conn->on_client_write_done, nullptr,
                        /*max_frame_size=*/INT_MAX);
  } else {
    // No more writes.  Unref the connection.
    proxy_connection_unref(conn, "write_done");
  }
}

static void on_client_write_done(void* arg, grpc_error_handle error) {
  proxy_connection* conn = static_cast<proxy_connection*>(arg);
  GRPC_CLOSURE_INIT(&conn->on_client_write_done, on_client_write_done_locked,
                    conn, nullptr);
  conn->proxy->combiner->Run(&conn->on_client_write_done, error);
}

// Callback for writing proxy data to the backend server.
static void on_server_write_done_locked(void* arg, grpc_error_handle error) {
  proxy_connection* conn = static_cast<proxy_connection*>(arg);
  conn->server_is_writing = false;
  if (!error.ok()) {
    conn->server_write_failed = true;
    proxy_connection_failed(conn, SERVER_WRITE_FAILED,
                            "HTTP proxy server write", error);
    return;
  }
  if (conn->client_read_failed) {
    grpc_endpoint_destroy(conn->server_endpoint);
    conn->server_endpoint = nullptr;
    // No more writes.  Unref the connection.
    proxy_connection_unref(conn, "server_write");
    return;
  }
  // Clear write buffer (the data we just wrote).
  grpc_slice_buffer_reset_and_unref(&conn->server_write_buffer);
  // If more data was read from the client since we started this write,
  // write that data now.
  if (conn->server_deferred_write_buffer.length > 0 &&
      conn->server_endpoint != nullptr) {
    grpc_slice_buffer_move_into(&conn->server_deferred_write_buffer,
                                &conn->server_write_buffer);
    conn->server_is_writing = true;
    GRPC_CLOSURE_INIT(&conn->on_server_write_done, on_server_write_done, conn,
                      grpc_schedule_on_exec_ctx);
    grpc_endpoint_write(conn->server_endpoint, &conn->server_write_buffer,
                        &conn->on_server_write_done, nullptr,
                        /*max_frame_size=*/INT_MAX);
  } else {
    // No more writes.  Unref the connection.
    proxy_connection_unref(conn, "server_write");
  }
}

static void on_server_write_done(void* arg, grpc_error_handle error) {
  proxy_connection* conn = static_cast<proxy_connection*>(arg);
  GRPC_CLOSURE_INIT(&conn->on_server_write_done, on_server_write_done_locked,
                    conn, nullptr);
  conn->proxy->combiner->Run(&conn->on_server_write_done, error);
}

// Callback for reading data from the client, which will be proxied to
// the backend server.
static void on_client_read_done_locked(void* arg, grpc_error_handle error) {
  proxy_connection* conn = static_cast<proxy_connection*>(arg);
  if (!error.ok()) {
    conn->client_read_failed = true;
    proxy_connection_failed(conn, CLIENT_READ_FAILED, "HTTP proxy client read",
                            error);
    return;
  }
  // If there is already a pending write (i.e., server_write_buffer is
  // not empty), then move the read data into server_deferred_write_buffer,
  // and the next write will be requested in on_server_write_done(), when
  // the current write is finished.
  //
  // Otherwise, move the read data into the write buffer and write it.
  if (conn->server_is_writing) {
    grpc_slice_buffer_move_into(&conn->client_read_buffer,
                                &conn->server_deferred_write_buffer);
  } else if (conn->server_endpoint != nullptr) {
    grpc_slice_buffer_move_into(&conn->client_read_buffer,
                                &conn->server_write_buffer);
    proxy_connection_ref(conn, "client_read");
    conn->server_is_writing = true;
    GRPC_CLOSURE_INIT(&conn->on_server_write_done, on_server_write_done, conn,
                      grpc_schedule_on_exec_ctx);
    grpc_endpoint_write(conn->server_endpoint, &conn->server_write_buffer,
                        &conn->on_server_write_done, nullptr,
                        /*max_frame_size=*/INT_MAX);
  }
  if (conn->client_endpoint == nullptr) {
    proxy_connection_unref(conn, "client_read");
    return;
  }
  // Read more data.
  GRPC_CLOSURE_INIT(&conn->on_client_read_done, on_client_read_done, conn,
                    grpc_schedule_on_exec_ctx);
  grpc_endpoint_read(conn->client_endpoint, &conn->client_read_buffer,
                     &conn->on_client_read_done, /*urgent=*/false,
                     /*min_progress_size=*/1);
}

static void on_client_read_done(void* arg, grpc_error_handle error) {
  proxy_connection* conn = static_cast<proxy_connection*>(arg);
  GRPC_CLOSURE_INIT(&conn->on_client_read_done, on_client_read_done_locked,
                    conn, nullptr);
  conn->proxy->combiner->Run(&conn->on_client_read_done, error);
}

// Callback for reading data from the backend server, which will be
// proxied to the client.
static void on_server_read_done_locked(void* arg, grpc_error_handle error) {
  proxy_connection* conn = static_cast<proxy_connection*>(arg);
  if (!error.ok()) {
    conn->server_read_failed = true;
    proxy_connection_failed(conn, SERVER_READ_FAILED, "HTTP proxy server read",
                            error);
    return;
  }
  // If there is already a pending write (i.e., client_write_buffer is
  // not empty), then move the read data into client_deferred_write_buffer,
  // and the next write will be requested in on_client_write_done(), when
  // the current write is finished.
  //
  // Otherwise, move the read data into the write buffer and write it.
  if (conn->client_is_writing) {
    grpc_slice_buffer_move_into(&conn->server_read_buffer,
                                &conn->client_deferred_write_buffer);
  } else if (conn->client_endpoint != nullptr) {
    grpc_slice_buffer_move_into(&conn->server_read_buffer,
                                &conn->client_write_buffer);
    proxy_connection_ref(conn, "server_read");
    conn->client_is_writing = true;
    GRPC_CLOSURE_INIT(&conn->on_client_write_done, on_client_write_done, conn,
                      grpc_schedule_on_exec_ctx);
    grpc_endpoint_write(conn->client_endpoint, &conn->client_write_buffer,
                        &conn->on_client_write_done, nullptr,
                        /*max_frame_size=*/INT_MAX);
  }
  if (conn->server_endpoint == nullptr) {
    proxy_connection_unref(conn, "server_read");
    return;
  }
  // Read more data.
  GRPC_CLOSURE_INIT(&conn->on_server_read_done, on_server_read_done, conn,
                    grpc_schedule_on_exec_ctx);
  grpc_endpoint_read(conn->server_endpoint, &conn->server_read_buffer,
                     &conn->on_server_read_done, /*urgent=*/false,
                     /*min_progress_size=*/1);
}

static void on_server_read_done(void* arg, grpc_error_handle error) {
  proxy_connection* conn = static_cast<proxy_connection*>(arg);
  GRPC_CLOSURE_INIT(&conn->on_server_read_done, on_server_read_done_locked,
                    conn, nullptr);
  conn->proxy->combiner->Run(&conn->on_server_read_done, error);
}

// Callback to write the HTTP response for the CONNECT request.
static void on_write_response_done_locked(void* arg, grpc_error_handle error) {
  proxy_connection* conn = static_cast<proxy_connection*>(arg);
  conn->client_is_writing = false;
  if (!error.ok()) {
    proxy_connection_failed(conn, SETUP_FAILED, "HTTP proxy write response",
                            error);
    return;
  }
  // Clear write buffer.
  grpc_slice_buffer_reset_and_unref(&conn->client_write_buffer);
  // Start reading from both client and server.  One of the read
  // requests inherits our ref to conn, but we need to take a new ref
  // for the other one.
  proxy_connection_ref(conn, "client_read");
  proxy_connection_ref(conn, "server_read");
  proxy_connection_unref(conn, "write_response");
  GRPC_CLOSURE_INIT(&conn->on_client_read_done, on_client_read_done, conn,
                    grpc_schedule_on_exec_ctx);
  grpc_endpoint_read(conn->client_endpoint, &conn->client_read_buffer,
                     &conn->on_client_read_done, /*urgent=*/false,
                     /*min_progress_size=*/1);
  GRPC_CLOSURE_INIT(&conn->on_server_read_done, on_server_read_done, conn,
                    grpc_schedule_on_exec_ctx);
  grpc_endpoint_read(conn->server_endpoint, &conn->server_read_buffer,
                     &conn->on_server_read_done, /*urgent=*/false,
                     /*min_progress_size=*/1);
}

static void on_write_response_done(void* arg, grpc_error_handle error) {
  proxy_connection* conn = static_cast<proxy_connection*>(arg);
  GRPC_CLOSURE_INIT(&conn->on_write_response_done,
                    on_write_response_done_locked, conn, nullptr);
  conn->proxy->combiner->Run(&conn->on_write_response_done, error);
}

// Callback to connect to the backend server specified by the HTTP
// CONNECT request.
static void on_server_connect_done_locked(void* arg, grpc_error_handle error) {
  proxy_connection* conn = static_cast<proxy_connection*>(arg);
  if (!error.ok()) {
    // TODO(roth): Technically, in this case, we should handle the error
    // by returning an HTTP response to the client indicating that the
    // connection failed.  However, for the purposes of this test code,
    // it's fine to pretend this is a client-side error, which will
    // cause the client connection to be dropped.
    proxy_connection_failed(conn, SETUP_FAILED, "HTTP proxy server connect",
                            error);
    return;
  }
  // We've established a connection, so send back a 200 response code to
  // the client.
  // The write callback inherits our reference to conn.
  grpc_slice slice =
      grpc_slice_from_copied_string("HTTP/1.0 200 connected\r\n\r\n");
  grpc_slice_buffer_add(&conn->client_write_buffer, slice);
  conn->client_is_writing = true;
  GRPC_CLOSURE_INIT(&conn->on_write_response_done, on_write_response_done, conn,
                    grpc_schedule_on_exec_ctx);
  grpc_endpoint_write(conn->client_endpoint, &conn->client_write_buffer,
                      &conn->on_write_response_done, nullptr,
                      /*max_frame_size=*/INT_MAX);
}

static void on_server_connect_done(void* arg, grpc_error_handle error) {
  proxy_connection* conn = static_cast<proxy_connection*>(arg);
  GRPC_CLOSURE_INIT(&conn->on_server_connect_done,
                    on_server_connect_done_locked, conn, nullptr);
  conn->proxy->combiner->Run(&conn->on_server_connect_done, error);
}

///
/// Parses the proxy auth header value to check if it matches :-
/// Basic <base64_encoded_expected_cred>
/// Returns true if it matches, false otherwise
///
static bool proxy_auth_header_matches(absl::string_view proxy_auth_header_val,
                                      absl::string_view expected_cred) {
  if (!absl::ConsumePrefix(&proxy_auth_header_val, "Basic ")) {
    return false;
  }
  std::string decoded;
  absl::Base64Unescape(proxy_auth_header_val, &decoded);
  return expected_cred == decoded;
}

// Callback to read the HTTP CONNECT request.
// TODO(roth): Technically, for any of the failure modes handled by this
// function, we should handle the error by returning an HTTP response to
// the client indicating that the request failed.  However, for the purposes
// of this test code, it's fine to pretend this is a client-side error,
// which will cause the client connection to be dropped.
static void on_read_request_done_locked(void* arg, grpc_error_handle error) {
  proxy_connection* conn = static_cast<proxy_connection*>(arg);
  VLOG(2) << "on_read_request_done: " << conn << " "
          << grpc_core::StatusToString(error);
  if (!error.ok()) {
    proxy_connection_failed(conn, SETUP_FAILED, "HTTP proxy read request",
                            error);
    return;
  }
  // Read request and feed it to the parser.
  for (size_t i = 0; i < conn->client_read_buffer.count; ++i) {
    if (GRPC_SLICE_LENGTH(conn->client_read_buffer.slices[i]) > 0) {
      error = grpc_http_parser_parse(
          &conn->http_parser, conn->client_read_buffer.slices[i], nullptr);
      if (!error.ok()) {
        proxy_connection_failed(conn, SETUP_FAILED, "HTTP proxy request parse",
                                error);
        return;
      }
    }
  }
  grpc_slice_buffer_reset_and_unref(&conn->client_read_buffer);
  // If we're not done reading the request, read more data.
  if (conn->http_parser.state != GRPC_HTTP_BODY) {
    GRPC_CLOSURE_INIT(&conn->on_read_request_done, on_read_request_done, conn,
                      grpc_schedule_on_exec_ctx);
    grpc_endpoint_read(conn->client_endpoint, &conn->client_read_buffer,
                       &conn->on_read_request_done, /*urgent=*/false,
                       /*min_progress_size=*/1);
    return;
  }
  // Make sure we got a CONNECT request.
  if (strcmp(conn->http_request.method, "CONNECT") != 0) {
    error = GRPC_ERROR_CREATE(absl::StrCat("HTTP proxy got request method ",
                                           conn->http_request.method));
    proxy_connection_failed(conn, SETUP_FAILED, "HTTP proxy read request",
                            error);
    return;
  }
  // If proxy auth is being used, check if the header is present and as expected
  const grpc_arg* proxy_auth_arg = grpc_channel_args_find(
      conn->proxy->channel_args, GRPC_ARG_HTTP_PROXY_AUTH_CREDS);
  char* proxy_auth_str = grpc_channel_arg_get_string(proxy_auth_arg);
  if (proxy_auth_str != nullptr) {
    bool client_authenticated = false;
    for (size_t i = 0; i < conn->http_request.hdr_count; i++) {
      if (strcmp(conn->http_request.hdrs[i].key, "Proxy-Authorization") == 0) {
        client_authenticated = proxy_auth_header_matches(
            conn->http_request.hdrs[i].value, proxy_auth_str);
        break;
      }
    }
    if (!client_authenticated) {
      const char* msg = "HTTP Connect could not verify authentication";
      error = GRPC_ERROR_CREATE(msg);
      proxy_connection_failed(conn, SETUP_FAILED, "HTTP proxy read request",
                              error);
      return;
    }
  }
  // Resolve address.
  grpc_resolved_address first_address;
  VLOG(2) << "proxy connecting to backend: " << conn->http_request.path;
  if (grpc_core::IsEventEngineDnsNonClientChannelEnabled()) {
    auto resolver = conn->proxy->combiner->event_engine->GetDNSResolver(
        grpc_event_engine::experimental::EventEngine::DNSResolver::
            ResolverOptions());
    if (!resolver.ok()) {
      proxy_connection_failed(conn, SETUP_FAILED, "HTTP proxy DNS lookup",
                              error);
      return;
    }
    auto ee_addresses = grpc_event_engine::experimental::LookupHostnameBlocking(
        resolver->get(), conn->http_request.path, "80");
    if (!ee_addresses.ok()) {
      proxy_connection_failed(conn, SETUP_FAILED, "HTTP proxy DNS lookup",
                              error);
      return;
    }
    CHECK(!ee_addresses->empty());
    first_address = CreateGRPCResolvedAddress(ee_addresses->at(0));
  } else {
    absl::StatusOr<std::vector<grpc_resolved_address>> addresses_or =
        grpc_core::GetDNSResolver()->LookupHostnameBlocking(
            conn->http_request.path, "80");
    if (!addresses_or.ok()) {
      proxy_connection_failed(conn, SETUP_FAILED, "HTTP proxy DNS lookup",
                              error);
      return;
    }
    CHECK(!addresses_or->empty());
    first_address = addresses_or->at(0);
  }  // Connect to requested address.
  // The connection callback inherits our reference to conn.
  const grpc_core::Timestamp deadline =
      grpc_core::Timestamp::Now() + grpc_core::Duration::Seconds(10);
  GRPC_CLOSURE_INIT(&conn->on_server_connect_done, on_server_connect_done, conn,
                    grpc_schedule_on_exec_ctx);
  auto args = grpc_core::CoreConfiguration::Get()
                  .channel_args_preconditioning()
                  .PreconditionChannelArgs(nullptr);
  grpc_tcp_client_connect(
      &conn->on_server_connect_done, &conn->server_endpoint, conn->pollset_set,
      grpc_event_engine::experimental::ChannelArgsEndpointConfig(args),
      &first_address, deadline);
}

static void on_read_request_done(void* arg, grpc_error_handle error) {
  proxy_connection* conn = static_cast<proxy_connection*>(arg);
  GRPC_CLOSURE_INIT(&conn->on_read_request_done, on_read_request_done_locked,
                    conn, nullptr);
  conn->proxy->combiner->Run(&conn->on_read_request_done, error);
}

static void on_accept(void* arg, grpc_endpoint* endpoint,
                      grpc_pollset* /*accepting_pollset*/,
                      grpc_tcp_server_acceptor* acceptor) {
  gpr_free(acceptor);
  if (proxy_destroyed.load()) {
    grpc_endpoint_destroy(endpoint);
    return;
  }
  grpc_end2end_http_proxy* proxy = static_cast<grpc_end2end_http_proxy*>(arg);
  proxy_ref(proxy);
  if (proxy->is_shutdown.load()) {
    grpc_endpoint_destroy(endpoint);
    proxy_unref(proxy);
    return;
  }
  proxy->num_connections.fetch_add(1);
  // Instantiate proxy_connection.
  proxy_connection* conn = grpc_core::Zalloc<proxy_connection>();
  conn->client_endpoint = endpoint;
  conn->proxy = proxy;
  gpr_ref_init(&conn->refcount, 1);
  conn->pollset_set = grpc_pollset_set_create();
  grpc_pollset_set_add_pollset(conn->pollset_set, proxy->pollset[0]);
  grpc_endpoint_add_to_pollset_set(endpoint, conn->pollset_set);
  grpc_slice_buffer_init(&conn->client_read_buffer);
  grpc_slice_buffer_init(&conn->client_deferred_write_buffer);
  conn->client_is_writing = false;
  grpc_slice_buffer_init(&conn->client_write_buffer);
  grpc_slice_buffer_init(&conn->server_read_buffer);
  grpc_slice_buffer_init(&conn->server_deferred_write_buffer);
  conn->server_is_writing = false;
  grpc_slice_buffer_init(&conn->server_write_buffer);
  grpc_http_parser_init(&conn->http_parser, GRPC_HTTP_REQUEST,
                        &conn->http_request);
  GRPC_CLOSURE_INIT(&conn->on_read_request_done, on_read_request_done, conn,
                    grpc_schedule_on_exec_ctx);
  grpc_endpoint_read(conn->client_endpoint, &conn->client_read_buffer,
                     &conn->on_read_request_done, /*urgent=*/false,
                     /*min_progress_size=*/1);
}

//
// Proxy class
//

static void thread_main(void* arg) {
  grpc_end2end_http_proxy* proxy = static_cast<grpc_end2end_http_proxy*>(arg);
  grpc_core::ExecCtx exec_ctx;
  do {
    proxy_ref(proxy);
    grpc_pollset_worker* worker = nullptr;
    gpr_mu_lock(proxy->mu);
    GRPC_LOG_IF_ERROR(
        "grpc_pollset_work",
        grpc_pollset_work(proxy->pollset[0], &worker,
                          grpc_core::Timestamp::Now() +
                              grpc_core::Duration::Milliseconds(100)));
    gpr_mu_unlock(proxy->mu);
    grpc_core::ExecCtx::Get()->Flush();
  } while (proxy_unref(proxy) > 1 || !proxy->is_shutdown.load());
}

grpc_end2end_http_proxy* grpc_end2end_http_proxy_create(
    const grpc_channel_args* args) {
  grpc_core::ExecCtx exec_ctx;
  grpc_end2end_http_proxy* proxy = new grpc_end2end_http_proxy();
  proxy_destroyed.store(false);
  // Construct proxy address.
  proxy->port = grpc_pick_unused_port_or_die();
  proxy->proxy_name = grpc_core::JoinHostPort("localhost", proxy->port);
  VLOG(2) << "Proxy address: " << proxy->proxy_name;
  // Create TCP server.
  auto channel_args = grpc_core::CoreConfiguration::Get()
                          .channel_args_preconditioning()
                          .PreconditionChannelArgs(args);
  proxy->channel_args = channel_args.ToC().release();
  grpc_error_handle error = grpc_tcp_server_create(
      nullptr,
      grpc_event_engine::experimental::ChannelArgsEndpointConfig(channel_args),
      on_accept, proxy, &proxy->server);
  CHECK_OK(error);
  // Bind to port.
  grpc_resolved_address resolved_addr;
  grpc_sockaddr_in* addr =
      reinterpret_cast<grpc_sockaddr_in*>(resolved_addr.addr);
  memset(&resolved_addr, 0, sizeof(resolved_addr));
  resolved_addr.len = sizeof(grpc_sockaddr_in);
  addr->sin_family = GRPC_AF_INET;
  grpc_sockaddr_set_port(&resolved_addr, proxy->port);
  int port;
  error = grpc_tcp_server_add_port(proxy->server, &resolved_addr, &port);
  CHECK_OK(error);
  CHECK(port == proxy->port);
  // Start server.
  auto* pollset = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
  grpc_pollset_init(pollset, &proxy->mu);
  proxy->pollset.push_back(pollset);
  grpc_tcp_server_start(proxy->server, &proxy->pollset);

  // Start proxy thread.
  proxy->thd = grpc_core::Thread("grpc_http_proxy", thread_main, proxy);
  proxy->thd.Start();
  return proxy;
}

static void destroy_pollset(void* arg, grpc_error_handle /*error*/) {
  grpc_pollset* pollset = static_cast<grpc_pollset*>(arg);
  grpc_pollset_destroy(pollset);
  gpr_free(pollset);
}

void grpc_end2end_http_proxy_destroy(grpc_end2end_http_proxy* proxy) {
  proxy->is_shutdown.store(true);
  grpc_core::ExecCtx exec_ctx;
  proxy->thd.Join();
  grpc_tcp_server_shutdown_listeners(proxy->server);
  grpc_tcp_server_unref(proxy->server);
  grpc_channel_args_destroy(proxy->channel_args);
  grpc_pollset_shutdown(proxy->pollset[0],
                        GRPC_CLOSURE_CREATE(destroy_pollset, proxy->pollset[0],
                                            grpc_schedule_on_exec_ctx));
  proxy_unref(proxy);
}

const char* grpc_end2end_http_proxy_get_proxy_name(
    grpc_end2end_http_proxy* proxy) {
  return proxy->proxy_name.c_str();
}

int grpc_end2end_http_proxy_get_proxy_port(grpc_end2end_http_proxy* proxy) {
  return proxy->port;
}

size_t grpc_end2end_http_proxy_num_connections(grpc_end2end_http_proxy* proxy) {
  return proxy->num_connections.load();
}
