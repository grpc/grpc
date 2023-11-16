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

#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>

#include <openssl/crypto.h>
#include <openssl/evp.h>

#include "absl/base/thread_annotations.h"
#include "gtest/gtest.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>
#include <grpc/support/time.h>

#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/port.h"
#include "test/core/util/test_config.h"

// IWYU pragma: no_include <arpa/inet.h>

// This test won't work except with posix sockets enabled
#ifdef GRPC_POSIX_SOCKET_TCP

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include "absl/strings/str_cat.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/load_file.h"

#define SSL_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SSL_KEY_PATH "src/core/tsi/test_creds/server1.key"
#define SSL_CA_PATH "src/core/tsi/test_creds/ca.pem"

grpc_core::TraceFlag client_ssl_tsi_tracing_enabled(false, "tsi");

class SslLibraryInfo {
 public:
  SslLibraryInfo() {}

  void Notify() {
    grpc_core::MutexLock lock(&mu_);
    ready_ = true;
    cv_.Signal();
  }

  void Await() {
    grpc_core::MutexLock lock(&mu_);
    while (!ready_) {
      cv_.Wait(&mu_);
    }
  }

 private:
  grpc_core::Mutex mu_;
  grpc_core::CondVar cv_;
  bool ready_ ABSL_GUARDED_BY(mu_) = false;
};

// Arguments for TLS server thread.
typedef struct {
  int socket;
  char* alpn_preferred;
  SslLibraryInfo* ssl_library_info;
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
static int alpn_select_cb(SSL* /*ssl*/, const uint8_t** out, uint8_t* out_len,
                          const uint8_t* in, unsigned in_len, void* arg) {
  const uint8_t* alpn_preferred = static_cast<const uint8_t*>(arg);

  *out = alpn_preferred;
  *out_len = static_cast<uint8_t>(
      strlen(reinterpret_cast<const char*>(alpn_preferred)));

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
      EXPECT_FALSE(h2_seen);
    }
    if (length == strlen("h2") && strncmp(inp, "h2", length) == 0) {
      h2_seen = true;
      EXPECT_TRUE(grpc_exp_seen);
    }
    inp += length;
  }

  EXPECT_EQ(inp, in_end);
  EXPECT_TRUE(grpc_exp_seen);
  EXPECT_TRUE(h2_seen);

  return SSL_TLSEXT_ERR_OK;
}

static void ssl_log_where_info(const SSL* ssl, int where, int flag,
                               const char* msg) {
  if ((where & flag) &&
      GRPC_TRACE_FLAG_ENABLED(client_ssl_tsi_tracing_enabled)) {
    gpr_log(GPR_INFO, "%20.20s - %30.30s  - %5.10s", msg,
            SSL_state_string_long(ssl), SSL_state_string(ssl));
  }
}

static void ssl_server_info_callback(const SSL* ssl, int where, int ret) {
  if (ret == 0) {
    gpr_log(GPR_ERROR, "ssl_server_info_callback: error occurred.\n");
    return;
  }

  ssl_log_where_info(ssl, where, SSL_CB_LOOP, "Server: LOOP");
  ssl_log_where_info(ssl, where, SSL_CB_HANDSHAKE_START,
                     "Server: HANDSHAKE START");
  ssl_log_where_info(ssl, where, SSL_CB_HANDSHAKE_DONE,
                     "Server: HANDSHAKE DONE");
}

