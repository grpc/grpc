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

// This test won't work except with posix sockets enabled
#ifdef GRPC_POSIX_SOCKET

#include <arpa/inet.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/load_file.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#define SSL_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SSL_KEY_PATH "src/core/tsi/test_creds/server1.key"
#define SSL_CA_PATH "src/core/tsi/test_creds/ca.pem"

// Arguments for TLS server thread.
typedef struct {
  int socket;
  char* alpn_preferred;
} server_args;

// Based on https://wiki.openssl.org/index.php/Simple_TLS_Server.
// Pick an arbitrary unused port and return it in *out_port. Return
// an fd>=0 on success.
static int create_socket(int* out_port) {
  int s;
  struct sockaddr_in addr;
  socklen_t addr_len;
  *out_port = -1;

  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) {
    perror("Unable to create socket");
    return -1;
  }

  if (bind(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    perror("Unable to bind");
    gpr_log(GPR_ERROR, "%s", "Unable to bind to any port");
    close(s);
    return -1;
  }

  if (listen(s, 1) < 0) {
    perror("Unable to listen");
    close(s);
    return -1;
  }

  addr_len = sizeof(addr);
  if (getsockname(s, reinterpret_cast<struct sockaddr*>(&addr), &addr_len) !=
          0 ||
      addr_len > sizeof(addr)) {
    perror("getsockname");
    gpr_log(GPR_ERROR, "%s", "Unable to get socket local address");
    close(s);
    return -1;
  }

  *out_port = ntohs(addr.sin_port);
  return s;
}

// Server callback during ALPN negotiation. See man page for
// SSL_CTX_set_alpn_select_cb.
static int alpn_select_cb(SSL* ssl, const uint8_t** out, uint8_t* out_len,
                          const uint8_t* in, unsigned in_len, void* arg) {
  const uint8_t* alpn_preferred = static_cast<const uint8_t*>(arg);

  *out = alpn_preferred;
  *out_len = static_cast<uint8_t>(strlen((char*)alpn_preferred));

  // Validate that the ALPN list includes "h2" and "grpc-exp", that "grpc-exp"
  // precedes "h2".
  bool grpc_exp_seen = false;
  bool h2_seen = false;
  const char* inp = reinterpret_cast<const char*>(in);
  const char* in_end = inp + in_len;
  while (inp < in_end) {
    const size_t length = static_cast<size_t>(*inp++);
    if (length == strlen("grpc-exp") && strncmp(inp, "grpc-exp", length) == 0) {
      grpc_exp_seen = true;
      GPR_ASSERT(!h2_seen);
    }
    if (length == strlen("h2") && strncmp(inp, "h2", length) == 0) {
      h2_seen = true;
      GPR_ASSERT(grpc_exp_seen);
    }
    inp += length;
  }

  GPR_ASSERT(inp == in_end);
  GPR_ASSERT(grpc_exp_seen);
  GPR_ASSERT(h2_seen);

  return SSL_TLSEXT_ERR_OK;
}

// Minimal TLS server. This is largely based on the example at
// https://wiki.openssl.org/index.php/Simple_TLS_Server and the gRPC core
// internals in src/core/tsi/ssl_transport_security.c.
static void server_thread(void* arg) {
  const server_args* args = static_cast<server_args*>(arg);

  SSL_load_error_strings();
  OpenSSL_add_ssl_algorithms();

  const SSL_METHOD* method = TLSv1_2_server_method();
  SSL_CTX* ctx = SSL_CTX_new(method);
  if (!ctx) {
    perror("Unable to create SSL context");
    ERR_print_errors_fp(stderr);
    abort();
  }

  // Load key pair.
  if (SSL_CTX_use_certificate_file(ctx, SSL_CERT_PATH, SSL_FILETYPE_PEM) < 0) {
    ERR_print_errors_fp(stderr);
    abort();
  }
  if (SSL_CTX_use_PrivateKey_file(ctx, SSL_KEY_PATH, SSL_FILETYPE_PEM) < 0) {
    ERR_print_errors_fp(stderr);
    abort();
  }

  // Set the cipher list to match the one expressed in
  // src/core/tsi/ssl_transport_security.c.
  const char* cipher_list =
      "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-"
      "SHA384:ECDHE-RSA-AES256-GCM-SHA384";
  if (!SSL_CTX_set_cipher_list(ctx, cipher_list)) {
    ERR_print_errors_fp(stderr);
    gpr_log(GPR_ERROR, "Couldn't set server cipher list.");
    abort();
  }

  // Register the ALPN selection callback.
  SSL_CTX_set_alpn_select_cb(ctx, alpn_select_cb, args->alpn_preferred);

  // bind/listen/accept at TCP layer.
  const int sock = args->socket;
  gpr_log(GPR_INFO, "Server listening");
  struct sockaddr_in addr;
  socklen_t len = sizeof(addr);
  const int client =
      accept(sock, reinterpret_cast<struct sockaddr*>(&addr), &len);
  if (client < 0) {
    perror("Unable to accept");
    abort();
  }

  // Establish a SSL* and accept at SSL layer.
  SSL* ssl = SSL_new(ctx);
  GPR_ASSERT(ssl);
  SSL_set_fd(ssl, client);
  if (SSL_accept(ssl) <= 0) {
    ERR_print_errors_fp(stderr);
    gpr_log(GPR_ERROR, "Handshake failed.");
  } else {
    gpr_log(GPR_INFO, "Handshake successful.");
  }

  // Wait until the client drops its connection.
  char buf;
  while (SSL_read(ssl, &buf, sizeof(buf)) > 0)
    ;

  SSL_free(ssl);
  close(client);
  close(sock);
  SSL_CTX_free(ctx);
  EVP_cleanup();
}

