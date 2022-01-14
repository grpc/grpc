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
#include "test/core/http/httpcli_test_util.h"
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
        grpc_pollset_kick(grpc_polling_entity_pollset(&pops_), nullptr)));
  }

  void PollUntil(const std::function<bool()>& predicate) {
    gpr_mu_lock(mu_);
    while (!predicate()) {
      grpc_pollset_worker* worker = nullptr;
      GPR_ASSERT(GRPC_LOG_IF_ERROR(
          "pollset_work", grpc_pollset_work(grpc_polling_entity_pollset(&pops_),
                                            &worker, NSecondsTime(1))));
      gpr_mu_unlock(mu_);
      gpr_mu_lock(mu_);
    }
    gpr_mu_unlock(mu_);
  }

  grpc_polling_entity* pops() { return &pops_; }

 protected:
  static void SetUpTestSuite() {
    std::tuple<gpr_subprocess*, int> server_and_port =
        grpc_core::testing::StartHttpCliTestServer(g_argc, g_argv,
                                                   false /* use_ssl */);
    g_server = std::get<0>(server_and_port);
    g_server_port = std::get<1>(server_and_port);
  }

  static void TearDownTestSuite() { gpr_subprocess_destroy(g_server); }

 private:
  static void DestroyPops(void* p, grpc_error_handle /*error*/) {
    grpc_polling_entity* pops = static_cast<grpc_polling_entity*>(p);
    grpc_pollset_destroy(grpc_polling_entity_pollset(pops));
    gpr_free(grpc_polling_entity_pollset(pops));
  }

  gpr_mu* mu_;
  grpc_polling_entity pops_;
};

struct RequestState {
  explicit RequestState(HttpCliTest* test) : test(test) {}

  ~RequestState() {
    grpc_core::ExecCtx exec_ctx;
    grpc_http_response_destroy(&response);
  }

  HttpCliTest* test;
  bool done = false;
  grpc_http_response response = {};
};

void OnFinish(void* arg, grpc_error_handle error) {
  RequestState* request_state = static_cast<RequestState*>(arg);
  const char* expect =
      "<html><head><title>Hello world!</title></head>"
      "<body><p>This is a test</p></body></html>";
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_http_response response = request_state->response;
  gpr_log(GPR_INFO, "response status=%d error=%s", response.status,
          grpc_error_std_string(error).c_str());
  GPR_ASSERT(response.status == 200);
  GPR_ASSERT(response.body_length == strlen(expect));
  GPR_ASSERT(0 == memcmp(expect, response.body, response.body_length));
  request_state->test->RunAndKick(
      [request_state]() { request_state->done = true; });
}

void OnFinishExpectCancelled(void* arg, grpc_error_handle error) {
  RequestState* request_state = static_cast<RequestState*>(arg);
  grpc_http_response response = request_state->response;
  gpr_log(GPR_INFO, "response status=%d error=%s", response.status,
          grpc_error_std_string(error).c_str());
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  request_state->test->RunAndKick(
      [request_state]() { request_state->done = true; });
}

TEST_F(HttpCliTest, Get) {
  RequestState request_state(this);
  grpc_http_request req;
  char* host;
  grpc_core::ExecCtx exec_ctx;
  gpr_asprintf(&host, "localhost:%d", g_server_port);
  gpr_log(GPR_INFO, "requesting from %s", host);
  memset(&req, 0, sizeof(req));
  req.path = const_cast<char*>("/get");
  std::vector<grpc_arg> request_args;
  request_args.push_back(grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY), host));
  grpc_channel_args* args = grpc_channel_args_copy_and_add(
      nullptr, request_args.data(), request_args.size());
  grpc_core::OrphanablePtr<grpc_core::HttpCli> httpcli =
      grpc_core::HttpCli::Get("http", args, pops(), &req, NSecondsTime(15),
                              GRPC_CLOSURE_CREATE(OnFinish, &request_state,
                                                  grpc_schedule_on_exec_ctx),
                              &request_state.response, grpc_insecure_credentials_create());
  httpcli->Start();
  grpc_channel_args_destroy(args);
  PollUntil([&request_state]() { return request_state.done; });
  gpr_free(host);
}

