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

#include "src/core/lib/http/httpcli.h"

#include <string.h>
#include <sys/socket.h>

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <ares.h>
#include <gtest/gtest.h>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/time_util.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "test/core/http/httpcli_test_util.h"
#include "test/core/util/fake_udp_and_tcp_server.h"
#include "test/core/util/port.h"
#include "test/core/util/subprocess.h"
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

class HttpRequestTest : public ::testing::Test {
 public:
  HttpRequestTest() {
    grpc_init();
    grpc_core::ExecCtx exec_ctx;
    grpc_pollset* pollset =
        static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
    grpc_pollset_init(pollset, &mu_);
    pops_ = grpc_polling_entity_create_from_pollset(pollset);
  }
  ~HttpRequestTest() override {
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
        g_argc, g_argv, false /* use_ssl */);
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
  explicit RequestState(HttpRequestTest* test) : test(test) {}

  ~RequestState() {
    grpc_core::ExecCtx exec_ctx;
    grpc_http_response_destroy(&response);
  }

  HttpRequestTest* test;
  bool done = false;
  grpc_http_response response = {};
  grpc_pollset_set* pollset_set_to_destroy_eagerly = nullptr;
};

void OnFinish(void* arg, grpc_error_handle error) {
  RequestState* request_state = static_cast<RequestState*>(arg);
  if (request_state->pollset_set_to_destroy_eagerly != nullptr) {
    // Destroy the request's polling entity param. The goal is to try to catch a
    // bug where we might still be referencing the polling entity by
    // a pending TCP connect.
    grpc_pollset_set_destroy(request_state->pollset_set_to_destroy_eagerly);
  }
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
  if (request_state->pollset_set_to_destroy_eagerly != nullptr) {
    // Destroy the request's polling entity param. The goal is to try to catch a
    // bug where we might still be referencing the polling entity by
    // a pending TCP connect.
    grpc_pollset_set_destroy(request_state->pollset_set_to_destroy_eagerly);
  }
  grpc_http_response response = request_state->response;
  gpr_log(GPR_INFO, "response status=%d error=%s", response.status,
          grpc_core::StatusToString(error).c_str());
  GPR_ASSERT(!error.ok());
  request_state->test->RunAndKick(
      [request_state]() { request_state->done = true; });
}

TEST_F(HttpRequestTest, Get) {
  RequestState request_state(this);
  grpc_http_request req;
  grpc_core::ExecCtx exec_ctx;
  std::string host = absl::StrFormat("localhost:%d", g_server_port);
  gpr_log(GPR_INFO, "requesting from %s", host.c_str());
  memset(&req, 0, sizeof(req));
  auto uri = grpc_core::URI::Create("http", host, "/get", {} /* query params */,
                                    "" /* fragment */);
  GPR_ASSERT(uri.ok());
  grpc_core::OrphanablePtr<grpc_core::HttpRequest> http_request =
      grpc_core::HttpRequest::Get(
          std::move(*uri), nullptr /* channel args */, pops(), &req,
          NSecondsTime(15),
          GRPC_CLOSURE_CREATE(OnFinish, &request_state,
                              grpc_schedule_on_exec_ctx),
          &request_state.response,
          grpc_core::RefCountedPtr<grpc_channel_credentials>(
              grpc_insecure_credentials_create()));
  http_request->Start();
  PollUntil([&request_state]() { return request_state.done; },
            AbslDeadlineSeconds(60));
}

