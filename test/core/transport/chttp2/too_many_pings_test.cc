/*
 *
 * Copyright 2020 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <gmock/gmock.h>
#include <stdlib.h>
#include <string.h>
#include <functional>
#include <set>
#include <thread>

#include "absl/strings/str_cat.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include <grpcpp/impl/codegen/service_type.h>
#include <grpcpp/server_builder.h>

#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/security/credentials/alts/alts_credentials.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/alts/alts_security_connector.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/channel.h"

#include "test/core/util/memory_counters.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#include "test/core/end2end/cq_verifier.h"

namespace {

class TransportCounter {
 public:
  static void CounterInitCallback() {
    absl::MutexLock lock(&mu());
    ++count_;
  }

  static void CounterDestructCallback() {
    absl::MutexLock lock(&mu());
    if (--count_ == 0) {
      cv().SignalAll();
    }
  }

  static void WaitForTransportsToBeDestroyed() {
    absl::MutexLock lock(&mu());
    while (count_ != 0) {
      ASSERT_FALSE(cv().WaitWithTimeout(&mu(), absl::Seconds(10)));
    }
  }

  static int count() {
    absl::MutexLock lock(&mu());
    return count_;
  }

  static absl::Mutex& mu() {
    static absl::Mutex* mu = new absl::Mutex();
    return *mu;
  }

  static absl::CondVar& cv() {
    static absl::CondVar* cv = new absl::CondVar();
    return *cv;
  }

 private:
  static int count_;
};

int TransportCounter::count_ = 0;

void* tag(intptr_t t) { return reinterpret_cast<void*>(t); }

// Perform a simple RPC where the server cancels the request with
// grpc_call_cancel_with_status
grpc_status_code PerformCall(grpc_channel* channel, grpc_server* server,
                             grpc_completion_queue* cq) {
  grpc_call* c;
  grpc_call* s;
  cq_verifier* cqv = cq_verifier_create(cq);
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(5);
  // Start a call
  c = grpc_channel_create_call(channel, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               deadline, nullptr);
  GPR_ASSERT(c);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(1),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  // Request a call on the server
  error = grpc_server_request_call(server, &s, &call_details,
                                   &request_metadata_recv, cq, cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  cq_verify(cqv);
  grpc_call_cancel_with_status(s, GRPC_STATUS_PERMISSION_DENIED, "test status",
                               nullptr);
  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);
  // cleanup
  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_call_unref(c);
  grpc_call_unref(s);
  cq_verifier_destroy(cqv);
  return status;
}

// Test that sending a lot of RPCs that are cancelled by the server doesn't
// result in too many pings due to the pings sent by BDP.
TEST(TooManyPings, TestLotsOfServerCancelledRpcsDoesntGiveTooManyPings) {
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  // create the server
  grpc_server* server = grpc_server_create(nullptr, nullptr);
  std::string server_address =
      grpc_core::JoinHostPort("localhost", grpc_pick_unused_port_or_die());
  grpc_server_register_completion_queue(server, cq, nullptr);
  GPR_ASSERT(
      grpc_server_add_insecure_http2_port(server, server_address.c_str()));
  grpc_server_start(server);
  // create the channel (bdp pings are enabled by default)
  grpc_channel* channel = grpc_insecure_channel_create(
      server_address.c_str(), nullptr /* channel args */, nullptr);
  std::map<grpc_status_code, int> statuses_and_counts;
  const int kNumTotalRpcs = 1e5;
  // perform an RPC
  gpr_log(GPR_INFO,
          "Performing %d total RPCs and expecting them all to receive status "
          "PERMISSION_DENIED (%d)",
          kNumTotalRpcs, GRPC_STATUS_PERMISSION_DENIED);
  for (int i = 0; i < kNumTotalRpcs; i++) {
    grpc_status_code status = PerformCall(channel, server, cq);
    statuses_and_counts[status] += 1;
  }
  int num_not_cancelled = 0;
  for (auto itr = statuses_and_counts.begin(); itr != statuses_and_counts.end();
       itr++) {
    if (itr->first != GRPC_STATUS_PERMISSION_DENIED) {
      num_not_cancelled += itr->second;
    }
    gpr_log(GPR_INFO, "%d / %d RPCs received status code: %d", itr->second,
            kNumTotalRpcs, itr->first);
  }
  if (num_not_cancelled > 0) {
    gpr_log(GPR_ERROR,
            "Expected all RPCs to receive status PERMISSION_DENIED (%d) but %d "
            "received other status codes",
            GRPC_STATUS_PERMISSION_DENIED, num_not_cancelled);
    FAIL();
  }
  // shutdown and destroy the client and server
  grpc_channel_destroy(channel);
  grpc_server_shutdown_and_notify(server, cq, nullptr);
  grpc_completion_queue_shutdown(cq);
  while (grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                    nullptr)
             .type != GRPC_QUEUE_SHUTDOWN) {
  }
  grpc_server_destroy(server);
  grpc_completion_queue_destroy(cq);
}