TEST_F(HttpCliTest, Post) {
  RequestState request_state(this);
  grpc_http_request req;
  char* host;
  grpc_core::ExecCtx exec_ctx;
  gpr_asprintf(&host, "localhost:%d", g_server_port);
  gpr_log(GPR_INFO, "posting to %s", host);
  memset(&req, 0, sizeof(req));
  req.path = const_cast<char*>("/post");
  std::vector<grpc_arg> request_args;
  request_args.push_back(grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY), host));
  grpc_channel_args* args = grpc_channel_args_copy_and_add(
      nullptr, request_args.data(), request_args.size());
  grpc_core::OrphanablePtr<grpc_core::HttpCli> httpcli =
      grpc_core::HttpCli::Post("http", args, pops(), &req, "hello", 5,
                               NSecondsTime(15),
                               GRPC_CLOSURE_CREATE(OnFinish, &request_state,
                                                   grpc_schedule_on_exec_ctx),
                               &request_state.response, grpc_insecure_credentials_create());
  httpcli->Start();
  grpc_channel_args_destroy(args);
  PollUntil([&request_state]() { return request_state.done; });
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
  int kNumThreads = 100;
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    threads.push_back(std::thread([this]() {
      RequestState request_state(this);
      grpc_http_request req;
      grpc_core::ExecCtx exec_ctx;
      memset(&req, 0, sizeof(req));
      req.path = const_cast<char*>("/get");
      std::vector<grpc_arg> request_args;
      request_args.push_back(grpc_channel_arg_string_create(
          const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY),
          const_cast<char*>("dont-care-since-wont-be-resolved.test.com:443")));
      grpc_channel_args* args = grpc_channel_args_copy_and_add(
          nullptr, request_args.data(), request_args.size());
      grpc_core::OrphanablePtr<grpc_core::HttpCli> httpcli =
          grpc_core::HttpCli::Get(
              "http", args, pops(), &req, NSecondsTime(15),
              GRPC_CLOSURE_CREATE(OnFinishExpectCancelled, &request_state,
                                  grpc_schedule_on_exec_ctx),
              &request_state.response, grpc_insecure_credentials_create());
      httpcli->Start();
      grpc_channel_args_destroy(args);
      std::thread cancel_thread([&httpcli]() {
        gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));
        grpc_core::ExecCtx exec_ctx;
        httpcli.reset();
      });
      PollUntil([&request_state]() { return request_state.done; });
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
  int kNumThreads = 100;
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    grpc_core::testing::FakeUdpAndTcpServer* fake_http_server_ptr =
        &fake_http_server;
    threads.push_back(std::thread([this, fake_http_server_ptr]() {
      RequestState request_state(this);
      grpc_http_request req;
      grpc_core::ExecCtx exec_ctx;
      memset(&req, 0, sizeof(req));
      req.path = const_cast<char*>("/get");
      std::vector<grpc_arg> request_args;
      request_args.push_back(grpc_channel_arg_string_create(
          const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY),
          const_cast<char*>(fake_http_server_ptr->address())));
      grpc_channel_args* args = grpc_channel_args_copy_and_add(
          nullptr, request_args.data(), request_args.size());
      grpc_core::OrphanablePtr<grpc_core::HttpCli> httpcli =
          grpc_core::HttpCli::Get(
              "http", args, pops(), &req, NSecondsTime(15),
              GRPC_CLOSURE_CREATE(OnFinishExpectCancelled, &request_state,
                                  grpc_schedule_on_exec_ctx),
              &request_state.response, grpc_insecure_credentials_create());
      httpcli->Start();
      grpc_channel_args_destroy(args);
      exec_ctx.Flush();
      std::thread cancel_thread([&httpcli]() {
        gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));
        grpc_core::ExecCtx exec_ctx;
        httpcli.reset();
      });
      PollUntil([&request_state]() { return request_state.done; });
      cancel_thread.join();
    }));
  }
  for (auto& t : threads) {
    t.join();
  }
}

