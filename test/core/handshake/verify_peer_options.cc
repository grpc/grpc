//
//
// Copyright 2018 gRPC authors.
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

#include "src/core/lib/iomgr/port.h"

// This test won't work except with posix sockets enabled
#ifdef GRPC_POSIX_SOCKET_TCP

#include <arpa/inet.h>
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
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/load_file.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#define SSL_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SSL_KEY_PATH "src/core/tsi/test_creds/server1.key"
#define SSL_CA_PATH "src/core/tsi/test_creds/ca.pem"

// Simple gRPC server. This listens until client_handshake_complete occurs.
static gpr_event client_handshake_complete;

static void server_thread(void* arg) {
  const int port = *static_cast<int*>(arg);

  // Load key pair and establish server SSL credentials.
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
  grpc_server_credentials* ssl_creds = grpc_ssl_server_credentials_create(
      ca_cert, &pem_key_cert_pair, 1, 0, nullptr);

  // Start server listening on local port.
  std::string addr = absl::StrCat("127.0.0.1:", port);
  grpc_server* server = grpc_server_create(nullptr, nullptr);
  GPR_ASSERT(grpc_server_add_http2_port(server, addr.c_str(), ssl_creds));

  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);

  grpc_server_register_completion_queue(server, cq, nullptr);
  grpc_server_start(server);

  // Wait a bounded number of time until client_handshake_complete is set,
  // sleeping between polls. The total time spent (deadline * retries)
  // should be strictly greater than the client retry limit so that the
  // client will always timeout first.
  int retries = 60;
  while (!gpr_event_get(&client_handshake_complete) && retries-- > 0) {
    const gpr_timespec cq_deadline = grpc_timeout_seconds_to_deadline(1);
    grpc_event ev = grpc_completion_queue_next(cq, cq_deadline, nullptr);
    GPR_ASSERT(ev.type == GRPC_QUEUE_TIMEOUT);
  }

  gpr_log(GPR_INFO, "Shutting down server");
  grpc_server_shutdown_and_notify(server, cq, nullptr);
  grpc_server_cancel_all_calls(server);
  grpc_completion_queue_shutdown(cq);

  const gpr_timespec cq_deadline = grpc_timeout_seconds_to_deadline(60);
  grpc_event ev = grpc_completion_queue_next(cq, cq_deadline, nullptr);
  GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);

  grpc_server_destroy(server);
  grpc_completion_queue_destroy(cq);
  grpc_server_credentials_release(ssl_creds);
  grpc_slice_unref(cert_slice);
  grpc_slice_unref(key_slice);
  grpc_slice_unref(ca_slice);
}

// This test launches a minimal TLS grpc server on a separate thread and then
// establishes a TLS handshake via the core library to the server. The client
// uses the supplied verify options.
static bool verify_peer_options_test(verify_peer_options* verify_options) {
  bool success = true;

  grpc_init();
  int port = grpc_pick_unused_port_or_die();
  gpr_event_init(&client_handshake_complete);

  // Load key pair and establish client SSL credentials.
  // NOTE: we intentionally load the credential files before starting
  // the server thread because grpc_load_file can experience trouble
  // when two threads attempt to load the same file concurrently
  // and server thread also reads the same files as soon as it starts.
  // See https://github.com/grpc/grpc/issues/23503 for details.
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
  grpc_channel_credentials* ssl_creds = grpc_ssl_credentials_create(
      ca_cert, &pem_key_cert_pair, verify_options, nullptr);

  // Launch the gRPC server thread.
  bool ok;
  grpc_core::Thread thd("grpc_client_ssl_test", server_thread, &port, &ok);
  GPR_ASSERT(ok);
  thd.Start();

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
  GPR_ASSERT(channel);

  // Initially the channel will be idle, the
  // grpc_channel_check_connectivity_state triggers an attempt to connect.
  GPR_ASSERT(grpc_channel_check_connectivity_state(
                 channel, 1 /* try_to_connect */) == GRPC_CHANNEL_IDLE);

  // Wait a bounded number of times for the channel to be ready. When the
  // channel is ready, the initial TLS handshake will have successfully
  // completed. The total time spent on the client side (retries * deadline)
  // should be greater than the server side time limit.
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

  // Now that the client is completely cleaned up, trigger the server to
  // shutdown
  gpr_event_set(&client_handshake_complete, &client_handshake_complete);
  // Wait for the server to completely shutdown
  thd.Join();

  grpc_shutdown();

  return success;
}

static int callback_return_value = 0;
static char callback_target_host[4096];
static char callback_target_pem[4096];
static void* callback_userdata = nullptr;
static void* destruct_userdata = nullptr;

static int verify_callback(const char* target_host, const char* target_pem,
                           void* userdata) {
  if (target_host != nullptr) {
    snprintf(callback_target_host, sizeof(callback_target_host), "%s",
             target_host);
  } else {
    callback_target_host[0] = '\0';
  }
  if (target_pem != nullptr) {
    snprintf(callback_target_pem, sizeof(callback_target_pem), "%s",
             target_pem);
  } else {
    callback_target_pem[0] = '\0';
  }
  callback_userdata = userdata;
  return callback_return_value;
}

static void verify_destruct(void* userdata) { destruct_userdata = userdata; }

int main(int argc, char* argv[]) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();

  int userdata = 42;
  verify_peer_options verify_options;

  // Load the server's cert so that we can assert it gets passed to the callback
  grpc_slice cert_slice;
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                               grpc_load_file(SSL_CERT_PATH, 1, &cert_slice)));
  const char* server_cert =
      reinterpret_cast<const char*> GRPC_SLICE_START_PTR(cert_slice);

  // Running with all-null values should have no effect
  verify_options.verify_peer_callback = nullptr;
  verify_options.verify_peer_callback_userdata = nullptr;
  verify_options.verify_peer_destruct = nullptr;
  GPR_ASSERT(verify_peer_options_test(&verify_options));
  GPR_ASSERT(strlen(callback_target_host) == 0);
  GPR_ASSERT(strlen(callback_target_pem) == 0);
  GPR_ASSERT(callback_userdata == nullptr);
  GPR_ASSERT(destruct_userdata == nullptr);

  // Running with the callbacks and verify we get the expected values
  verify_options.verify_peer_callback = verify_callback;
  verify_options.verify_peer_callback_userdata = static_cast<void*>(&userdata);
  verify_options.verify_peer_destruct = verify_destruct;
  GPR_ASSERT(verify_peer_options_test(&verify_options));
  GPR_ASSERT(strcmp(callback_target_host, "foo.test.google.fr") == 0);
  GPR_ASSERT(strcmp(callback_target_pem, server_cert) == 0);
  GPR_ASSERT(callback_userdata == static_cast<void*>(&userdata));
  GPR_ASSERT(destruct_userdata == static_cast<void*>(&userdata));

  // If the callback returns non-zero, initializing the channel should fail.
  callback_return_value = 1;
  GPR_ASSERT(!verify_peer_options_test(&verify_options));

  grpc_slice_unref(cert_slice);

  grpc_shutdown();
  return 0;
}

#else  // GRPC_POSIX_SOCKET_TCP

int main(int argc, char** argv) { return 1; }

#endif  // GRPC_POSIX_SOCKET_TCP