// Perform a simple RPC where the client makes a request, and both the client
// and server continue reading so that gRPC can send and receive keepalive
// pings.
grpc_status_code PerformWaitingCall(grpc_channel* channel, grpc_server* server,
                                    grpc_completion_queue* cq) {
  grpc_call* c;
  grpc_call* s;
  cq_verifier* cqv = cq_verifier_create(cq);
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(15);
  // Start a call
  c = grpc_channel_create_call(channel, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               deadline, nullptr);
  GPR_ASSERT(c);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(1),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  // Request a call on the server
  error = grpc_server_request_call(server, &s, &call_details,
                                   &request_metadata_recv, cq, cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  cq_verify(cqv);
  // Since the server is configured to allow only a single ping strike, it would
  // take 3 pings to trigger the GOAWAY frame with "too_many_pings" from the
  // server. (The second ping from the client would be the first bad ping sent
  // too quickly leading to a ping strike and the third ping would lead to the
  // GOAWAY.) If the client settings match with the server's settings, there
  // won't be a bad ping, and the call will end due to the deadline expiring
  // instead.
  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  // The call will end after this
  cq_verify(cqv, 60);
  // cleanup
  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_call_unref(c);
  grpc_call_unref(s);
  cq_verifier_destroy(cqv);
  return status;
}

// Shuts down and destroys the server.
void ServerShutdownAndDestroy(grpc_server* server, grpc_completion_queue* cq) {
  // Shutdown and destroy server
  grpc_server_shutdown_and_notify(server, cq, reinterpret_cast<void*>(1000));
  while (grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                    nullptr)
             .tag != reinterpret_cast<void*>(1000)) {
  }
  grpc_server_destroy(server);
}

void VerifyChannelReady(grpc_channel* channel, grpc_completion_queue* cq) {
  grpc_connectivity_state state =
      grpc_channel_check_connectivity_state(channel, 1 /* try_to_connect */);
  while (state != GRPC_CHANNEL_READY) {
    grpc_channel_watch_connectivity_state(
        channel, state, grpc_timeout_seconds_to_deadline(5), cq, nullptr);
    grpc_completion_queue_next(cq, grpc_timeout_seconds_to_deadline(5),
                               nullptr);
    state = grpc_channel_check_connectivity_state(channel, 0);
  }
}

void VerifyChannelDisconnected(grpc_channel* channel,
                               grpc_completion_queue* cq) {
  // Verify channel gets disconnected. Use a ping to make sure that clients
  // tries sending/receiving bytes if the channel is connected.
  grpc_channel_ping(channel, cq, reinterpret_cast<void*>(2000), nullptr);
  grpc_event ev = grpc_completion_queue_next(
      cq, grpc_timeout_seconds_to_deadline(5), nullptr);
  GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
  GPR_ASSERT(ev.tag == reinterpret_cast<void*>(2000));
  GPR_ASSERT(ev.success == 0);
  GPR_ASSERT(grpc_channel_check_connectivity_state(channel, 0) !=
             GRPC_CHANNEL_READY);
}

class KeepaliveThrottlingTest : public ::testing::Test {
 protected:
  // Starts the server and makes sure that the channel is able to get connected.
  grpc_server* ServerStart(const char* addr, grpc_completion_queue* cq) {
    // Set up server channel args to expect pings at an interval of 5 seconds
    // and use a single ping strike
    grpc_arg server_args[] = {
        grpc_channel_arg_integer_create(
            const_cast<char*>(
                GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS),
            5 * 1000),
        grpc_channel_arg_integer_create(
            const_cast<char*>(GRPC_ARG_HTTP2_MAX_PING_STRIKES), 1)};
    grpc_channel_args server_channel_args = {GPR_ARRAY_SIZE(server_args),
                                             server_args};
    // Create server
    grpc_server* server = grpc_server_create(&server_channel_args, nullptr);
    grpc_server_register_completion_queue(server, cq, nullptr);
    GPR_ASSERT(grpc_server_add_insecure_http2_port(server, addr));
    grpc_server_start(server);
    return server;
  }
};