// The point of this test is just to exercise the machinery around cancellation
// during TCP connection establishment, to make sure there are no crashes/races
// etc. This test doesn't actually verify that cancellation during TCP setup is
// timely, though. For that, we would need to fake packet loss in the test.
TEST_F(HttpCliTest, CancelGetRacesWithConnectionFailure) {
  // Grab an unoccupied port but don't listen on it. The goal
  // here is just to have a server address that will reject
  // TCP connection setups.
  int fake_server_port = grpc_pick_unused_port_or_die();
  std::string fake_server_address =
      absl::StrCat("[::1]:", std::to_string(fake_server_port));
  int kNumThreads = 100;
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    threads.push_back(std::thread([this, fake_server_address]() {
      RequestState request_state(this);
      grpc_http_request req;
      grpc_core::ExecCtx exec_ctx;
      memset(&req, 0, sizeof(req));
      req.path = const_cast<char*>("/get");
      std::vector<grpc_arg> request_args;
      request_args.push_back(grpc_channel_arg_string_create(
          const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY),
          const_cast<char*>(fake_server_address.c_str())));
      grpc_channel_args* args = grpc_channel_args_copy_and_add(
          nullptr, request_args.data(), request_args.size());
      grpc_core::OrphanablePtr<grpc_core::HttpCli> httpcli =
          grpc_core::HttpCli::Get(
              "http", args, pops(), &req, NSecondsTime(15),
              GRPC_CLOSURE_CREATE(OnFinishExpectCancelled, &request_state,
                                  grpc_schedule_on_exec_ctx),
              &request_state.response, grpc_insecure_credentials_create());
      httpcli->Start();
      grpc_channel_args_destroy(args);
      exec_ctx.Flush();
      std::thread cancel_thread([&httpcli]() {
        grpc_core::ExecCtx exec_ctx;
        httpcli.reset();
      });
      PollUntil([&request_state]() { return request_state.done; });
      cancel_thread.join();
    }));
  }
  for (auto& t : threads) {
    t.join();
  }
}

// The point of this test is just to exercise the machinery around cancellation
// during TCP connection establishment, to make sure there are no crashes/races
// etc. This test doesn't actually verify that cancellation during TCP setup is
// timely, though. For that, we would need to fake packet loss in the test.
TEST_F(HttpCliTest, CancelGetRacesWithConnectionSuccess) {
  // Grab an unoccupied port but don't listen on it. The goal
  // here is just to have a server address that will reject
  // TCP connection setups.
  int fake_server_port = grpc_pick_unused_port_or_die();
  std::string fake_server_address =
      absl::StrCat("[::1]:", std::to_string(fake_server_port));
  RequestState request_state(this);
  grpc_http_request req;
  grpc_core::ExecCtx exec_ctx;
  memset(&req, 0, sizeof(req));
  req.path = const_cast<char*>("/get");
  grpc_pollset_set* pollset_set_to_destroy_eagerly = grpc_pollset_set_create();
  grpc_polling_entity_add_to_pollset_set(pops(),
                                         pollset_set_to_destroy_eagerly);
  grpc_polling_entity wrapped_pollset_set_to_destroy_eagerly =
      grpc_polling_entity_create_from_pollset_set(
          pollset_set_to_destroy_eagerly);
  std::vector<grpc_arg> request_args;
  request_args.push_back(grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY),
      const_cast<char*>(fake_server_address.c_str())));
  grpc_channel_args* args = grpc_channel_args_copy_and_add(
      nullptr, request_args.data(), request_args.size());
  grpc_core::OrphanablePtr<grpc_core::HttpCli> httpcli =
      grpc_core::HttpCli::Get(
          "http", args, &wrapped_pollset_set_to_destroy_eagerly, &req,
          NSecondsTime(15),
          GRPC_CLOSURE_CREATE(OnFinishExpectCancelled, &request_state,
                              grpc_schedule_on_exec_ctx),
          &request_state.response, grpc_insecure_credentials_create());
  httpcli->Start();
  grpc_channel_args_destroy(args);
  exec_ctx.Flush();
  httpcli.reset();  // cancel the request
  exec_ctx.Flush();
  // because we're cancelling the request during TCP connection establishment,
  // we can be certain that our on_done callback has already ran
  GPR_ASSERT(request_state.done);
  // Destroy the request's polling entity param. The goal is to try to catch a
  // bug where we might still be referencing the polling entity by
  // a pending TCP connect.
  gpr_log(GPR_DEBUG, "apolcyn begin destroy pollset set");
  grpc_pollset_set_destroy(pollset_set_to_destroy_eagerly);
  gpr_log(GPR_DEBUG, "apolcyn finish destroy pollset set");
  exec_ctx.Flush();
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  // launch the test server later, so that --gtest_list_tests works
  g_argc = argc;
  g_argv = argv;
  // run tests
  return RUN_ALL_TESTS();
}
