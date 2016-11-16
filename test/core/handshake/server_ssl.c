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

#include <arpa/inet.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include "src/core/lib/iomgr/load_file.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#define SSL_CERT_PATH "src/core/lib/tsi/test_creds/server1.pem"
#define SSL_KEY_PATH "src/core/lib/tsi/test_creds/server1.key"
#define SSL_CA_PATH "src/core/lib/tsi/test_creds/ca.pem"

// Handshake completed signal to server thread.
static gpr_event client_handshake_complete;

static int create_socket(int port) {
  int s;
  struct sockaddr_in addr;

  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) {
    perror("Unable to create socket");
    return -1;
  }

  if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("Unable to connect");
    return -1;
  }

  return s;
}

// Simple gRPC server. This listens until client_handshake_complete occurs.
static void server_thread(void *arg) {
  const int port = *(int *)arg;

  // Load key pair and establish server SSL credentials.
  grpc_ssl_pem_key_cert_pair pem_key_cert_pair;
  grpc_slice ca_slice, cert_slice, key_slice;
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                               grpc_load_file(SSL_CA_PATH, 1, &ca_slice)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                               grpc_load_file(SSL_CERT_PATH, 1, &cert_slice)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                               grpc_load_file(SSL_KEY_PATH, 1, &key_slice)));
  const char *ca_cert = (const char *)GRPC_SLICE_START_PTR(ca_slice);
  pem_key_cert_pair.private_key = (const char *)GRPC_SLICE_START_PTR(key_slice);
  pem_key_cert_pair.cert_chain = (const char *)GRPC_SLICE_START_PTR(cert_slice);
  grpc_server_credentials *ssl_creds = grpc_ssl_server_credentials_create(
      ca_cert, &pem_key_cert_pair, 1, 0, NULL);

  // Start server listening on local port.
  char *addr;
  gpr_asprintf(&addr, "127.0.0.1:%d", port);
  grpc_server *server = grpc_server_create(NULL, NULL);
  GPR_ASSERT(grpc_server_add_secure_http2_port(server, addr, ssl_creds));
  free(addr);

  grpc_completion_queue *cq = grpc_completion_queue_create(NULL);

  grpc_server_register_completion_queue(server, cq, NULL);
  grpc_server_start(server);

  // Wait a bounded number of time until client_handshake_complete is set,
  // sleeping between polls.
  int retries = 10;
  while (!gpr_event_get(&client_handshake_complete) && retries-- > 0) {
    const gpr_timespec cq_deadline = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(1);
    grpc_event ev = grpc_completion_queue_next(cq, cq_deadline, NULL);
    GPR_ASSERT(ev.type == GRPC_QUEUE_TIMEOUT);
  }

  gpr_log(GPR_INFO, "Shutting down server");
  grpc_server_shutdown_and_notify(server, cq, NULL);
  grpc_completion_queue_shutdown(cq);

  const gpr_timespec cq_deadline = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(5);
  grpc_event ev = grpc_completion_queue_next(cq, cq_deadline, NULL);
  GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);

  grpc_server_destroy(server);
  grpc_completion_queue_destroy(cq);
  grpc_server_credentials_release(ssl_creds);
  grpc_slice_unref(cert_slice);
  grpc_slice_unref(key_slice);
  grpc_slice_unref(ca_slice);
}