TEST_F(KeepaliveThrottlingTest, KeepaliveThrottlingMultipleChannels) {
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  std::string server_address =
      grpc_core::JoinHostPort("127.0.0.1", grpc_pick_unused_port_or_die());
  grpc_server* server = ServerStart(server_address.c_str(), cq);
  // create two channel with a keepalive ping interval of 1 second.
  grpc_arg client_args[] = {
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA), 0),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_KEEPALIVE_TIME_MS), 1 * 1000),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_HTTP2_BDP_PROBE), 0)};
  grpc_channel_args client_channel_args = {GPR_ARRAY_SIZE(client_args),
                                           client_args};
  grpc_channel* channel = grpc_insecure_channel_create(
      server_address.c_str(), &client_channel_args, nullptr);
  grpc_channel* channel_dup = grpc_insecure_channel_create(
      server_address.c_str(), &client_channel_args, nullptr);
  int expected_keepalive_time_sec = 1;
  // We need 3 GOAWAY frames to throttle the keepalive time from 1 second to 8
  // seconds (> 5sec).
  for (int i = 0; i < 3; i++) {
    gpr_log(GPR_INFO, "Expected keepalive time : %d",
            expected_keepalive_time_sec);
    EXPECT_EQ(PerformWaitingCall(channel, server, cq), GRPC_STATUS_UNAVAILABLE);
    expected_keepalive_time_sec *= 2;
  }
  gpr_log(
      GPR_INFO,
      "Client keepalive time %d should now be in sync with the server settings",
      expected_keepalive_time_sec);
  EXPECT_EQ(PerformWaitingCall(channel, server, cq),
            GRPC_STATUS_DEADLINE_EXCEEDED);
  // Since the subchannel is shared, the second channel should also have
  // keepalive settings in sync with the server.
  gpr_log(GPR_INFO, "Now testing second channel sharing the same subchannel");
  EXPECT_EQ(PerformWaitingCall(channel_dup, server, cq),
            GRPC_STATUS_DEADLINE_EXCEEDED);
  // shutdown and destroy the client and server
  grpc_channel_destroy(channel);
  grpc_channel_destroy(channel_dup);
  ServerShutdownAndDestroy(server, cq);
  grpc_completion_queue_shutdown(cq);
  while (grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                    nullptr)
             .type != GRPC_QUEUE_SHUTDOWN) {
  }
  grpc_completion_queue_destroy(cq);
}

grpc_core::Resolver::Result BuildResolverResult(
    const std::vector<std::string>& addresses) {
  grpc_core::Resolver::Result result;
  for (const auto& address_str : addresses) {
    absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(address_str);
    if (!uri.ok()) {
      gpr_log(GPR_ERROR, "Failed to parse uri. Error: %s",
              uri.status().ToString().c_str());
      GPR_ASSERT(uri.ok());
    }
    grpc_resolved_address address;
    GPR_ASSERT(grpc_parse_uri(*uri, &address));
    result.addresses.emplace_back(address.addr, address.len, nullptr);
  }
  return result;
}

