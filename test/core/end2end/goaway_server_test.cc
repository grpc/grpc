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

#include <grpc/credentials.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <stdint.h>
#include <string.h>

#include <memory>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/event_engine/util/delegating_event_engine.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"

static gpr_mu g_mu;
static int g_resolve_port = -1;

static void set_resolve_port(int port) {
  gpr_mu_lock(&g_mu);
  g_resolve_port = port;
  gpr_mu_unlock(&g_mu);
}

namespace {

using grpc_event_engine::experimental::EventEngine;

class TestDNSResolver : public EventEngine::DNSResolver {
 public:
  explicit TestDNSResolver(std::shared_ptr<EventEngine> engine)
      : engine_(std::move(engine)),
        default_resolver_(engine_->GetDNSResolver(
            EventEngine::DNSResolver::ResolverOptions())) {}

  void LookupHostname(LookupHostnameCallback on_resolve, absl::string_view name,
                      absl::string_view default_port) override {
    CHECK(default_resolver_.ok());
    if (name != "test") {
      return (*default_resolver_)
          ->LookupHostname(std::move(on_resolve), name, default_port);
    }
    gpr_mu_lock(&g_mu);
    if (g_resolve_port < 0) {
      gpr_mu_unlock(&g_mu);
      engine_->Run([on_resolve = std::move(on_resolve)]() mutable {
        on_resolve(absl::UnknownError("Forced Failure"));
      });
    } else {
      std::vector<EventEngine::ResolvedAddress> addrs;
      struct sockaddr_in in;
      memset(&in, 0, sizeof(struct sockaddr_in));
      in.sin_family = GRPC_AF_INET;
      in.sin_addr.s_addr = 0x100007f;
      in.sin_port = grpc_htons(static_cast<uint16_t>(g_resolve_port));
      addrs.emplace_back(reinterpret_cast<const sockaddr*>(&in),
                         sizeof(struct sockaddr_in));
      gpr_mu_unlock(&g_mu);
      engine_->Run([on_resolve = std::move(on_resolve),
                    addrs = std::move(addrs)]() mutable {
        on_resolve(std::move(addrs));
      });
    }
  }
  void LookupSRV(LookupSRVCallback on_resolve,
                 absl::string_view name) override {
    CHECK(default_resolver_.ok());
    return (*default_resolver_)->LookupSRV(std::move(on_resolve), name);
  }
  void LookupTXT(LookupTXTCallback on_resolve,
                 absl::string_view name) override {
    CHECK(default_resolver_.ok());
    return (*default_resolver_)->LookupTXT(std::move(on_resolve), name);
  }

 private:
  std::shared_ptr<EventEngine> engine_;
  absl::StatusOr<std::unique_ptr<EventEngine::DNSResolver>> default_resolver_;
};

class TestEventEngine
    : public grpc_event_engine::experimental::DelegatingEventEngine {
 public:
  explicit TestEventEngine(std::shared_ptr<EventEngine> default_event_engine)
      : DelegatingEventEngine(std::move(default_event_engine)) {}

  ~TestEventEngine() override = default;

  absl::StatusOr<std::unique_ptr<DNSResolver>> GetDNSResolver(
      const DNSResolver::ResolverOptions& options) override {
    return std::make_unique<TestDNSResolver>(wrapped_engine());
  }
};

}  // namespace