// Minimal TLS server. This is largely based on the example at
// https://wiki.openssl.org/index.php/Simple_TLS_Server and the gRPC core
// internals in src/core/tsi/ssl_transport_security.c.
static void server_thread(void* arg) {
  const server_args* args = static_cast<server_args*>(arg);

  SSL_load_error_strings();
  OpenSSL_add_ssl_algorithms();
  args->ssl_library_info->Notify();

  const SSL_METHOD* method = TLSv1_2_server_method();
  SSL_CTX* ctx = SSL_CTX_new(method);
  if (!ctx) {
    perror("Unable to create SSL context");
    ERR_print_errors_fp(stderr);
    abort();
  }

  // Load key pair.
  if (SSL_CTX_use_certificate_file(ctx, SSL_CERT_PATH, SSL_FILETYPE_PEM) < 0) {
    perror("Unable to use certificate file.");
    ERR_print_errors_fp(stderr);
    abort();
  }
  if (SSL_CTX_use_PrivateKey_file(ctx, SSL_KEY_PATH, SSL_FILETYPE_PEM) < 0) {
    perror("Unable to use private key file.");
    ERR_print_errors_fp(stderr);
    abort();
  }
  if (SSL_CTX_check_private_key(ctx) != 1) {
    perror("Check private key failed.");
    ERR_print_errors_fp(stderr);
    abort();
  }

  // Set the cipher list to match the one expressed in
  // src/core/tsi/ssl_transport_security.cc.
  const char* cipher_list =
      "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-"
      "SHA384:ECDHE-RSA-AES256-GCM-SHA384";
  if (!SSL_CTX_set_cipher_list(ctx, cipher_list)) {
    ERR_print_errors_fp(stderr);
    grpc_core::Crash("Couldn't set server cipher list.");
  }

  // Enable automatic curve selection. This is a NO-OP when using OpenSSL
  // versions > 1.0.2.
  if (!SSL_CTX_set_ecdh_auto(ctx, /*onoff=*/1)) {
    ERR_print_errors_fp(stderr);
    grpc_core::Crash("Couldn't set automatic curve selection.");
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
  SSL_set_info_callback(ssl, ssl_server_info_callback);
  ASSERT_TRUE(ssl);
  SSL_set_fd(ssl, client);
  if (SSL_accept(ssl) <= 0) {
    ERR_print_errors_fp(stderr);
    gpr_log(GPR_ERROR, "Handshake failed.");
  } else {
    gpr_log(GPR_INFO, "Handshake successful.");
  }

  // Send out the settings frame.
  const char settings_frame[] = "\x00\x00\x00\x04\x00\x00\x00\x00\x00";
  SSL_write(ssl, settings_frame, sizeof(settings_frame) - 1);

  // Wait until the client drops its connection.
  char buf;
  while (SSL_read(ssl, &buf, sizeof(buf)) > 0) {
  }

  SSL_free(ssl);
  close(client);
  close(sock);
  SSL_CTX_free(ctx);
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
  EXPECT_GT(server_socket, 0);
  EXPECT_GT(port, 0);

  // Launch the TLS server thread.
  SslLibraryInfo ssl_library_info;
  server_args args = {server_socket, server_alpn_preferred, &ssl_library_info};
  bool ok;
  grpc_core::Thread thd("grpc_client_ssl_test", server_thread, &args, &ok);
  EXPECT_TRUE(ok);
  thd.Start();
  ssl_library_info.Await();

  // Load key pair and establish client SSL credentials.
  grpc_ssl_pem_key_cert_pair pem_key_cert_pair;
  grpc_slice ca_slice, cert_slice, key_slice;
  EXPECT_TRUE(GRPC_LOG_IF_ERROR("load_file",
                                grpc_load_file(SSL_CA_PATH, 1, &ca_slice)));
  EXPECT_TRUE(GRPC_LOG_IF_ERROR("load_file",
                                grpc_load_file(SSL_CERT_PATH, 1, &cert_slice)));
  EXPECT_TRUE(GRPC_LOG_IF_ERROR("load_file",
                                grpc_load_file(SSL_KEY_PATH, 1, &key_slice)));
  const char* ca_cert =
      reinterpret_cast<const char*> GRPC_SLICE_START_PTR(ca_slice);
  pem_key_cert_pair.private_key =
      reinterpret_cast<const char*> GRPC_SLICE_START_PTR(key_slice);
  pem_key_cert_pair.cert_chain =
      reinterpret_cast<const char*> GRPC_SLICE_START_PTR(cert_slice);
  grpc_channel_credentials* ssl_creds = grpc_ssl_credentials_create(
      ca_cert, &pem_key_cert_pair, nullptr, nullptr);

  // Establish a channel pointing at the TLS server. Since the gRPC runtime is
  // lazy, this won't necessarily establish a connection yet.
  std::string target = absl::StrCat("127.0.0.1:", port);
  grpc_arg ssl_name_override = {
      GRPC_ARG_STRING,
      const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
      {const_cast<char*>("foo.test.google.fr")}};
  grpc_channel_args grpc_args;
  grpc_args.num_args = 1;
  grpc_args.args = &ssl_name_override;
  grpc_channel* channel =
      grpc_channel_create(target.c_str(), ssl_creds, &grpc_args);
  EXPECT_TRUE(channel);

  // Initially the channel will be idle, the
  // grpc_channel_check_connectivity_state triggers an attempt to connect.
  EXPECT_EQ(
      grpc_channel_check_connectivity_state(channel, 1 /* try_to_connect */),
      GRPC_CHANNEL_IDLE);

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
    EXPECT_EQ(ev.type, GRPC_OP_COMPLETE);
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

TEST(ClientSslTest, MainTest) {
  // Handshake succeeeds when the server has grpc-exp as the ALPN preference.
  ASSERT_TRUE(client_ssl_test(const_cast<char*>("grpc-exp")));
  // Handshake succeeeds when the server has h2 as the ALPN preference. This
  // covers legacy gRPC servers which don't support grpc-exp.
  ASSERT_TRUE(client_ssl_test(const_cast<char*>("h2")));

// TODO(gtcooke94) Figure out why test is failing with OpenSSL and fix it.
#ifdef OPENSSL_IS_BORING_SSL
  // Handshake fails when the server uses a fake protocol as its ALPN
  // preference. This validates the client is correctly validating ALPN returns
  // and sanity checks the client_ssl_test.
  ASSERT_FALSE(client_ssl_test(const_cast<char*>("foo")));
#endif  // OPENSSL_IS_BORING_SSL
  // Clean up the SSL libraries.
  EVP_cleanup();
}

#endif  // GRPC_POSIX_SOCKET_TCP

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