// Tests that when new subchannels are created due to a change in resolved
// addresses, the new subchannels use the updated keepalive time.
TEST_F(KeepaliveThrottlingTest, NewSubchannelsUseUpdatedKeepaliveTime) {
  grpc_core::ExecCtx exec_ctx;
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  std::string server_address1 =
      grpc_core::JoinHostPort("127.0.0.1", grpc_pick_unused_port_or_die());
  std::string server_address2 =
      grpc_core::JoinHostPort("127.0.0.1", grpc_pick_unused_port_or_die());
  grpc_server* server1 = ServerStart(server_address1.c_str(), cq);
  grpc_server* server2 = ServerStart(server_address2.c_str(), cq);
  // create a single channel with multiple subchannels with a keepalive ping
  // interval of 1 second. To get finer control on subchannel connection times,
  // we are using pick_first instead of round_robin and using the fake resolver
  // response generator to switch between the two.
  auto response_generator =
      grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
  grpc_arg client_args[] = {
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA), 0),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS), 0),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_KEEPALIVE_TIME_MS), 1 * 1000),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_HTTP2_BDP_PROBE), 0),
      grpc_core::FakeResolverResponseGenerator::MakeChannelArg(
          response_generator.get())};
  grpc_channel_args client_channel_args = {GPR_ARRAY_SIZE(client_args),
                                           client_args};
  grpc_channel* channel =
      grpc_insecure_channel_create("fake:///", &client_channel_args, nullptr);
  // For a single subchannel 3 GOAWAYs would be sufficient to increase the
  // keepalive time from 1 second to beyond 5 seconds. Even though we are
  // alternating between two subchannels, 3 GOAWAYs should still be enough since
  // the channel should start all new transports with the new keepalive value
  // (even those from a different subchannel).
  int expected_keepalive_time_sec = 1;
  for (int i = 0; i < 3; i++) {
    gpr_log(GPR_INFO, "Expected keepalive time : %d",
            expected_keepalive_time_sec);
    response_generator->SetResponse(BuildResolverResult({absl::StrCat(
        "ipv4:", i % 2 == 0 ? server_address1 : server_address2)}));
    // ExecCtx::Flush() might not be enough to make sure that the resolver
    // result has been propagated, so sleep for a bit.
    grpc_core::ExecCtx::Get()->Flush();
    gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));
    EXPECT_EQ(PerformWaitingCall(channel, i % 2 == 0 ? server1 : server2, cq),
              GRPC_STATUS_UNAVAILABLE);
    expected_keepalive_time_sec *= 2;
  }
  gpr_log(
      GPR_INFO,
      "Client keepalive time %d should now be in sync with the server settings",
      expected_keepalive_time_sec);
  response_generator->SetResponse(
      BuildResolverResult({absl::StrCat("ipv4:", server_address2)}));
  grpc_core::ExecCtx::Get()->Flush();
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));
  EXPECT_EQ(PerformWaitingCall(channel, server2, cq),
            GRPC_STATUS_DEADLINE_EXCEEDED);
  // shutdown and destroy the client and server
  grpc_channel_destroy(channel);
  ServerShutdownAndDestroy(server1, cq);
  ServerShutdownAndDestroy(server2, cq);
  grpc_completion_queue_shutdown(cq);
  while (grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                    nullptr)
             .type != GRPC_QUEUE_SHUTDOWN) {
  }
  grpc_completion_queue_destroy(cq);
}

// Tests that when a channel has multiple subchannels and receives a GOAWAY with
// "too_many_pings" on one of them, all subchannels start any new transports
// with an updated keepalive time.
TEST_F(KeepaliveThrottlingTest,
       ExistingSubchannelsUseNewKeepaliveTimeWhenReconnecting) {
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  std::string server_address1 =
      grpc_core::JoinHostPort("127.0.0.1", grpc_pick_unused_port_or_die());
  std::string server_address2 =
      grpc_core::JoinHostPort("127.0.0.1", grpc_pick_unused_port_or_die());
  // create a single channel with round robin load balancing policy.
  auto response_generator =
      grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
  grpc_arg client_args[] = {
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA), 0),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS), 0),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_KEEPALIVE_TIME_MS), 1 * 1000),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_HTTP2_BDP_PROBE), 0),
      grpc_core::FakeResolverResponseGenerator::MakeChannelArg(
          response_generator.get())};
  grpc_channel_args client_channel_args = {GPR_ARRAY_SIZE(client_args),
                                           client_args};
  grpc_channel* channel =
      grpc_insecure_channel_create("fake:///", &client_channel_args, nullptr);
  response_generator->SetResponse(
      BuildResolverResult({absl::StrCat("ipv4:", server_address1),
                           absl::StrCat("ipv4:", server_address2)}));
  // For a single subchannel 3 GOAWAYs would be sufficient to increase the
  // keepalive time from 1 second to beyond 5 seconds. Even though we are
  // alternating between two subchannels, 3 GOAWAYs should still be enough since
  // the channel should start all new transports with the new keepalive value
  // (even those from a different subchannel).
  int expected_keepalive_time_sec = 1;
  for (int i = 0; i < 3; i++) {
    gpr_log(GPR_ERROR, "Expected keepalive time : %d",
            expected_keepalive_time_sec);
    grpc_server* server = ServerStart(
        i % 2 == 0 ? server_address1.c_str() : server_address2.c_str(), cq);
    VerifyChannelReady(channel, cq);
    EXPECT_EQ(PerformWaitingCall(channel, server, cq), GRPC_STATUS_UNAVAILABLE);
    ServerShutdownAndDestroy(server, cq);
    VerifyChannelDisconnected(channel, cq);
    expected_keepalive_time_sec *= 2;
  }
  gpr_log(
      GPR_INFO,
      "Client keepalive time %d should now be in sync with the server settings",
      expected_keepalive_time_sec);
  grpc_server* server = ServerStart(server_address1.c_str(), cq);
  VerifyChannelReady(channel, cq);
  EXPECT_EQ(PerformWaitingCall(channel, server, cq),
            GRPC_STATUS_DEADLINE_EXCEEDED);
  ServerShutdownAndDestroy(server, cq);
  // shutdown and destroy the client
  grpc_channel_destroy(channel);
  grpc_completion_queue_shutdown(cq);
  while (grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                    nullptr)
             .type != GRPC_QUEUE_SHUTDOWN) {
  }
  grpc_completion_queue_destroy(cq);
}

