//
//
// Copyright 2015 gRPC authors.
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

#include <string.h>

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/subprocess.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/time_util.h"
#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/http/httpcli_ssl_credentials.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/uri/uri_parser.h"
#include "test/core/http/httpcli_test_util.h"
#include "test/core/util/fake_udp_and_tcp_server.h"
#include "test/core/util/test_config.h"

namespace {

grpc_core::Timestamp NSecondsTime(int seconds) {
  return grpc_core::Timestamp::FromTimespecRoundUp(
      grpc_timeout_seconds_to_deadline(seconds));
}

absl::Time AbslDeadlineSeconds(int s) {
  return grpc_core::ToAbslTime(grpc_timeout_seconds_to_deadline(s));
}

int g_argc;
char** g_argv;
int g_server_port;
gpr_subprocess* g_server;

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

  void PollUntil(const std::function<bool()>& predicate, absl::Time deadline) {
    gpr_mu_lock(mu_);
    while (!predicate()) {
      GPR_ASSERT(absl::Now() < deadline);
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
    auto test_server = grpc_core::testing::StartHttpRequestTestServer(
        g_argc, g_argv, true /* use_ssl */);
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
  GPR_ASSERT(error.ok());
  grpc_http_response response = request_state->response;
  gpr_log(GPR_INFO, "response status=%d error=%s", response.status,
          grpc_core::StatusToString(error).c_str());
  GPR_ASSERT(response.status == 200);
  GPR_ASSERT(response.body_length == strlen(expect));
  GPR_ASSERT(0 == memcmp(expect, response.body, response.body_length));
  request_state->test->RunAndKick(
      [request_state]() { request_state->done = true; });
}

void OnFinishExpectFailure(void* arg, grpc_error_handle error) {
  RequestState* request_state = static_cast<RequestState*>(arg);
  grpc_http_response response = request_state->response;
  gpr_log(GPR_INFO, "response status=%d error=%s", response.status,
          grpc_core::StatusToString(error).c_str());
  GPR_ASSERT(!error.ok());
  request_state->test->RunAndKick(
      [request_state]() { request_state->done = true; });
}

TEST_F(HttpsCliTest, Get) {
  RequestState request_state(this);
  grpc_http_request req;
  grpc_core::ExecCtx exec_ctx;
  std::string host = absl::StrFormat("localhost:%d", g_server_port);
  gpr_log(GPR_INFO, "requesting from %s", host.c_str());
  memset(&req, 0, sizeof(req));
  grpc_arg ssl_override_arg = grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
      const_cast<char*>("foo.test.google.fr"));
  grpc_channel_args args = {1, &ssl_override_arg};
  auto uri = grpc_core::URI::Create("https", host, "/get",
                                    {} /* query params */, "" /* fragment */);
  GPR_ASSERT(uri.ok());
  grpc_core::OrphanablePtr<grpc_core::HttpRequest> http_request =
      grpc_core::HttpRequest::Get(
          std::move(*uri), &args, pops(), &req, NSecondsTime(15),
          GRPC_CLOSURE_CREATE(OnFinish, &request_state,
                              grpc_schedule_on_exec_ctx),
          &request_state.response,
          grpc_core::CreateHttpRequestSSLCredentials());
  http_request->Start();
  PollUntil([&request_state]() { return request_state.done; },
            AbslDeadlineSeconds(60));
}

TEST_F(HttpsCliTest, Post) {
  RequestState request_state(this);
  grpc_http_request req;
  grpc_core::ExecCtx exec_ctx;
  std::string host = absl::StrFormat("localhost:%d", g_server_port);
  gpr_log(GPR_INFO, "posting to %s", host.c_str());
  memset(&req, 0, sizeof(req));
  req.body = const_cast<char*>("hello");
  req.body_length = 5;
  grpc_arg ssl_override_arg = grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
      const_cast<char*>("foo.test.google.fr"));
  grpc_channel_args args = {1, &ssl_override_arg};
  auto uri = grpc_core::URI::Create("https", host, "/post",
                                    {} /* query params */, "" /* fragment */);
  GPR_ASSERT(uri.ok());
  grpc_core::OrphanablePtr<grpc_core::HttpRequest> http_request =
      grpc_core::HttpRequest::Post(
          std::move(*uri), &args /* channel args */, pops(), &req,
          NSecondsTime(15),
          GRPC_CLOSURE_CREATE(OnFinish, &request_state,
                              grpc_schedule_on_exec_ctx),
          &request_state.response,
          grpc_core::CreateHttpRequestSSLCredentials());
  http_request->Start();
  PollUntil([&request_state]() { return request_state.done; },
            AbslDeadlineSeconds(60));
}

// The goal of this test is to make sure that we can cancel HTTP requests
// while they're waiting for a response from the server to finish their
// SSL handshakes. Note that the main focus of this test is to just exercise
// the relevant code paths and make sure there aren't any crashes etc., rather
// than to make sure that cancellation happens in a timely manner.
TEST_F(HttpsCliTest, CancelGetDuringSSLHandshake) {
  // Start up a fake TCP server which accepts connections and then hangs,
  // i.e. it won't send any bytes back to the client.
  grpc_core::testing::FakeUdpAndTcpServer fake_http_server(
      grpc_core::testing::FakeUdpAndTcpServer::AcceptMode::
          kWaitForClientToSendFirstBytes,
      grpc_core::testing::FakeUdpAndTcpServer::CloseSocketUponCloseFromPeer);
  // Use multiple threads to try to trigger races etc.
  int kNumThreads = 10;
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
      grpc_arg ssl_override_arg = grpc_channel_arg_string_create(
          const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
          const_cast<char*>("foo.test.google.fr"));
      grpc_channel_args args = {1, &ssl_override_arg};
      auto uri = grpc_core::URI::Create(
          "https", fake_http_server_ptr->address(), "/get",
          {} /* query params */, "" /* fragment */);
      grpc_core::OrphanablePtr<grpc_core::HttpRequest> http_request =
          grpc_core::HttpRequest::Get(
              std::move(*uri), &args, pops(), &req, NSecondsTime(120),
              GRPC_CLOSURE_CREATE(OnFinishExpectFailure, &request_state,
                                  grpc_schedule_on_exec_ctx),
              &request_state.response,
              grpc_core::CreateHttpRequestSSLCredentials());
      // Start a request. It will establish a TCP connection to the
      // server and then begin an SSL handshake. The server won't send
      // anything back though, so it will be stuck in its SSL handshake,
      // waiting for the firt response from the server.
      http_request->Start();
      exec_ctx.Flush();
      std::thread cancel_thread([&http_request]() {
        // Give one second to let the client get into the middle of its
        // SSL handshake, and then cancel the request.
        gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));
        grpc_core::ExecCtx exec_ctx;
        http_request.reset();
      });
      // Poll with a deadline explicitly lower than the request timeout, so
      // that we know that the request timeout isn't just kicking in.
      PollUntil([&request_state]() { return request_state.done; },
                AbslDeadlineSeconds(60));
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
  grpc::testing::TestEnvironment env(&argc, argv);
  // launch the test server later, so that --gtest_list_tests works
  g_argc = argc;
  g_argv = argv;
  // run tests
  return RUN_ALL_TESTS();
}
