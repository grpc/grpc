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

#include "src/core/lib/http/httpcli.h"

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "test/core/util/port.h"
#include "test/core/util/subprocess.h"
#include "test/core/util/test_config.h"
#include "test/core/util/fake_tcp_server.h"

static grpc_millis n_seconds_time(int seconds) {
  return grpc_timespec_to_millis_round_up(
      grpc_timeout_seconds_to_deadline(seconds));
}

struct TestPollingArg {
  TestPollingArg() {
    grpc_core::ExecCtx exec_ctx;
    grpc_pollset* pollset =
        static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
    grpc_pollset_init(pollset, &mu);
    pops = grpc_polling_entity_create_from_pollset(pollset);
  }

  ~TestPollingArg() {
    grpc_core::ExecCtx exec_ctx;
    grpc_pollset_shutdown(
        grpc_polling_entity_pollset(&pops),
        GRPC_CLOSURE_CREATE(DestroyPops, &pops, grpc_schedule_on_exec_ctx));
  }

  static void DestroyPops(void* p, grpc_error_handle /*error*/) {
    grpc_polling_entity* pops = static_cast<grpc_polling_entity*>(p);
    grpc_pollset_destroy(
        grpc_polling_entity_pollset(pops));
    gpr_free(grpc_polling_entity_pollset(pops));
  }

  gpr_mu *mu;
  grpc_polling_entity pops;
};

struct TestArg {
  TestArg(TestPollingArg* polling_arg) : polling_arg(polling_arg) {
    grpc_core::ExecCtx exec_ctx;
    grpc_httpcli_context_init(&context);
  }

  ~TestArg() {
    grpc_core::ExecCtx exec_ctx;
    grpc_httpcli_context_destroy(&context);
    grpc_http_response_destroy(&response);
  }

  TestPollingArg* polling_arg;
  bool done = false;
  grpc_http_response response = {};
  grpc_httpcli_context context;
};

static void on_finish(void* arg, grpc_error_handle error) {
  TestArg* test_arg = static_cast<TestArg*>(arg);
  const char* expect =
      "<html><head><title>Hello world!</title></head>"
      "<body><p>This is a test</p></body></html>";
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_http_response response = test_arg->response;
  gpr_log(GPR_INFO, "response status=%d error=%s", response.status,
          grpc_error_std_string(error).c_str());
  GPR_ASSERT(response.status == 200);
  GPR_ASSERT(response.body_length == strlen(expect));
  GPR_ASSERT(0 == memcmp(expect, response.body, response.body_length));
  grpc_core::MutexLockForGprMu lock(test_arg->polling_arg->mu);
  test_arg->done = true;
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "pollset_kick",
      grpc_pollset_kick(grpc_polling_entity_pollset(&test_arg->polling_arg->pops), nullptr)));
}

static void on_finish_expect_cancelled(void* arg, grpc_error_handle error) {
  TestArg *test_arg = static_cast<TestArg*>(arg);
  grpc_http_response response = test_arg->response;
  gpr_log(GPR_INFO, "response status=%d error=%s", response.status,
          grpc_error_std_string(error).c_str());
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  grpc_core::MutexLockForGprMu lock(test_arg->polling_arg->mu);
  test_arg->done = true;
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "pollset_kick",
      grpc_pollset_kick(grpc_polling_entity_pollset(&test_arg->polling_arg->pops), nullptr)));
}

static void test_get(int port) {
  TestPollingArg polling_arg;
  TestArg test_arg(&polling_arg);
  grpc_httpcli_request req;
  char* host;
  grpc_core::ExecCtx exec_ctx;

  gpr_log(GPR_INFO, "test_get");

  gpr_asprintf(&host, "localhost:%d", port);
  gpr_log(GPR_INFO, "requesting from %s", host);

  memset(&req, 0, sizeof(req));
  req.host = host;
  req.http.path = const_cast<char*>("/get");
  req.handshaker = &grpc_httpcli_plaintext;

  grpc_resource_quota* resource_quota = grpc_resource_quota_create("test_get");
  grpc_httpcli_get(
      &test_arg.context, &test_arg.polling_arg->pops, resource_quota, &req, n_seconds_time(15),
      GRPC_CLOSURE_CREATE(on_finish, &test_arg, grpc_schedule_on_exec_ctx),
      &test_arg.response);
  gpr_mu_lock(test_arg.polling_arg->mu);
  while (!test_arg.done) {
    grpc_pollset_worker* worker = nullptr;
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "pollset_work", grpc_pollset_work(grpc_polling_entity_pollset(&test_arg.polling_arg->pops),
                                          &worker, n_seconds_time(1))));
    gpr_mu_unlock(test_arg.polling_arg->mu);
    gpr_mu_lock(test_arg.polling_arg->mu);
  }
  gpr_mu_unlock(test_arg.polling_arg->mu);
  gpr_free(host);
}

