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

#include <string.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/http/httpcli_ssl_credentials.h"
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

class HttpsCliTest : public ::testing::Test {
 public:
  HttpsCliTest() {
    grpc_init();
    grpc_core::ExecCtx exec_ctx;
    grpc_pollset* pollset =
        static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
    grpc_pollset_init(pollset, &mu_);
    pops_ = grpc_polling_entity_create_from_pollset(pollset);
  }
  ~HttpsCliTest() override {
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
    auto test_server =
        grpc_core::testing::StartHttpRequestTestServer(g_argc, g_argv,
                                                   true /* use_ssl */);
    g_server = test_server.server;
    g_server_port = test_server.port;
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
  explicit RequestState(HttpsCliTest* test) : test(test) {}

  ~RequestState() {
    grpc_core::ExecCtx exec_ctx;
    grpc_http_response_destroy(&response);
  }

  HttpsCliTest* test;
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

TEST_F(HttpsCliTest, Get) {
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
  request_args.push_back(grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
      const_cast<char*>("foo.test.google.fr")));
  grpc_channel_args* args = grpc_channel_args_copy_and_add(
      nullptr, request_args.data(), request_args.size());
  grpc_core::OrphanablePtr<grpc_core::HttpRequest> http_request =
      grpc_core::HttpRequest::Get("https", args, pops(), &req, NSecondsTime(15),
                              GRPC_CLOSURE_CREATE(OnFinish, &request_state,
                                                  grpc_schedule_on_exec_ctx),
                              &request_state.response,
                              grpc_core::CreateHttpRequestSSLCredentials());
  http_request->Start();
  grpc_channel_args_destroy(args);
  PollUntil([&request_state]() { return request_state.done; });
  gpr_free(host);
}

TEST_F(HttpsCliTest, Post) {
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
  request_args.push_back(grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
      const_cast<char*>("foo.test.google.fr")));
  grpc_channel_args* args = grpc_channel_args_copy_and_add(
      nullptr, request_args.data(), request_args.size());
  grpc_core::OrphanablePtr<grpc_core::HttpRequest> http_request =
      grpc_core::HttpRequest::Post(
          "https", args, pops(), &req, "hello", 5, NSecondsTime(15),
          GRPC_CLOSURE_CREATE(OnFinish, &request_state,
                              grpc_schedule_on_exec_ctx),
          &request_state.response, grpc_core::CreateHttpRequestSSLCredentials());
  http_request->Start();
  grpc_channel_args_destroy(args);
  PollUntil([&request_state]() { return request_state.done; });
  gpr_free(host);
}

TEST_F(HttpsCliTest, CancelGetDuringSSLHandshake) {
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
      request_args.push_back(grpc_channel_arg_string_create(
          const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
          const_cast<char*>("foo.test.google.fr")));
      grpc_channel_args* args = grpc_channel_args_copy_and_add(
          nullptr, request_args.data(), request_args.size());
      grpc_core::OrphanablePtr<grpc_core::HttpRequest> http_request =
          grpc_core::HttpRequest::Get(
              "https", args, pops(), &req, NSecondsTime(15),
              GRPC_CLOSURE_CREATE(OnFinishExpectCancelled, &request_state,
                                  grpc_schedule_on_exec_ctx),
              &request_state.response,
              grpc_core::CreateHttpRequestSSLCredentials());
      http_request->Start();
      grpc_channel_args_destroy(args);
      exec_ctx.Flush();
      std::thread cancel_thread([&httpcli]() {
        gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));
        grpc_core::ExecCtx exec_ctx;
        http_request.reset();
      });
      PollUntil([&request_state]() { return request_state.done; });
      cancel_thread.join();
    }));
  }
  for (auto& t : threads) {
    t.join();
  }
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