TEST_F(HttpRequestTest, Post) {
  RequestState request_state(this);
  grpc_http_request req;
  grpc_core::ExecCtx exec_ctx;
  std::string host = absl::StrFormat("localhost:%d", g_server_port);
  gpr_log(GPR_INFO, "posting to %s", host.c_str());
  memset(&req, 0, sizeof(req));
  req.body = const_cast<char*>("hello");
  req.body_length = 5;
  auto uri = grpc_core::URI::Create("http", host, "/post",
                                    {} /* query params */, "" /* fragment */);
  GPR_ASSERT(uri.ok());
  grpc_core::OrphanablePtr<grpc_core::HttpRequest> http_request =
      grpc_core::HttpRequest::Post(
          std::move(*uri), nullptr /* channel args */, pops(), &req,
          NSecondsTime(15),
          GRPC_CLOSURE_CREATE(OnFinish, &request_state,
                              grpc_schedule_on_exec_ctx),
          &request_state.response,
          grpc_core::RefCountedPtr<grpc_channel_credentials>(
              grpc_insecure_credentials_create()));
  http_request->Start();
  PollUntil([&request_state]() { return request_state.done; },
            AbslDeadlineSeconds(60));
}

int g_fake_non_responsive_dns_server_port;

void InjectNonResponsiveDNSServer(ares_channel* channel) {
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
  GPR_ASSERT(ares_set_servers_ports(*channel, dns_server_addrs) ==
             ARES_SUCCESS);
}