// Perform a simple RPC where the client makes a request expecting a response
// with payload.
void PerformCallWithResponsePayload(grpc_channel* channel, grpc_server* server,
                                    grpc_completion_queue* cq) {
  grpc_slice response_payload_slice = grpc_slice_from_static_string("hello");

  grpc_call* c;
  grpc_call* s;
  grpc_byte_buffer* response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  cq_verifier* cqv = cq_verifier_create(cq);
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_byte_buffer* response_payload_recv = nullptr;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;

  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(60);
  c = grpc_channel_create_call(channel, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               deadline, nullptr);
  GPR_ASSERT(c);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &response_payload_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(1),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error = grpc_server_request_call(server, &s, &call_details,
                                   &request_metadata_recv, cq, cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  cq_verify(cqv);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(102),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), 1);
  cq_verify(cqv);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = response_payload;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_OK;
  grpc_slice status_details = grpc_slice_from_static_string("xyz");
  op->data.send_status_from_server.status_details = &status_details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(103),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(103), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_OK);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/foo"));
  GPR_ASSERT(was_cancelled == 0);
  GPR_ASSERT(
      byte_buffer_eq_slice(response_payload_recv, response_payload_slice));

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(c);
  grpc_call_unref(s);

  cq_verifier_destroy(cqv);

  grpc_byte_buffer_destroy(response_payload);
  grpc_byte_buffer_destroy(response_payload_recv);
}