static void test_post(int port) {
  TestPollingArg polling_arg;
  TestArg test_arg(&polling_arg);
  grpc_httpcli_request req;
  char* host;
  grpc_core::ExecCtx exec_ctx;

  gpr_log(GPR_INFO, "test_post");

  gpr_asprintf(&host, "localhost:%d", port);
  gpr_log(GPR_INFO, "posting to %s", host);

  memset(&req, 0, sizeof(req));
  req.host = host;
  req.http.path = const_cast<char*>("/post");
  req.handshaker = &grpc_httpcli_plaintext;

  grpc_resource_quota* resource_quota = grpc_resource_quota_create("test_post");
  grpc_httpcli_post(
      &test_arg.context, &test_arg.polling_arg->pops, resource_quota, &req, "hello", 5, n_seconds_time(15),
      GRPC_CLOSURE_CREATE(on_finish, &test_arg, grpc_schedule_on_exec_ctx),
      &test_arg.response);
  gpr_mu_lock(test_arg.polling_arg->mu);
  while (!test_arg.done) {
    grpc_pollset_worker* worker = nullptr;
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "pollset_work", grpc_pollset_work(grpc_polling_entity_pollset(&test_arg.polling_arg->pops),
                                          &worker, n_seconds_time(1))));
    gpr_mu_unlock(test_arg.polling_arg->mu);
    gpr_mu_lock(test_arg.polling_arg->mu);
  }
  gpr_mu_unlock(test_arg.polling_arg->mu);
  gpr_free(host);
}

int g_fake_non_responsive_dns_server_port;

void InjectNonResponsiveDNSServer(ares_channel channel) {
  gpr_log(GPR_DEBUG,
          "Injecting broken nameserver list. Bad server address:|[::1]:%d|.",
          g_fake_non_responsive_dns_server_port);
  // Configure a non-responsive DNS server at the front of c-ares's nameserver list.
  struct ares_addr_port_node dns_server_addrs[1];
  dns_server_addrs[0].family = AF_INET6;
  (reinterpret_cast<char*>(&dns_server_addrs[0].addr.addr6))[15] = 0x1;
  dns_server_addrs[0].tcp_port = g_fake_non_responsive_dns_server_port;
  dns_server_addrs[0].udp_port = g_fake_non_responsive_dns_server_port;
  dns_server_addrs[0].next = nullptr;
  GPR_ASSERT(ares_set_servers_ports(channel, dns_server_addrs) == ARES_SUCCESS);
}

static void test_cancel_get_during_dns_resolution() {
  // Inject an unresponsive DNS server into the resolver's DNS server config
  FakeTcpServer fake_dns_server(
      FakeTcpServer::AcceptMode::kWaitForClientToSendFirstBytes,
      FakeTcpServer::CloseSocketUponCloseFromPeer);
  g_fake_non_responsive_dns_server_port = fake_dns_server.port();
  void (*prev_test_only_inject_config)(ares_channel channel) = grpc_ares_test_only_inject_config;
  grpc_ares_test_only_inject_config = InjectNonResponsiveDNSServer;
  // Run the same test on several threads in parallel to try to trigger races
  // etc.
  std::vector<std::thread> threads;
  TestPollingArg polling_arg;
  for (int i = 0; i < 100; i++) {
    threads.push_back(std::thread([&polling_arg](){
      TestArg test_arg(&polling_arg);
      grpc_httpcli_request req;
      grpc_core::ExecCtx exec_ctx;
      gpr_log(GPR_INFO, "test_cancel_get_during_dns_resolution");

      memset(&req, 0, sizeof(req));
      req.host = const_cast<char*>("dont-care-since-wont-be-resolver.test.com:443");
      req.http.path = const_cast<char*>("/get");
      req.handshaker = &grpc_httpcli_plaintext;

      grpc_resource_quota* resource_quota = grpc_resource_quota_create("test_cancel_get_during_dns_resolution");
      grpc_httpcli_get(
          &test_arg.context, &test_arg.polling_arg->pops, resource_quota, &req, n_seconds_time(15),
          GRPC_CLOSURE_CREATE(on_finish_expect_cancelled, &test_arg, grpc_schedule_on_exec_ctx),
          &test_arg.response);
      std::thread cancel_thread([&test_arg]() {
        gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));
        grpc_core::ExecCtx exec_ctx;
        gpr_log(GPR_DEBUG, "now cancel http request using grpc_httpcli_cancel");
        grpc_httpcli_cancel(&test_arg.context, GRPC_ERROR_CANCELLED);
      });
      gpr_mu_lock(test_arg.polling_arg->mu);
      while (!test_arg.done) {
        grpc_pollset_worker* worker = nullptr;
        GPR_ASSERT(GRPC_LOG_IF_ERROR(
            "pollset_work", grpc_pollset_work(grpc_polling_entity_pollset(&test_arg.polling_arg->pops),
                                              &worker, n_seconds_time(1))));
        gpr_mu_unlock(test_arg.polling_arg->mu);
        gpr_mu_lock(test_arg.polling_arg->mu);
      }
      gpr_mu_unlock(test_arg.polling_arg->mu);
      cancel_thread.join();
    }));
  }
  for (auto &t : threads) {
    t.join();
  }
  grpc_ares_test_only_inject_config = prev_test_only_inject_config;
}

