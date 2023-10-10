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

#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/resolve_address_impl.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "src/core/lib/resolver/endpoint_addresses.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

static gpr_mu g_mu;
static int g_resolve_port = -1;

static grpc_ares_request* (*iomgr_dns_lookup_ares)(
    const char* dns_server, const char* addr, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    std::unique_ptr<grpc_core::EndpointAddressesList>* addresses,
    int query_timeout_ms);

static void (*iomgr_cancel_ares_request)(grpc_ares_request* request);

static void set_resolve_port(int port) {
  gpr_mu_lock(&g_mu);
  g_resolve_port = port;
  gpr_mu_unlock(&g_mu);
}

namespace {

class TestDNSResolver : public grpc_core::DNSResolver {
 public:
  explicit TestDNSResolver(
      std::shared_ptr<grpc_core::DNSResolver> default_resolver)
      : default_resolver_(std::move(default_resolver)),
        engine_(grpc_event_engine::experimental::GetDefaultEventEngine()) {}
  TaskHandle LookupHostname(
      std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
          on_resolved,
      absl::string_view name, absl::string_view default_port,
      grpc_core::Duration timeout, grpc_pollset_set* interested_parties,
      absl::string_view name_server) override {
    if (name != "test") {
      return default_resolver_->LookupHostname(std::move(on_resolved), name,
                                               default_port, timeout,
                                               interested_parties, name_server);
    }
    MakeDNSRequest(std::move(on_resolved));
    return kNullHandle;
  }

  absl::StatusOr<std::vector<grpc_resolved_address>> LookupHostnameBlocking(
      absl::string_view name, absl::string_view default_port) override {
    return default_resolver_->LookupHostnameBlocking(name, default_port);
  }

  TaskHandle LookupSRV(
      std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
          on_resolved,
      absl::string_view /* name */, grpc_core::Duration /* timeout */,
      grpc_pollset_set* /* interested_parties */,
      absl::string_view /* name_server */) override {
    engine_->Run([on_resolved] {
      grpc_core::ApplicationCallbackExecCtx app_exec_ctx;
      grpc_core::ExecCtx exec_ctx;
      on_resolved(absl::UnimplementedError(
          "The Testing DNS resolver does not support looking up SRV records"));
    });
    return {-1, -1};
  };

  TaskHandle LookupTXT(
      std::function<void(absl::StatusOr<std::string>)> on_resolved,
      absl::string_view /* name */, grpc_core::Duration /* timeout */,
      grpc_pollset_set* /* interested_parties */,
      absl::string_view /* name_server */) override {
    // Not supported
    engine_->Run([on_resolved] {
      grpc_core::ApplicationCallbackExecCtx app_exec_ctx;
      grpc_core::ExecCtx exec_ctx;
      on_resolved(absl::UnimplementedError(
          "The Testing DNS resolver does not support looking up TXT records"));
    });
    return {-1, -1};
  };

  bool Cancel(TaskHandle /*handle*/) override { return false; }

 private:
  void MakeDNSRequest(
      std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
          on_done) {
    gpr_mu_lock(&g_mu);
    if (g_resolve_port < 0) {
      gpr_mu_unlock(&g_mu);
      new grpc_core::DNSCallbackExecCtxScheduler(
          std::move(on_done), absl::UnknownError("Forced Failure"));
    } else {
      std::vector<grpc_resolved_address> addrs;
      grpc_resolved_address addr;
      grpc_sockaddr_in* sa = reinterpret_cast<grpc_sockaddr_in*>(&addr);
      sa->sin_family = GRPC_AF_INET;
      sa->sin_addr.s_addr = 0x100007f;
      sa->sin_port = grpc_htons(static_cast<uint16_t>(g_resolve_port));
      addr.len = static_cast<socklen_t>(sizeof(*sa));
      addrs.push_back(addr);
      gpr_mu_unlock(&g_mu);
      new grpc_core::DNSCallbackExecCtxScheduler(std::move(on_done),
                                                 std::move(addrs));
    }
  }
  std::shared_ptr<grpc_core::DNSResolver> default_resolver_;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> engine_;
};

}  // namespace

static grpc_ares_request* my_dns_lookup_ares(
    const char* dns_server, const char* addr, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    std::unique_ptr<grpc_core::EndpointAddressesList>* addresses,
    int query_timeout_ms) {
  if (0 != strcmp(addr, "test")) {
    // A records should suffice
    return iomgr_dns_lookup_ares(dns_server, addr, default_port,
                                 interested_parties, on_done, addresses,
                                 query_timeout_ms);
  }

  grpc_error_handle error;
  gpr_mu_lock(&g_mu);
  if (g_resolve_port < 0) {
    gpr_mu_unlock(&g_mu);
    error = GRPC_ERROR_CREATE("Forced Failure");
  } else {
    *addresses = std::make_unique<grpc_core::EndpointAddressesList>();
    grpc_resolved_address address;
    memset(&address, 0, sizeof(address));
    auto* sa = reinterpret_cast<grpc_sockaddr_in*>(&address.addr);
    sa->sin_family = GRPC_AF_INET;
    sa->sin_addr.s_addr = 0x100007f;
    sa->sin_port = grpc_htons(static_cast<uint16_t>(g_resolve_port));
    address.len = sizeof(grpc_sockaddr_in);
    (*addresses)->emplace_back(address, grpc_core::ChannelArgs());
    gpr_mu_unlock(&g_mu);
  }
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, error);
  return nullptr;
}

static void my_cancel_ares_request(grpc_ares_request* request) {
  if (request != nullptr) {
    iomgr_cancel_ares_request(request);
  }
}

int main(int argc, char** argv) {
  grpc_completion_queue* cq;
  grpc_op ops[6];
  grpc_op* op;

  grpc::testing::TestEnvironment env(&argc, argv);

  gpr_mu_init(&g_mu);
  grpc_init();
  grpc_core::ResetDNSResolver(
      std::make_unique<TestDNSResolver>(grpc_core::GetDNSResolver()));
  iomgr_dns_lookup_ares = grpc_dns_lookup_hostname_ares;
  iomgr_cancel_ares_request = grpc_cancel_ares_request;
  grpc_dns_lookup_hostname_ares = my_dns_lookup_ares;
  grpc_cancel_ares_request = my_cancel_ares_request;

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
          // the targetted server port number changes, and the client channel
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
  GPR_ASSERT(GRPC_CALL_OK ==
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
  GPR_ASSERT(GRPC_CALL_OK ==
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
  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_server_request_call(server1, &server_call1, &request_details1,
                                      &request_metadata1, cq, cq,
                                      grpc_core::CqVerifier::tag(0x301)));

  set_resolve_port(port1);

  // first call should now start
  cqv.Expect(grpc_core::CqVerifier::tag(0x101), true);
  cqv.Expect(grpc_core::CqVerifier::tag(0x301), true);
  cqv.Verify();

  GPR_ASSERT(GRPC_CHANNEL_READY ==
             grpc_channel_check_connectivity_state(chan, 0));
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
  GPR_ASSERT(GRPC_CALL_OK ==
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
  GPR_ASSERT(GRPC_CALL_OK ==
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
  GPR_ASSERT(GRPC_CALL_OK ==
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
  GPR_ASSERT(GRPC_CALL_OK ==
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
  GPR_ASSERT(GRPC_CALL_OK ==
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

  grpc_shutdown();
  gpr_mu_destroy(&g_mu);

  return 0;
}