int main(int argc, char** argv) {
  grpc_completion_queue* cq;
  grpc_op ops[6];
  grpc_op* op;

  grpc::testing::TestEnvironment env(&argc, argv);

  gpr_mu_init(&g_mu);
  grpc_init();
  auto test_event_engine = std::make_shared<TestEventEngine>(
      grpc_event_engine::experimental::GetDefaultEventEngine());
  grpc_event_engine::experimental::SetDefaultEventEngine(test_event_engine);

  int was_cancelled1;
  int was_cancelled2;

  grpc_metadata_array trailing_metadata_recv1;
  grpc_metadata_array request_metadata1;
  grpc_call_details request_details1;
  grpc_status_code status1;
  grpc_slice details1;
  grpc_metadata_array_init(&trailing_metadata_recv1);
  grpc_metadata_array_init(&request_metadata1);
  grpc_call_details_init(&request_details1);

  grpc_metadata_array trailing_metadata_recv2;
  grpc_metadata_array request_metadata2;
  grpc_call_details request_details2;
  grpc_status_code status2;
  grpc_slice details2;
  grpc_metadata_array_init(&trailing_metadata_recv2);
  grpc_metadata_array_init(&request_metadata2);
  grpc_call_details_init(&request_details2);

  cq = grpc_completion_queue_create_for_next(nullptr);
  grpc_core::CqVerifier cqv(cq);

  // reserve two ports
  int port1 = grpc_pick_unused_port_or_die();
  int port2 = grpc_pick_unused_port_or_die();

  std::string addr;

  auto client_args =
      grpc_core::ChannelArgs()
          .Set(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS, 1000)
          .Set(GRPC_ARG_MAX_RECONNECT_BACKOFF_MS, 1000)
          .Set(GRPC_ARG_MIN_RECONNECT_BACKOFF_MS, 5000)
          // When this test brings down server1 and then brings up server2,
          // the targeted server port number changes, and the client channel
          // needs to re-resolve to pick this up. This test requires that
          // happen within 10 seconds, but gRPC's DNS resolvers rate limit
          // resolution attempts to at most once every 30 seconds by default.
          // So we tweak it for this test.
          .Set(GRPC_ARG_DNS_MIN_TIME_BETWEEN_RESOLUTIONS_MS, 1000)
          .ToC();

  // create a channel that picks first amongst the servers
  grpc_channel_credentials* creds = grpc_insecure_credentials_create();
  grpc_channel* chan = grpc_channel_create("test", creds, client_args.get());
  grpc_channel_credentials_release(creds);
  // and an initial call to them
  grpc_slice host = grpc_slice_from_static_string("127.0.0.1");
  grpc_call* call1 =
      grpc_channel_create_call(chan, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
                               grpc_slice_from_static_string("/foo"), &host,
                               grpc_timeout_seconds_to_deadline(20), nullptr);
  // send initial metadata to probe connectivity
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = GRPC_INITIAL_METADATA_WAIT_FOR_READY;
  op->reserved = nullptr;
  op++;
  CHECK_EQ(GRPC_CALL_OK,
           grpc_call_start_batch(call1, ops, (size_t)(op - ops),
                                 grpc_core::CqVerifier::tag(0x101), nullptr));
  // and receive status to probe termination
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv1;
  op->data.recv_status_on_client.status = &status1;
  op->data.recv_status_on_client.status_details = &details1;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  CHECK_EQ(GRPC_CALL_OK,
           grpc_call_start_batch(call1, ops, (size_t)(op - ops),
                                 grpc_core::CqVerifier::tag(0x102), nullptr));

  // bring a server up on the first port
  grpc_server* server1 = grpc_server_create(nullptr, nullptr);
  addr = absl::StrCat("127.0.0.1:", port1);
  grpc_server_credentials* server_creds =
      grpc_insecure_server_credentials_create();
  grpc_server_add_http2_port(server1, addr.c_str(), server_creds);
  grpc_server_credentials_release(server_creds);
  grpc_server_register_completion_queue(server1, cq, nullptr);
  grpc_server_start(server1);

  // request a call to the server
  grpc_call* server_call1;
  CHECK_EQ(GRPC_CALL_OK,
           grpc_server_request_call(server1, &server_call1, &request_details1,
                                    &request_metadata1, cq, cq,
                                    grpc_core::CqVerifier::tag(0x301)));

  set_resolve_port(port1);

  // first call should now start
  cqv.Expect(grpc_core::CqVerifier::tag(0x101), true);
  cqv.Expect(grpc_core::CqVerifier::tag(0x301), true);
  cqv.Verify();

  CHECK(GRPC_CHANNEL_READY == grpc_channel_check_connectivity_state(chan, 0));
  grpc_channel_watch_connectivity_state(chan, GRPC_CHANNEL_READY,
                                        gpr_inf_future(GPR_CLOCK_REALTIME), cq,
                                        grpc_core::CqVerifier::tag(0x9999));

  // listen for close on the server call to probe for finishing
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled1;
  op->flags = 0;
  op++;
  CHECK_EQ(GRPC_CALL_OK,
           grpc_call_start_batch(server_call1, ops, (size_t)(op - ops),
                                 grpc_core::CqVerifier::tag(0x302), nullptr));

  // shutdown first server:
  // we should see a connectivity change and then nothing
  set_resolve_port(-1);
  grpc_server_shutdown_and_notify(server1, cq,
                                  grpc_core::CqVerifier::tag(0xdead1));
  cqv.Expect(grpc_core::CqVerifier::tag(0x9999), true);
  cqv.Verify();
  cqv.VerifyEmpty();

  // and a new call: should go through to server2 when we start it
  grpc_call* call2 =
      grpc_channel_create_call(chan, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
                               grpc_slice_from_static_string("/foo"), &host,
                               grpc_timeout_seconds_to_deadline(20), nullptr);
  // send initial metadata to probe connectivity
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = GRPC_INITIAL_METADATA_WAIT_FOR_READY;
  op->reserved = nullptr;
  op++;
  CHECK_EQ(GRPC_CALL_OK,
           grpc_call_start_batch(call2, ops, (size_t)(op - ops),
                                 grpc_core::CqVerifier::tag(0x201), nullptr));
  // and receive status to probe termination
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv2;
  op->data.recv_status_on_client.status = &status2;
  op->data.recv_status_on_client.status_details = &details2;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  CHECK_EQ(GRPC_CALL_OK,
           grpc_call_start_batch(call2, ops, (size_t)(op - ops),
                                 grpc_core::CqVerifier::tag(0x202), nullptr));

  // and bring up second server
  set_resolve_port(port2);
  grpc_server* server2 = grpc_server_create(nullptr, nullptr);
  addr = absl::StrCat("127.0.0.1:", port2);
  grpc_server_credentials* another_server_creds =
      grpc_insecure_server_credentials_create();
  grpc_server_add_http2_port(server2, addr.c_str(), another_server_creds);
  grpc_server_credentials_release(another_server_creds);
  grpc_server_register_completion_queue(server2, cq, nullptr);
  grpc_server_start(server2);

  // request a call to the server
  grpc_call* server_call2;
  CHECK_EQ(GRPC_CALL_OK,
           grpc_server_request_call(server2, &server_call2, &request_details2,
                                    &request_metadata2, cq, cq,
                                    grpc_core::CqVerifier::tag(0x401)));

  // second call should now start
  cqv.Expect(grpc_core::CqVerifier::tag(0x201), true);
  cqv.Expect(grpc_core::CqVerifier::tag(0x401), true);
  cqv.Verify();

  // listen for close on the server call to probe for finishing
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled2;
  op->flags = 0;
  op++;
  CHECK_EQ(GRPC_CALL_OK,
           grpc_call_start_batch(server_call2, ops, (size_t)(op - ops),
                                 grpc_core::CqVerifier::tag(0x402), nullptr));

  // shutdown second server: we should see nothing
  grpc_server_shutdown_and_notify(server2, cq,
                                  grpc_core::CqVerifier::tag(0xdead2));
  cqv.VerifyEmpty();

  grpc_call_cancel(call1, nullptr);
  grpc_call_cancel(call2, nullptr);

  // now everything else should finish
  cqv.Expect(grpc_core::CqVerifier::tag(0x102), true);
  cqv.Expect(grpc_core::CqVerifier::tag(0x202), true);
  cqv.Expect(grpc_core::CqVerifier::tag(0x302), true);
  cqv.Expect(grpc_core::CqVerifier::tag(0x402), true);
  cqv.Expect(grpc_core::CqVerifier::tag(0xdead1), true);
  cqv.Expect(grpc_core::CqVerifier::tag(0xdead2), true);
  cqv.Verify();

  grpc_call_unref(call1);
  grpc_call_unref(call2);
  grpc_call_unref(server_call1);
  grpc_call_unref(server_call2);
  grpc_server_destroy(server1);
  grpc_server_destroy(server2);
  grpc_channel_destroy(chan);

  grpc_metadata_array_destroy(&trailing_metadata_recv1);
  grpc_metadata_array_destroy(&request_metadata1);
  grpc_call_details_destroy(&request_details1);
  grpc_slice_unref(details1);
  grpc_metadata_array_destroy(&trailing_metadata_recv2);
  grpc_metadata_array_destroy(&request_metadata2);
  grpc_call_details_destroy(&request_details2);
  grpc_slice_unref(details2);

  grpc_completion_queue_destroy(cq);

  test_event_engine.reset();
  grpc_event_engine::experimental::ShutdownDefaultEventEngine();

  grpc_shutdown();
  gpr_mu_destroy(&g_mu);

  return 0;
}  // namespace