static void test_cancel_get_while_reading_response() {
  FakeTcpServer fake_http_server(
      FakeTcpServer::AcceptMode::kWaitForClientToSendFirstBytes,
      FakeTcpServer::CloseSocketUponCloseFromPeer);
  std::vector<std::thread> threads;
  TestPollingArg polling_arg;
  for (int i = 0; i < 100; i++) {
    FakeTcpServer *fake_http_server_ptr = &fake_http_server;
    threads.push_back(std::thread([&polling_arg, fake_http_server_ptr]() {
      TestArg test_arg(&polling_arg);
      grpc_httpcli_request req;
      grpc_core::ExecCtx exec_ctx;
      gpr_log(GPR_INFO, "test_cancel_get_while_reading_response");

      memset(&req, 0, sizeof(req));
      req.host = const_cast<char*>(fake_http_server_ptr->address());
      req.http.path = const_cast<char*>("/get");
      req.handshaker = &grpc_httpcli_plaintext;

      grpc_resource_quota* resource_quota = grpc_resource_quota_create("test_cancel_get_while_reading_response");
      grpc_httpcli_get(
          &test_arg.context, &test_arg.polling_arg->pops, resource_quota, &req, n_seconds_time(15),
          GRPC_CLOSURE_CREATE(on_finish_expect_cancelled, &test_arg, grpc_schedule_on_exec_ctx),
          &test_arg.response);
      exec_ctx.Flush();
      std::thread cancel_thread([&test_arg]() {
        gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));
        grpc_core::ExecCtx exec_ctx;
        gpr_log(GPR_DEBUG, "now cancel http request using grpc_httpcli_cancel");
        grpc_httpcli_cancel(&test_arg.context, GRPC_ERROR_CANCELLED);
      });
      gpr_mu_lock(test_arg.polling_arg->mu);
      while (!test_arg.done) {
        grpc_pollset_worker* worker = nullptr;
        GPR_ASSERT(GRPC_LOG_IF_ERROR(
            "pollset_work", grpc_pollset_work(grpc_polling_entity_pollset(&test_arg.polling_arg->pops),
                                              &worker, n_seconds_time(1))));
        gpr_mu_unlock(test_arg.polling_arg->mu);
        gpr_mu_lock(test_arg.polling_arg->mu);
      }
      gpr_mu_unlock(test_arg.polling_arg->mu);
      cancel_thread.join();
    }));
  }
  for (auto& t : threads) {
    t.join();
  }
}

int main(int argc, char** argv) {
  gpr_subprocess* server;
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  {
    grpc_closure destroyed;
    grpc_core::ExecCtx exec_ctx;
    char* me = argv[0];
    char* lslash = strrchr(me, '/');
    char* args[4];
    int port = grpc_pick_unused_port_or_die();
    int arg_shift = 0;
    /* figure out where we are */
    char* root;
    //if (lslash != nullptr) {
    //  /* Hack for bazel target */
    //  if (static_cast<unsigned>(lslash - me) >= (sizeof("http") - 1) &&
    //      strncmp(me + (lslash - me) - sizeof("http") + 1, "http",
    //              sizeof("http") - 1) == 0) {
    //    lslash = me + (lslash - me) - sizeof("http");
    //  }
    //  root = static_cast<char*>(
    //      gpr_malloc(static_cast<size_t>(lslash - me + sizeof("/../.."))));
    //  memcpy(root, me, static_cast<size_t>(lslash - me));
    //  memcpy(root + (lslash - me), "/../..", sizeof("/../.."));
    //} else {
      root = gpr_strdup(".");
    //}

    GPR_ASSERT(argc <= 2);
    if (argc == 2) {
      args[0] = gpr_strdup(argv[1]);
    } else {
      arg_shift = 1;
      gpr_asprintf(&args[0], "%s/test/core/http/python_wrapper.sh", root);
      gpr_asprintf(&args[1], "%s/test/core/http/test_server.py", root);
    }

    /* start the server */
    args[1 + arg_shift] = const_cast<char*>("--port");
    gpr_asprintf(&args[2 + arg_shift], "%d", port);
    server =
        gpr_subprocess_create(3 + arg_shift, const_cast<const char**>(args));
    GPR_ASSERT(server);
    gpr_free(args[0]);
    if (arg_shift) gpr_free(args[1]);
    gpr_free(args[2 + arg_shift]);
    gpr_free(root);

    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                 gpr_time_from_seconds(5, GPR_TIMESPAN)));
    //test_get(port);
    //test_post(port);
    test_cancel_get_during_dns_resolution();
    //test_cancel_get_while_reading_response();
  }
  grpc_shutdown();

  gpr_subprocess_destroy(server);

  return 0;
}
