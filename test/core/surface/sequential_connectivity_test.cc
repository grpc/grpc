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

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

typedef struct test_fixture {
  const char* name;
  void (*add_server_port)(grpc_server* server, const char* addr);
  grpc_channel* (*create_channel)(const char* addr);
} test_fixture;

#define NUM_CONNECTIONS 1000

typedef struct {
  grpc_server* server;
  grpc_completion_queue* cq;
} server_thread_args;

static void server_thread_func(void* args) {
  server_thread_args* a = static_cast<server_thread_args*>(args);
  grpc_event ev = grpc_completion_queue_next(
      a->cq, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
  GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
  GPR_ASSERT(ev.tag == nullptr);
  GPR_ASSERT(ev.success == true);
}

static void run_test(const test_fixture* fixture) {
  gpr_log(GPR_INFO, "TEST: %s", fixture->name);

  grpc_init();

  char* addr;
  gpr_join_host_port(&addr, "localhost", grpc_pick_unused_port_or_die());

  grpc_server* server = grpc_server_create(nullptr, nullptr);
  fixture->add_server_port(server, addr);
  grpc_completion_queue* server_cq =
      grpc_completion_queue_create_for_next(nullptr);
  grpc_server_register_completion_queue(server, server_cq, nullptr);
  grpc_server_start(server);

  server_thread_args sta = {server, server_cq};
  grpc_core::Thread server_thread("grpc_server", server_thread_func, &sta);
  server_thread.Start();

  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  grpc_channel* channels[NUM_CONNECTIONS];
  for (size_t i = 0; i < NUM_CONNECTIONS; i++) {
    channels[i] = fixture->create_channel(addr);

    gpr_timespec connect_deadline = grpc_timeout_seconds_to_deadline(30);
    grpc_connectivity_state state;
    while ((state = grpc_channel_check_connectivity_state(channels[i], 1)) !=
           GRPC_CHANNEL_READY) {
      grpc_channel_watch_connectivity_state(channels[i], state,
                                            connect_deadline, cq, nullptr);
      grpc_event ev = grpc_completion_queue_next(
          cq, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
      /* check that the watcher from "watch state" was free'd */
      GPR_ASSERT(grpc_channel_num_external_connectivity_watchers(channels[i]) ==
                 0);
      GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
      GPR_ASSERT(ev.tag == nullptr);
      GPR_ASSERT(ev.success == true);
    }
  }

  grpc_server_shutdown_and_notify(server, server_cq, nullptr);
  server_thread.Join();

  grpc_completion_queue_shutdown(server_cq);
  grpc_completion_queue_shutdown(cq);

  while (grpc_completion_queue_next(server_cq,
                                    gpr_inf_future(GPR_CLOCK_REALTIME), nullptr)
             .type != GRPC_QUEUE_SHUTDOWN)
    ;
  while (grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                    nullptr)
             .type != GRPC_QUEUE_SHUTDOWN)
    ;

  for (size_t i = 0; i < NUM_CONNECTIONS; i++) {
    grpc_channel_destroy(channels[i]);
  }

  grpc_server_destroy(server);
  grpc_completion_queue_destroy(server_cq);
  grpc_completion_queue_destroy(cq);

  grpc_shutdown();
  gpr_free(addr);
}

static void insecure_test_add_port(grpc_server* server, const char* addr) {
  grpc_server_add_insecure_http2_port(server, addr);
}

static grpc_channel* insecure_test_create_channel(const char* addr) {
  return grpc_insecure_channel_create(addr, nullptr, nullptr);
}

static const test_fixture insecure_test = {
    "insecure",
    insecure_test_add_port,
    insecure_test_create_channel,
};

static void secure_test_add_port(grpc_server* server, const char* addr) {
  grpc_ssl_pem_key_cert_pair pem_cert_key_pair = {test_server1_key,
                                                  test_server1_cert};
  grpc_server_credentials* ssl_creds = grpc_ssl_server_credentials_create(
      nullptr, &pem_cert_key_pair, 1, 0, nullptr);
  grpc_server_add_secure_http2_port(server, addr, ssl_creds);
  grpc_server_credentials_release(ssl_creds);
}

static grpc_channel* secure_test_create_channel(const char* addr) {
  grpc_channel_credentials* ssl_creds =
      grpc_ssl_credentials_create(test_root_cert, nullptr, nullptr);
  grpc_arg ssl_name_override = {
      GRPC_ARG_STRING,
      const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
      {const_cast<char*>("foo.test.google.fr")}};
  grpc_channel_args* new_client_args =
      grpc_channel_args_copy_and_add(nullptr, &ssl_name_override, 1);
  grpc_channel* channel =
      grpc_secure_channel_create(ssl_creds, addr, new_client_args, nullptr);
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_channel_args_destroy(new_client_args);
  }
  grpc_channel_credentials_release(ssl_creds);
  return channel;
}

static const test_fixture secure_test = {
    "secure",
    secure_test_add_port,
    secure_test_create_channel,
};

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);

  run_test(&insecure_test);
  run_test(&secure_test);
}