TEST(TooManyPings, BdpPingNotSentWithoutReceiveSideActivity) {
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  // create the server
  std::string server_address =
      grpc_core::JoinHostPort("localhost", grpc_pick_unused_port_or_die());
  grpc_arg server_args[] = {
      grpc_channel_arg_integer_create(
          const_cast<char*>(
              GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS),
          60 * 1000),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_HTTP2_MAX_PING_STRIKES), 1)};
  grpc_channel_args server_channel_args = {GPR_ARRAY_SIZE(server_args),
                                           server_args};
  grpc_server* server = grpc_server_create(&server_channel_args, nullptr);
  grpc_server_register_completion_queue(server, cq, nullptr);
  GPR_ASSERT(
      grpc_server_add_insecure_http2_port(server, server_address.c_str()));
  grpc_server_start(server);
  // create the channel (bdp pings are enabled by default)
  grpc_arg client_args[] = {
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA), 0),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS), 1)};
  grpc_channel_args client_channel_args = {GPR_ARRAY_SIZE(client_args),
                                           client_args};
  grpc_channel* channel = grpc_insecure_channel_create(
      server_address.c_str(), &client_channel_args, nullptr);
  VerifyChannelReady(channel, cq);
  EXPECT_EQ(TransportCounter::count(), 2 /* one each for server and client */);
  cq_verifier* cqv = cq_verifier_create(cq);
  // Channel should be able to send two pings without disconnect if there was no
  // BDP sent.
  grpc_channel_ping(channel, cq, tag(1), nullptr);
  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv, 5);
  // Second ping
  grpc_channel_ping(channel, cq, tag(2), nullptr);
  CQ_EXPECT_COMPLETION(cqv, tag(2), 1);
  cq_verify(cqv, 5);
  ASSERT_EQ(grpc_channel_check_connectivity_state(channel, 0),
            GRPC_CHANNEL_READY);
  PerformCallWithResponsePayload(channel, server, cq);
  // Wait a bit to make sure that the BDP ping goes out.
  cq_verify_empty_timeout(cqv, 1);
  // The call with a response payload should have triggered a BDP ping.
  // Send two more pings to verify. The second ping should cause a disconnect.
  // If BDP was not sent, the second ping would not cause a disconnect.
  grpc_channel_ping(channel, cq, tag(3), nullptr);
  CQ_EXPECT_COMPLETION(cqv, tag(3), 1);
  cq_verify(cqv, 5);
  // Second ping
  grpc_channel_ping(channel, cq, tag(4), nullptr);
  CQ_EXPECT_COMPLETION(cqv, tag(4), 1);
  cq_verify(cqv, 5);
  // Make sure that the transports have been destroyed
  VerifyChannelDisconnected(channel, cq);
  TransportCounter::WaitForTransportsToBeDestroyed();
  cq_verifier_destroy(cqv);
  // shutdown and destroy the client and server
  ServerShutdownAndDestroy(server, cq);
  grpc_channel_destroy(channel);
  grpc_completion_queue_shutdown(cq);
  while (grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                    nullptr)
             .type != GRPC_QUEUE_SHUTDOWN) {
  }
  grpc_completion_queue_destroy(cq);
}

TEST(TooManyPings, TransportsGetCleanedUpOnDisconnect) {
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  // create the client and server
  std::string server_address =
      grpc_core::JoinHostPort("localhost", grpc_pick_unused_port_or_die());
  grpc_arg server_args[] = {
      grpc_channel_arg_integer_create(
          const_cast<char*>(
              GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS),
          60 * 1000),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_HTTP2_MAX_PING_STRIKES), 1)};
  grpc_channel_args server_channel_args = {GPR_ARRAY_SIZE(server_args),
                                           server_args};
  grpc_server* server = grpc_server_create(&server_channel_args, nullptr);
  grpc_server_register_completion_queue(server, cq, nullptr);
  GPR_ASSERT(
      grpc_server_add_insecure_http2_port(server, server_address.c_str()));
  grpc_server_start(server);
  grpc_arg client_args[] = {
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA), 0),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS), 1)};
  grpc_channel_args client_channel_args = {GPR_ARRAY_SIZE(client_args),
                                           client_args};
  grpc_channel* channel = grpc_insecure_channel_create(
      server_address.c_str(), &client_channel_args, nullptr);
  VerifyChannelReady(channel, cq);
  EXPECT_EQ(TransportCounter::count(), 2 /* one each for server and client */);
  cq_verifier* cqv = cq_verifier_create(cq);
  // First ping
  grpc_channel_ping(channel, cq, tag(1), nullptr);
  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv, 5);
  // Second ping
  grpc_channel_ping(channel, cq, tag(2), nullptr);
  CQ_EXPECT_COMPLETION(cqv, tag(2), 1);
  cq_verify(cqv, 5);
  // Third ping caused disconnect
  grpc_channel_ping(channel, cq, tag(2), nullptr);
  CQ_EXPECT_COMPLETION(cqv, tag(2), 1);
  cq_verify(cqv, 5);
  // Make sure that the transports have been destroyed
  VerifyChannelDisconnected(channel, cq);
  TransportCounter::WaitForTransportsToBeDestroyed();
  cq_verifier_destroy(cqv);
  // shutdown and destroy the client and server
  ServerShutdownAndDestroy(server, cq);
  grpc_channel_destroy(channel);
  grpc_completion_queue_shutdown(cq);
  while (grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                    nullptr)
             .type != GRPC_QUEUE_SHUTDOWN) {
  }
  grpc_completion_queue_destroy(cq);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_core::TestOnlySetGlobalHttp2TransportInitCallback(
      TransportCounter::CounterInitCallback);
  grpc_core::TestOnlySetGlobalHttp2TransportDestructCallback(
      TransportCounter::CounterDestructCallback);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