// This test launches a minimal TLS server on a separate thread and then
// establishes a TLS handshake via the core library to the server. The TLS
// server validates ALPN aspects of the handshake and supplies the protocol
// specified in the server_alpn_preferred argument to the client.
static bool client_ssl_test(char* server_alpn_preferred) {
  bool success = true;

  grpc_init();

  // Find a port we can bind to. Retries added to handle flakes in port server
  // and port picking.
  int port = -1;
  int server_socket = -1;
  int socket_retries = 30;
  while (server_socket == -1 && socket_retries-- > 0) {
    server_socket = create_socket(&port);
    if (server_socket == -1) {
      sleep(1);
    }
  }
  GPR_ASSERT(server_socket > 0 && port > 0);

  // Launch the TLS server thread.
  server_args args = {server_socket, server_alpn_preferred};
  bool ok;
  grpc_core::Thread thd("grpc_client_ssl_test", server_thread, &args, &ok);
  GPR_ASSERT(ok);
  thd.Start();

  // Load key pair and establish client SSL credentials.
  grpc_ssl_pem_key_cert_pair pem_key_cert_pair;
  grpc_slice ca_slice, cert_slice, key_slice;
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                               grpc_load_file(SSL_CA_PATH, 1, &ca_slice)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                               grpc_load_file(SSL_CERT_PATH, 1, &cert_slice)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                               grpc_load_file(SSL_KEY_PATH, 1, &key_slice)));
  const char* ca_cert =
      reinterpret_cast<const char*> GRPC_SLICE_START_PTR(ca_slice);
  pem_key_cert_pair.private_key =
      reinterpret_cast<const char*> GRPC_SLICE_START_PTR(key_slice);
  pem_key_cert_pair.cert_chain =
      reinterpret_cast<const char*> GRPC_SLICE_START_PTR(cert_slice);
  grpc_channel_credentials* ssl_creds =
      grpc_ssl_credentials_create(ca_cert, &pem_key_cert_pair, nullptr);

  // Establish a channel pointing at the TLS server. Since the gRPC runtime is
  // lazy, this won't necessarily establish a connection yet.
  char* target;
  gpr_asprintf(&target, "127.0.0.1:%d", port);
  grpc_arg ssl_name_override = {
      GRPC_ARG_STRING,
      const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
      {const_cast<char*>("foo.test.google.fr")}};
  grpc_channel_args grpc_args;
  grpc_args.num_args = 1;
  grpc_args.args = &ssl_name_override;
  grpc_channel* channel =
      grpc_secure_channel_create(ssl_creds, target, &grpc_args, nullptr);
  GPR_ASSERT(channel);
  gpr_free(target);

  // Initially the channel will be idle, the
  // grpc_channel_check_connectivity_state triggers an attempt to connect.
  GPR_ASSERT(grpc_channel_check_connectivity_state(
                 channel, 1 /* try_to_connect */) == GRPC_CHANNEL_IDLE);

  // Wait a bounded number of times for the channel to be ready. When the
  // channel is ready, the initial TLS handshake will have successfully
  // completed and we know that the client's ALPN list satisfied the server.
  int retries = 10;
  grpc_connectivity_state state = GRPC_CHANNEL_IDLE;
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);

  while (state != GRPC_CHANNEL_READY && retries-- > 0) {
    grpc_channel_watch_connectivity_state(
        channel, state, grpc_timeout_seconds_to_deadline(3), cq, nullptr);
    gpr_timespec cq_deadline = grpc_timeout_seconds_to_deadline(5);
    grpc_event ev = grpc_completion_queue_next(cq, cq_deadline, nullptr);
    GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
    state =
        grpc_channel_check_connectivity_state(channel, 0 /* try_to_connect */);
  }
  grpc_completion_queue_destroy(cq);
  if (retries < 0) {
    success = false;
  }

  grpc_channel_destroy(channel);
  grpc_channel_credentials_release(ssl_creds);
  grpc_slice_unref(cert_slice);
  grpc_slice_unref(key_slice);
  grpc_slice_unref(ca_slice);

  thd.Join();

  grpc_shutdown();

  return success;
}

int main(int argc, char* argv[]) {
  // Handshake succeeeds when the server has grpc-exp as the ALPN preference.
  GPR_ASSERT(client_ssl_test(const_cast<char*>("grpc-exp")));
  // Handshake succeeeds when the server has h2 as the ALPN preference. This
  // covers legacy gRPC servers which don't support grpc-exp.
  GPR_ASSERT(client_ssl_test(const_cast<char*>("h2")));
  // Handshake fails when the server uses a fake protocol as its ALPN
  // preference. This validates the client is correctly validating ALPN returns
  // and sanity checks the client_ssl_test.
  GPR_ASSERT(!client_ssl_test(const_cast<char*>("foo")));
  return 0;
}

#else /* GRPC_POSIX_SOCKET */

int main(int argc, char** argv) { return 1; }

#endif /* GRPC_POSIX_SOCKET */