// This test launches a gRPC server on a separate thread and then establishes a
// TLS handshake via a minimal TLS client. The TLS client has configurable (via
// alpn_list) ALPN settings and can probe at the supported ALPN preferences
// using this (via alpn_expected).
static bool server_ssl_test(const char *alpn_list[], unsigned int alpn_list_len,
                            const char *alpn_expected) {
  bool success = true;

  grpc_init();
  int port = grpc_pick_unused_port_or_die();
  gpr_event_init(&client_handshake_complete);

  // Launch the gRPC server thread.
  gpr_thd_options thdopt = gpr_thd_options_default();
  gpr_thd_id thdid;
  gpr_thd_options_set_joinable(&thdopt);
  GPR_ASSERT(gpr_thd_new(&thdid, server_thread, &port, &thdopt));

  SSL_load_error_strings();
  OpenSSL_add_ssl_algorithms();

  const SSL_METHOD *method = TLSv1_2_client_method();
  SSL_CTX *ctx = SSL_CTX_new(method);
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
  // src/core/lib/tsi/ssl_transport_security.c.
  const char *cipher_list =
      "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-"
      "SHA384:ECDHE-RSA-AES256-GCM-SHA384";
  if (!SSL_CTX_set_cipher_list(ctx, cipher_list)) {
    ERR_print_errors_fp(stderr);
    gpr_log(GPR_ERROR, "Couldn't set server cipher list.");
    abort();
  }

  // Configure ALPN list the client will send to the server. This must match the
  // wire format, see documentation for SSL_CTX_set_alpn_protos.
  unsigned int alpn_protos_len = alpn_list_len;
  for (unsigned int i = 0; i < alpn_list_len; ++i) {
    alpn_protos_len += (unsigned int)strlen(alpn_list[i]);
  }
  unsigned char *alpn_protos = gpr_malloc(alpn_protos_len);
  unsigned char *p = alpn_protos;
  for (unsigned int i = 0; i < alpn_list_len; ++i) {
    const uint8_t len = (uint8_t)strlen(alpn_list[i]);
    *p++ = len;
    memcpy(p, alpn_list[i], len);
    p += len;
  }
  GPR_ASSERT(SSL_CTX_set_alpn_protos(ctx, alpn_protos, alpn_protos_len) == 0);

  // Try and connect to server. We allow a bounded number of retries as we might
  // be racing with the server setup on its separate thread.
  int retries = 10;
  int sock = -1;
  while (sock == -1 && retries-- > 0) {
    sock = create_socket(port);
    if (sock < 0) {
      sleep(1);
    }
  }
  GPR_ASSERT(sock > 0);
  gpr_log(GPR_INFO, "Connected to server on port %d", port);

  // Establish a SSL* and connect at SSL layer.
  SSL *ssl = SSL_new(ctx);
  GPR_ASSERT(ssl);
  SSL_set_fd(ssl, sock);
  if (SSL_connect(ssl) <= 0) {
    ERR_print_errors_fp(stderr);
    gpr_log(GPR_ERROR, "Handshake failed.");
    success = false;
  } else {
    gpr_log(GPR_INFO, "Handshake successful.");
    // Validate ALPN preferred by server matches alpn_expected.
    const unsigned char *alpn_selected;
    unsigned int alpn_selected_len;
    SSL_get0_alpn_selected(ssl, &alpn_selected, &alpn_selected_len);
    if (strlen(alpn_expected) != alpn_selected_len ||
        strncmp((const char *)alpn_selected, alpn_expected,
                alpn_selected_len) != 0) {
      gpr_log(GPR_ERROR, "Unexpected ALPN protocol preference");
      success = false;
    }
  }
  gpr_event_set(&client_handshake_complete, &client_handshake_complete);

  SSL_free(ssl);
  gpr_free(alpn_protos);
  SSL_CTX_free(ctx);
  EVP_cleanup();
  close(sock);

  gpr_thd_join(thdid);

  grpc_shutdown();

  return success;
}

int main(int argc, char *argv[]) {
  // Handshake succeeeds when the client supplies the standard ALPN list.
  const char *full_alpn_list[] = {"grpc-exp", "h2"};
  GPR_ASSERT(server_ssl_test(full_alpn_list, 2, "grpc-exp"));
  // Handshake succeeeds when the client supplies only h2 as the ALPN list. This
  // covers legacy gRPC clients which don't support grpc-exp.
  const char *h2_only_alpn_list[] = {"h2"};
  GPR_ASSERT(server_ssl_test(h2_only_alpn_list, 1, "h2"));
  // Handshake succeeds when the client supplies superfluous ALPN entries and
  // also when h2 precedes gprc-exp.
  const char *extra_alpn_list[] = {"foo", "h2", "bar", "grpc-exp"};
  GPR_ASSERT(server_ssl_test(extra_alpn_list, 4, "h2"));
  // Handshake fails when the client uses a fake protocol as its only ALPN
  // preference. This validates the server is correctly validating ALPN
  // and sanity checks the server_ssl_test.
  const char *fake_alpn_list[] = {"foo"};
  GPR_ASSERT(!server_ssl_test(fake_alpn_list, 1, "foo"));
  return 0;
}
