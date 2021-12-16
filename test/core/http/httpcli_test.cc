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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "test/core/util/fake_udp_and_tcp_server.h"
#include "test/core/util/port.h"
#include "test/core/util/subprocess.h"
#include "test/core/util/test_config.h"

namespace {

grpc_millis NSecondsTime(int seconds) {
  return grpc_timespec_to_millis_round_up(
      grpc_timeout_seconds_to_deadline(seconds));
}

int g_argc;
char** g_argv;
int g_server_port;
gpr_subprocess* g_server;

std::vector<std::string> g_subprocess_args;

class HttpCliTest : public ::testing::Test {
 public:
  HttpCliTest() {
    grpc_init();
    grpc_core::ExecCtx exec_ctx;
    grpc_pollset* pollset =
        static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
    grpc_pollset_init(pollset, &mu_);
    pops_ = grpc_polling_entity_create_from_pollset(pollset);
  }
  ~HttpCliTest() override {
    {
      grpc_core::ExecCtx exec_ctx;
      grpc_pollset_shutdown(
          grpc_polling_entity_pollset(&pops_),
          GRPC_CLOSURE_CREATE(DestroyPops, &pops_, grpc_schedule_on_exec_ctx));
    }
    grpc_shutdown();
  }

  void RunAndKick(const std::function<void()>& f) {
    grpc_core::MutexLockForGprMu lock(mu_);
    f();
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "pollset_kick",
        grpc_pollset_kick(
            grpc_polling_entity_pollset(&pops_), nullptr)));
  }

  void PollUntil(const std::function<bool()>& predicate) {
    gpr_mu_lock(mu_);
    while (!predicate()) {
      grpc_pollset_worker* worker = nullptr;
      GPR_ASSERT(GRPC_LOG_IF_ERROR(
          "pollset_work", grpc_pollset_work(grpc_polling_entity_pollset(
                                                &pops_),
                                            &worker, NSecondsTime(1))));
      gpr_mu_unlock(mu_);
      gpr_mu_lock(mu_);
    }
    gpr_mu_unlock(mu_);
  }

  grpc_polling_entity* pops() { return &pops_; }

 protected:
  static void SetUpTestSuite() {
    gpr_log(GPR_INFO, "begin SetUpTestSuite");
    char* me = g_argv[0];
    char* lslash = strrchr(me, '/');
    char* args[4];
    gpr_log(GPR_INFO, "begin SetUpTestSuite 1");
    g_server_port = grpc_pick_unused_port_or_die();
    int arg_shift = 0;
    /* figure out where we are */
    gpr_log(GPR_INFO, "begin SetUpTestSuite 2");
    char* root;
    if (lslash != nullptr) {
      /* Hack for bazel target */
      if (static_cast<unsigned>(lslash - me) >= (sizeof("http") - 1) &&
          strncmp(me + (lslash - me) - sizeof("http") + 1, "http",
                  sizeof("http") - 1) == 0) {
        lslash = me + (lslash - me) - sizeof("http");
      }
      root = static_cast<char*>(
          gpr_malloc(static_cast<size_t>(lslash - me + sizeof("/../.."))));
      memcpy(root, me, static_cast<size_t>(lslash - me));
      memcpy(root + (lslash - me), "/../..", sizeof("/../.."));
    } else {
      root = gpr_strdup(".");
    }
    gpr_log(GPR_INFO, "begin SetUpTestSuite 3");

    GPR_ASSERT(g_argc <= 2);
    if (g_argc == 2) {
      args[0] = gpr_strdup(g_argv[1]);
    } else {
      arg_shift = 1;
      gpr_asprintf(&args[0], "%s/test/core/http/python_wrapper.sh", root);
      gpr_asprintf(&args[1], "%s/test/core/http/test_server.py", root);
    }
    gpr_log(GPR_INFO, "begin SetUpTestSuite 4");

    /* start the server */
    args[1 + arg_shift] = const_cast<char*>("--port");
    gpr_asprintf(&args[2 + arg_shift], "%d", g_server_port);
    int num_args = 3 + arg_shift;
    gpr_log(GPR_INFO, "starting test server subprocess:");
    for (int i = 0; i < num_args; i++) {
      gpr_log(GPR_INFO, "  test server subprocess argv[%d]: ", i, args[i]);
    }
    g_server =
        gpr_subprocess_create(3 + arg_shift, const_cast<const char**>(args));
    GPR_ASSERT(g_server);
    gpr_free(args[0]);
    if (arg_shift) gpr_free(args[1]);
    gpr_free(args[2 + arg_shift]);
    gpr_free(root);
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                 gpr_time_from_seconds(5, GPR_TIMESPAN)));
    gpr_log(GPR_INFO, "begin SetUpTestSuite 5");
  }

  static void TearDownTestSuite() {
    gpr_subprocess_destroy(g_server);
  }

 private:
  static void DestroyPops(void* p, grpc_error_handle /*error*/) {
    grpc_polling_entity* pops = static_cast<grpc_polling_entity*>(p);
    grpc_pollset_destroy(grpc_polling_entity_pollset(pops));
    gpr_free(grpc_polling_entity_pollset(pops));
  }

  gpr_mu* mu_;
  grpc_polling_entity pops_;
};