TEST_F(HttpRequestTest, CancelGetDuringDNSResolution) {
  // Inject an unresponsive DNS server into the resolver's DNS server config
  grpc_core::testing::FakeUdpAndTcpServer fake_dns_server(
      grpc_core::testing::FakeUdpAndTcpServer::AcceptMode::
          kWaitForClientToSendFirstBytes,
      grpc_core::testing::FakeUdpAndTcpServer::CloseSocketUponCloseFromPeer);
  g_fake_non_responsive_dns_server_port = fake_dns_server.port();
  void (*prev_test_only_inject_config)(ares_channel* channel) =
      grpc_ares_test_only_inject_config;
  grpc_ares_test_only_inject_config = InjectNonResponsiveDNSServer;
  // Run the same test on several threads in parallel to try to trigger races
  // etc.
  int kNumThreads = 10;
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    threads.push_back(std::thread([this]() {
      RequestState request_state(this);
      grpc_http_request req;
      grpc_core::ExecCtx exec_ctx;
      memset(&req, 0, sizeof(grpc_http_request));
      auto uri = grpc_core::URI::Create(
          "http", "dont-care-since-wont-be-resolved.test.com:443", "/get",
          {} /* query params */, "" /* fragment */);
      GPR_ASSERT(uri.ok());
      grpc_core::OrphanablePtr<grpc_core::HttpRequest> http_request =
          grpc_core::HttpRequest::Get(
              std::move(*uri), nullptr /* channel args */, pops(), &req,
              NSecondsTime(120),
              GRPC_CLOSURE_CREATE(OnFinishExpectFailure, &request_state,
                                  grpc_schedule_on_exec_ctx),
              &request_state.response,
              grpc_core::RefCountedPtr<grpc_channel_credentials>(
                  grpc_insecure_credentials_create()));
      http_request->Start();
      std::thread cancel_thread([&http_request]() {
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
  grpc_ares_test_only_inject_config = prev_test_only_inject_config;
}

TEST_F(HttpRequestTest, CancelGetWhileReadingResponse) {
  // Start up a fake HTTP server which just accepts connections
  // and then hangs, i.e. does not send back any bytes to the client.
  // The goal here is to get the client to connect to this fake server
  // and send a request, and then sit waiting for a response. Then, a
  // separate thread will cancel the HTTP request, and that should let it
  // complete.
  grpc_core::testing::FakeUdpAndTcpServer fake_http_server(
      grpc_core::testing::FakeUdpAndTcpServer::AcceptMode::
          kWaitForClientToSendFirstBytes,
      grpc_core::testing::FakeUdpAndTcpServer::CloseSocketUponCloseFromPeer);
  // Run the same test on several threads in parallel to try to trigger races
  // etc.
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
      auto uri = grpc_core::URI::Create("http", fake_http_server_ptr->address(),
                                        "/get", {} /* query params */,
                                        "" /* fragment */);
      GPR_ASSERT(uri.ok());
      grpc_core::OrphanablePtr<grpc_core::HttpRequest> http_request =
          grpc_core::HttpRequest::Get(
              std::move(*uri), nullptr /* channel args */, pops(), &req,
              NSecondsTime(120),
              GRPC_CLOSURE_CREATE(OnFinishExpectFailure, &request_state,
                                  grpc_schedule_on_exec_ctx),
              &request_state.response,
              grpc_core::RefCountedPtr<grpc_channel_credentials>(
                  grpc_insecure_credentials_create()));
      http_request->Start();
      exec_ctx.Flush();
      std::thread cancel_thread([&http_request]() {
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

// The main point of this test is just to exercise the machinery around
// cancellation during TCP connection establishment, to make sure there are no
// crashes/races etc. This test doesn't actually verify that cancellation during
// TCP setup is happening, though. For that, we would need to induce packet loss
// in the test.
TEST_F(HttpRequestTest, CancelGetRacesWithConnectionFailure) {
  // Grab an unoccupied port but don't listen on it. The goal
  // here is just to have a server address that will reject
  // TCP connection setups.
  // Note that because the server is rejecting TCP connections, we
  // don't really need to cancel the HTTP requests in this test case
  // in order for them proceeed i.e. in order for them to pass. The test
  // is still beneficial though because it can exercise the same code paths
  // that would get taken if the HTTP request was cancelled while the TCP
  // connect attempt was actually hanging.
  int fake_server_port = grpc_pick_unused_port_or_die();
  std::string fake_server_address =
      absl::StrCat("[::1]:", std::to_string(fake_server_port));
  // Run the same test on several threads in parallel to try to trigger races
  // etc.
  int kNumThreads = 10;
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    threads.push_back(std::thread([this, fake_server_address]() {
      RequestState request_state(this);
      grpc_http_request req;
      grpc_core::ExecCtx exec_ctx;
      memset(&req, 0, sizeof(req));
      auto uri =
          grpc_core::URI::Create("http", fake_server_address, "/get",
                                 {} /* query params */, "" /* fragment */);
      GPR_ASSERT(uri.ok());
      grpc_core::OrphanablePtr<grpc_core::HttpRequest> http_request =
          grpc_core::HttpRequest::Get(
              std::move(*uri), nullptr /* channel args */, pops(), &req,
              NSecondsTime(120),
              GRPC_CLOSURE_CREATE(OnFinishExpectFailure, &request_state,
                                  grpc_schedule_on_exec_ctx),
              &request_state.response,
              grpc_core::RefCountedPtr<grpc_channel_credentials>(
                  grpc_insecure_credentials_create()));
      // Start the HTTP request. We will ~immediately begin a TCP connect
      // attempt because there's no name to resolve.
      http_request->Start();
      exec_ctx.Flush();
      // Spawn a separate thread which ~immediately cancels the HTTP request.
      // Note that even though the server is rejecting TCP connections, it can
      // still take some time for the client to receive that rejection. So
      // cancelling the request now can trigger the code paths that would get
      // taken if the TCP connection was truly hanging e.g. from  packet loss.
      // The goal is just to make sure there are no crashes, races, etc.
      std::thread cancel_thread([&http_request]() {
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

// The pollent parameter passed to HttpRequest::Get or Post is owned by
// the caller and must not be referenced by the HttpRequest after the
// requests's on_done callback is invoked. This test verifies that this
// isn't happening by destroying the request's pollset set within the
// on_done callback.
TEST_F(HttpRequestTest, CallerPollentsAreNotReferencedAfterCallbackIsRan) {
  // Grab an unoccupied port but don't listen on it. The goal
  // here is just to have a server address that will reject
  // TCP connection setups.
  // Note that we could have used a different server for this test case, e.g.
  // one which accepts TCP connections. All we need here is something for the
  // client to connect to, since it will be cancelled roughly during the
  // connection attempt anyways.
  int fake_server_port = grpc_pick_unused_port_or_die();
  std::string fake_server_address =
      absl::StrCat("[::1]:", std::to_string(fake_server_port));
  RequestState request_state(this);
  grpc_http_request req;
  grpc_core::ExecCtx exec_ctx;
  memset(&req, 0, sizeof(req));
  req.path = const_cast<char*>("/get");
  request_state.pollset_set_to_destroy_eagerly = grpc_pollset_set_create();
  grpc_polling_entity_add_to_pollset_set(
      pops(), request_state.pollset_set_to_destroy_eagerly);
  grpc_polling_entity wrapped_pollset_set_to_destroy_eagerly =
      grpc_polling_entity_create_from_pollset_set(
          request_state.pollset_set_to_destroy_eagerly);
  auto uri = grpc_core::URI::Create("http", fake_server_address, "/get",
                                    {} /* query params */, "" /* fragment */);
  GPR_ASSERT(uri.ok());
  grpc_core::OrphanablePtr<grpc_core::HttpRequest> http_request =
      grpc_core::HttpRequest::Get(
          std::move(*uri), nullptr /* channel args */,
          &wrapped_pollset_set_to_destroy_eagerly, &req, NSecondsTime(15),
          GRPC_CLOSURE_CREATE(OnFinishExpectFailure, &request_state,
                              grpc_schedule_on_exec_ctx),
          &request_state.response,
          grpc_core::RefCountedPtr<grpc_channel_credentials>(
              grpc_insecure_credentials_create()));
  // Start the HTTP request. We'll start the TCP connect attempt right away.
  http_request->Start();
  exec_ctx.Flush();
  http_request.reset();  // cancel the request
  // Since the request was cancelled, the on_done callback should be flushed
  // out on the ExecCtx flush below. When the on_done callback is ran, it will
  // eagerly destroy 'request_state.pollset_set_to_destroy_eagerly'. Thus, we
  // can't poll on that pollset here.
  exec_ctx.Flush();
}

void CancelRequest(grpc_core::HttpRequest* req) {
  gpr_log(
      GPR_INFO,
      "test only HttpRequest::OnHandshakeDone intercept orphaning request: %p",
      req);
  req->Orphan();
}

// This exercises the code paths that happen when we cancel an HTTP request
// before the security handshake callback runs, but after that callback has
// already been scheduled with a success result. This case is interesting
// because the current security handshake API transfers ownership of output
// arguments to the caller only if the handshake is successful, rendering
// this code path as something that only occurs with just the right timing.
TEST_F(HttpRequestTest,
       CancelDuringSecurityHandshakeButHandshakeStillSucceeds) {
  RequestState request_state(this);
  grpc_http_request req;
  grpc_core::ExecCtx exec_ctx;
  std::string host = absl::StrFormat("localhost:%d", g_server_port);
  gpr_log(GPR_INFO, "requesting from %s", host.c_str());
  memset(&req, 0, sizeof(req));
  auto uri = grpc_core::URI::Create("http", host, "/get", {} /* query params */,
                                    "" /* fragment */);
  GPR_ASSERT(uri.ok());
  grpc_core::OrphanablePtr<grpc_core::HttpRequest> http_request =
      grpc_core::HttpRequest::Get(
          std::move(*uri), nullptr /* channel args */, pops(), &req,
          NSecondsTime(15),
          GRPC_CLOSURE_CREATE(OnFinishExpectFailure, &request_state,
                              grpc_schedule_on_exec_ctx),
          &request_state.response,
          grpc_core::RefCountedPtr<grpc_channel_credentials>(
              grpc_insecure_credentials_create()));
  grpc_core::HttpRequest::TestOnlySetOnHandshakeDoneIntercept(CancelRequest);
  http_request->Start();
  (void)http_request.release();  // request will be orphaned by CancelRequest
  exec_ctx.Flush();
  PollUntil([&request_state]() { return request_state.done; },
            AbslDeadlineSeconds(60));
  grpc_core::HttpRequest::TestOnlySetOnHandshakeDoneIntercept(nullptr);
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