struct RequestArgs {
  explicit RequestArgs(HttpCliTest* test) : test(test) {}

  ~RequestArgs() {
    grpc_core::ExecCtx exec_ctx;
    grpc_http_response_destroy(&response);
  }

  HttpCliTest* test;
  bool done = false;
  grpc_http_response response = {};
};

void OnFinish(void* arg, grpc_error_handle error) {
  RequestArgs* request_args = static_cast<RequestArgs*>(arg);
  const char* expect =
      "<html><head><title>Hello world!</title></head>"
      "<body><p>This is a test</p></body></html>";
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_http_response response = request_args->response;
  gpr_log(GPR_INFO, "response status=%d error=%s", response.status,
          grpc_error_std_string(error).c_str());
  GPR_ASSERT(response.status == 200);
  GPR_ASSERT(response.body_length == strlen(expect));
  GPR_ASSERT(0 == memcmp(expect, response.body, response.body_length));
  request_args->test->RunAndKick([request_args]() {
    request_args->done = true;
  });
}

void OnFinishExpectCancelled(void* arg, grpc_error_handle error) {
  RequestArgs* request_args = static_cast<RequestArgs*>(arg);
  grpc_http_response response = request_args->response;
  gpr_log(GPR_INFO, "response status=%d error=%s", response.status,
          grpc_error_std_string(error).c_str());
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  request_args->test->RunAndKick([request_args]() {
    request_args->done = true;
  });
}

TEST_F(HttpCliTest, Get) {
  RequestArgs request_args(this);
  grpc_httpcli_request req;
  char* host;
  grpc_core::ExecCtx exec_ctx;

  gpr_log(GPR_INFO, "test_get");

  gpr_asprintf(&host, "localhost:%d", g_server_port);
  gpr_log(GPR_INFO, "requesting from %s", host);

  memset(&req, 0, sizeof(req));
  req.host = host;
  req.http.path = const_cast<char*>("/get");
  req.handshaker = &grpc_httpcli_plaintext;

  grpc_core::OrphanablePtr<grpc_core::HttpCliRequest> httpcli_request =
      grpc_core::HttpCliRequest::Get(
          pops(), grpc_core::ResourceQuota::Default(),
          &req, NSecondsTime(15),
          GRPC_CLOSURE_CREATE(OnFinish, &request_args, grpc_schedule_on_exec_ctx),
          &request_args.response);
  httpcli_request->Start();
  PollUntil([&request_args]() {
    return request_args.done;
  });
  gpr_free(host);
}

TEST_F(HttpCliTest, Post) {
  RequestArgs request_args(this);
  grpc_httpcli_request req;
  char* host;
  grpc_core::ExecCtx exec_ctx;

  gpr_log(GPR_INFO, "test_post");

  gpr_asprintf(&host, "localhost:%d", g_server_port);
  gpr_log(GPR_INFO, "posting to %s", host);

  memset(&req, 0, sizeof(req));
  req.host = host;
  req.http.path = const_cast<char*>("/post");
  req.handshaker = &grpc_httpcli_plaintext;

  grpc_core::OrphanablePtr<grpc_core::HttpCliRequest> httpcli_request =
      grpc_core::HttpCliRequest::Post(
          pops(), grpc_core::ResourceQuota::Default(),
          &req, "hello", 5, NSecondsTime(15),
          GRPC_CLOSURE_CREATE(OnFinish, &request_args, grpc_schedule_on_exec_ctx),
          &request_args.response);
  httpcli_request->Start();
  PollUntil([&request_args]() {
    return request_args.done;
  });
  gpr_free(host);
}

int g_fake_non_responsive_dns_server_port;

void InjectNonResponsiveDNSServer(ares_channel channel) {
  gpr_log(GPR_DEBUG,
          "Injecting broken nameserver list. Bad server address:|[::1]:%d|.",
          g_fake_non_responsive_dns_server_port);
  // Configure a non-responsive DNS server at the front of c-ares's nameserver
  // list.
  struct ares_addr_port_node dns_server_addrs[1];
  dns_server_addrs[0].family = AF_INET6;
  (reinterpret_cast<char*>(&dns_server_addrs[0].addr.addr6))[15] = 0x1;
  dns_server_addrs[0].tcp_port = g_fake_non_responsive_dns_server_port;
  dns_server_addrs[0].udp_port = g_fake_non_responsive_dns_server_port;
  dns_server_addrs[0].next = nullptr;
  GPR_ASSERT(ares_set_servers_ports(channel, dns_server_addrs) == ARES_SUCCESS);
}

TEST_F(HttpCliTest, CancelGetDuringDNSResolution) {
  // Inject an unresponsive DNS server into the resolver's DNS server config
  grpc_core::testing::FakeUdpAndTcpServer fake_dns_server(
      grpc_core::testing::FakeUdpAndTcpServer::AcceptMode::
          kWaitForClientToSendFirstBytes,
      grpc_core::testing::FakeUdpAndTcpServer::CloseSocketUponCloseFromPeer);
  g_fake_non_responsive_dns_server_port = fake_dns_server.port();
  void (*prev_test_only_inject_config)(ares_channel channel) =
      grpc_ares_test_only_inject_config;
  grpc_ares_test_only_inject_config = InjectNonResponsiveDNSServer;
  // Run the same test on several threads in parallel to try to trigger races
  // etc.
  std::vector<std::thread> threads;
    for (int i = 0; i < 100; i++) {
    threads.push_back(std::thread([this]() {
      RequestArgs request_args(this);
      grpc_httpcli_request req;
      grpc_core::ExecCtx exec_ctx;
      gpr_log(GPR_INFO, "test_cancel_get_during_dns_resolution");

      memset(&req, 0, sizeof(req));
      req.host =
          const_cast<char*>("dont-care-since-wont-be-resolver.test.com:443");
      req.http.path = const_cast<char*>("/get");
      req.handshaker = &grpc_httpcli_plaintext;

      grpc_core::OrphanablePtr<grpc_core::HttpCliRequest> httpcli_request =
          grpc_core::HttpCliRequest::Get(
              pops(), grpc_core::ResourceQuota::Default(),
              &req, NSecondsTime(15),
              GRPC_CLOSURE_CREATE(OnFinishExpectCancelled, &request_args,
                                  grpc_schedule_on_exec_ctx),
              &request_args.response);
      httpcli_request->Start();
      std::thread cancel_thread([&httpcli_request]() {
        gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));
        grpc_core::ExecCtx exec_ctx;
        gpr_log(GPR_DEBUG, "now cancel http request using grpc_httpcli_cancel");
        httpcli_request.reset();
      });
      PollUntil([&request_args]() {
        return request_args.done;
      });
      cancel_thread.join();
    }));
  }
  for (auto& t : threads) {
    t.join();
  }
  grpc_ares_test_only_inject_config = prev_test_only_inject_config;
}

TEST_F(HttpCliTest, CancelGetWhileReadingResponse) {
  grpc_core::testing::FakeUdpAndTcpServer fake_http_server(
      grpc_core::testing::FakeUdpAndTcpServer::AcceptMode::
          kWaitForClientToSendFirstBytes,
      grpc_core::testing::FakeUdpAndTcpServer::CloseSocketUponCloseFromPeer);
  std::vector<std::thread> threads;
    for (int i = 0; i < 100; i++) {
    grpc_core::testing::FakeUdpAndTcpServer* fake_http_server_ptr =
        &fake_http_server;
    threads.push_back(std::thread([this, fake_http_server_ptr]() {
      RequestArgs request_args(this);
      grpc_httpcli_request req;
      grpc_core::ExecCtx exec_ctx;
      gpr_log(GPR_INFO, "test_cancel_get_while_reading_response");

      memset(&req, 0, sizeof(req));
      req.host = const_cast<char*>(fake_http_server_ptr->address());
      req.http.path = const_cast<char*>("/get");
      req.handshaker = &grpc_httpcli_plaintext;

      grpc_core::OrphanablePtr<grpc_core::HttpCliRequest> httpcli_request =
          grpc_core::HttpCliRequest::Get(
              pops(), grpc_core::ResourceQuota::Default(),
              &req, NSecondsTime(15),
              GRPC_CLOSURE_CREATE(OnFinishExpectCancelled, &request_args,
                                  grpc_schedule_on_exec_ctx),
              &request_args.response);
      httpcli_request->Start();
      exec_ctx.Flush();
      std::thread cancel_thread([&httpcli_request]() {
        gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));
        grpc_core::ExecCtx exec_ctx;
        gpr_log(GPR_DEBUG, "now cancel http request using grpc_httpcli_cancel");
        httpcli_request.reset();
      });
      PollUntil([&request_args]() {
        return request_args.done;
      });
      cancel_thread.join();
    }));
  }
  for (auto& t : threads) {
    t.join();
  }
}

} // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  // launch the test server later, so that --gtest_list_tests works
  g_argc = argc;
  g_argv = argv;
  // run tests
  return RUN_ALL_TESTS();
}
