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

#include <stdlib.h>
#include <string.h>

#include <functional>
#include <set>
#include <thread>

#include <gmock/gmock.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/types/optional.h"

#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/security/credentials/alts/alts_credentials.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/alts/alts_security_connector.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/uri/uri_parser.h"

#include "test/core/util/memory_counters.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#include "test/core/end2end/cq_verifier.h"

namespace {

const int kNumMessagePingPongsPerCall = 4000;

struct TestCall {
  explicit TestCall(grpc_channel* channel, grpc_call* call,
                    grpc_completion_queue* cq)
      : channel(channel), call(call), cq(cq) {}

  TestCall(const TestCall& other) = delete;
  TestCall& operator=(const TestCall& other) = delete;

  ~TestCall() {
    grpc_call_cancel(call, nullptr);
    grpc_call_unref(call);
    grpc_channel_destroy(channel);
    grpc_completion_queue_shutdown(cq);
    while (grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                      nullptr)
               .type != GRPC_QUEUE_SHUTDOWN) {
    }
    grpc_completion_queue_destroy(cq);
  }

  grpc_channel* channel;
  grpc_call* call;
  grpc_completion_queue* cq;
  absl::optional<grpc_status_code>
      status;  // filled in when the call is finished
};

void StartCall(TestCall* test_call) {
  grpc_op ops[6];
  grpc_op* op;
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = GRPC_INITIAL_METADATA_WAIT_FOR_READY;
  op->reserved = nullptr;
  op++;
  void* tag = test_call;
  grpc_call_error error = grpc_call_start_batch(
      test_call->call, ops, static_cast<size_t>(op - ops), tag, nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  cq_verifier* cqv = cq_verifier_create(test_call->cq);
  CQ_EXPECT_COMPLETION(cqv, tag, 1);
  cq_verify(cqv);
  cq_verifier_destroy(cqv);
}

void SendMessage(grpc_call* call, grpc_completion_queue* cq) {
  grpc_slice request_payload_slice = grpc_slice_from_copied_string("a");
  grpc_byte_buffer* request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_op ops[6];
  grpc_op* op;
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload;
  op->reserved = nullptr;
  op++;
  void* tag = call;
  grpc_call_error error = grpc_call_start_batch(
      call, ops, static_cast<size_t>(op - ops), tag, nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  cq_verifier* cqv = cq_verifier_create(cq);
  CQ_EXPECT_COMPLETION(cqv, tag, 1);
  cq_verify(cqv);
  cq_verifier_destroy(cqv);
  grpc_byte_buffer_destroy(request_payload);
}

void ReceiveMessage(grpc_call* call, grpc_completion_queue* cq) {
  grpc_byte_buffer* request_payload = nullptr;
  grpc_op ops[6];
  grpc_op* op;
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &request_payload;
  op->reserved = nullptr;
  op++;
  void* tag = call;
  grpc_call_error error = grpc_call_start_batch(
      call, ops, static_cast<size_t>(op - ops), tag, nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  cq_verifier* cqv = cq_verifier_create(cq);
  CQ_EXPECT_COMPLETION(cqv, tag, 1);
  cq_verify(cqv);
  cq_verifier_destroy(cqv);
  grpc_byte_buffer_destroy(request_payload);
}

void ReceiveInitialMetadata(TestCall* test_call, gpr_timespec deadline) {
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_op ops[6];
  grpc_op* op;
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->reserved = nullptr;
  op++;
  void* tag = test_call;
  grpc_call_error error = grpc_call_start_batch(
      test_call->call, ops, static_cast<size_t>(op - ops), tag, nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  grpc_event event =
      grpc_completion_queue_next(test_call->cq, deadline, nullptr);
  if (event.type != GRPC_OP_COMPLETE || !event.success) {
    gpr_log(GPR_ERROR,
            "Wanted op complete with success, got op type:%d success:%d",
            event.type, event.success);
    GPR_ASSERT(0);
  }
  GPR_ASSERT(event.tag == tag);
  grpc_metadata_array_destroy(&initial_metadata_recv);
}

void FinishCall(TestCall* test_call) {
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array trailing_metadata_recv;
  grpc_status_code status;
  grpc_slice details;
  grpc_metadata_array_init(&trailing_metadata_recv);
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  void* tag = test_call;
  grpc_call_error error = grpc_call_start_batch(
      test_call->call, ops, static_cast<size_t>(op - ops), tag, nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  grpc_event event = grpc_completion_queue_next(
      test_call->cq, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
  GPR_ASSERT(event.type == GRPC_OP_COMPLETE);
  GPR_ASSERT(event.success);
  GPR_ASSERT(event.tag == tag);
  test_call->status = status;
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_slice_unref(details);
}

class TestServer {
 public:
  explicit TestServer() {
    cq_ = grpc_completion_queue_create_for_next(nullptr);
    server_ = grpc_server_create(nullptr, nullptr);
    address_ =
        grpc_core::JoinHostPort("127.0.0.1", grpc_pick_unused_port_or_die());
    grpc_server_register_completion_queue(server_, cq_, nullptr);
    GPR_ASSERT(grpc_server_add_insecure_http2_port(server_, address_.c_str()));
    grpc_server_start(server_);
    thread_ = std::thread(std::bind(&TestServer::AcceptThread, this));
  }

  ~TestServer() {
    thread_.join();
    void* shutdown_and_notify_tag = this;
    grpc_server_shutdown_and_notify(server_, cq_, shutdown_and_notify_tag);
    grpc_event event = grpc_completion_queue_next(
        cq_, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    GPR_ASSERT(event.type == GRPC_OP_COMPLETE);
    GPR_ASSERT(event.tag == shutdown_and_notify_tag);
    GPR_ASSERT(event.success);
    grpc_server_destroy(server_);
    grpc_completion_queue_shutdown(cq_);
    while (grpc_completion_queue_next(cq_, gpr_inf_future(GPR_CLOCK_REALTIME),
                                      nullptr)
               .type != GRPC_QUEUE_SHUTDOWN) {
    }
    grpc_completion_queue_destroy(cq_);
  }

  std::string address() const { return address_; }

 private:
  void AcceptThread() {
    grpc_call_details call_details;
    grpc_call_details_init(&call_details);
    grpc_metadata_array request_metadata_recv;
    grpc_metadata_array_init(&request_metadata_recv);
    void* tag = &call_details;
    grpc_call* call;
    grpc_call_error error = grpc_server_request_call(
        server_, &call, &call_details, &request_metadata_recv, cq_, cq_, tag);
    GPR_ASSERT(error == GRPC_CALL_OK);
    grpc_event event = grpc_completion_queue_next(
        cq_, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    GPR_ASSERT(event.type == GRPC_OP_COMPLETE);
    GPR_ASSERT(event.success);
    GPR_ASSERT(event.tag == tag);
    grpc_op ops[6];
    grpc_op* op;
    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    op->data.send_initial_metadata.count = 0;
    op->reserved = nullptr;
    op++;
    error = grpc_call_start_batch(call, ops, static_cast<size_t>(op - ops), tag,
                                  nullptr);
    GPR_ASSERT(GRPC_CALL_OK == error);
    event = grpc_completion_queue_next(cq_, gpr_inf_future(GPR_CLOCK_REALTIME),
                                       nullptr);
    GPR_ASSERT(event.type == GRPC_OP_COMPLETE);
    GPR_ASSERT(event.success);
    GPR_ASSERT(event.tag == tag);
    for (int i = 0; i < kNumMessagePingPongsPerCall; i++) {
      ReceiveMessage(call, cq_);
      SendMessage(call, cq_);
    }
    grpc_call_cancel_with_status(call, GRPC_STATUS_PERMISSION_DENIED,
                                 "test status", nullptr);
    grpc_metadata_array_destroy(&request_metadata_recv);
    grpc_call_details_destroy(&call_details);
    grpc_call_unref(call);
  }

  grpc_server* server_;
  grpc_completion_queue* cq_;
  std::string address_;
  std::thread thread_;
};

grpc_core::Resolver::Result BuildResolverResponse(
    const std::vector<std::string>& addresses) {
  grpc_core::Resolver::Result result;
  for (const auto& address_str : addresses) {
    absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(address_str);
    if (!uri.ok()) {
      gpr_log(GPR_ERROR, "Failed to parse. Error: %s",
              uri.status().ToString().c_str());
      GPR_ASSERT(uri.ok());
    }
    grpc_resolved_address address;
    GPR_ASSERT(grpc_parse_uri(*uri, &address));
    result.addresses.emplace_back(address.addr, address.len, nullptr);
  }
  return result;
}

// Perform a simple RPC where the server cancels the request with
// grpc_call_cancel_with_status
TEST(Pollers, TestReadabilityNotificationsDontGetStrandedOnOneCq) {
  gpr_log(GPR_DEBUG, "test thread");
  /* 64 is a somewhat arbitary number, the important thing is that it
   * exceeds the value of MAX_EPOLL_EVENTS_HANDLED_EACH_POLL_CALL (16), which
   * is enough to repro a bug at time of writing. */
  const int kNumCalls = 64;
  size_t ping_pong_round = 0;
  size_t ping_pongs_done = 0;
  grpc_core::Mutex ping_pong_round_mu;
  grpc_core::CondVar ping_pong_round_cv;
  const std::string kSharedUnconnectableAddress =
      grpc_core::JoinHostPort("127.0.0.1", grpc_pick_unused_port_or_die());
  gpr_log(GPR_DEBUG, "created unconnectable address:%s",
          kSharedUnconnectableAddress.c_str());
  std::vector<std::thread> threads;
  threads.reserve(kNumCalls);
  std::vector<std::unique_ptr<TestServer>> test_servers;
  // Instantiate servers inline here, so that we get port allocation out of the
  // way and don't depend on it during the actual test. It can sometimes take
  // time to allocate kNumCalls ports from the port server, and we don't want to
  // hit test timeouts because of that.
  test_servers.reserve(kNumCalls);
  for (int i = 0; i < kNumCalls; i++) {
    test_servers.push_back(absl::make_unique<TestServer>());
  }
  for (int i = 0; i < kNumCalls; i++) {
    auto test_server = test_servers[i].get();
    threads.push_back(std::thread([kSharedUnconnectableAddress,
                                   &ping_pong_round, &ping_pongs_done,
                                   &ping_pong_round_mu, &ping_pong_round_cv,
                                   test_server]() {
      gpr_log(GPR_DEBUG, "using test_server with address:%s",
              test_server->address().c_str());
      std::vector<grpc_arg> args;
      grpc_arg service_config_arg;
      service_config_arg.type = GRPC_ARG_STRING;
      service_config_arg.key = const_cast<char*>(GRPC_ARG_SERVICE_CONFIG);
      service_config_arg.value.string =
          const_cast<char*>("{\"loadBalancingConfig\":[{\"round_robin\":{}}]}");
      args.push_back(service_config_arg);
      auto fake_resolver_response_generator =
          grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
      {
        grpc_core::ExecCtx exec_ctx;
        fake_resolver_response_generator->SetResponse(BuildResolverResponse(
            {absl::StrCat("ipv4:", kSharedUnconnectableAddress),
             absl::StrCat("ipv4:", test_server->address())}));
      }
      args.push_back(grpc_core::FakeResolverResponseGenerator::MakeChannelArg(
          fake_resolver_response_generator.get()));
      grpc_channel_args* channel_args =
          grpc_channel_args_copy_and_add(nullptr, args.data(), args.size());
      grpc_channel* channel = grpc_insecure_channel_create(
          "fake:///test.server.com", channel_args, nullptr);
      grpc_channel_args_destroy(channel_args);
      grpc_completion_queue* cq =
          grpc_completion_queue_create_for_next(nullptr);
      grpc_call* call = grpc_channel_create_call(
          channel, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
          grpc_slice_from_static_string("/foo"), nullptr,
          gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
      auto test_call = absl::make_unique<TestCall>(channel, call, cq);
      // Start a call, and ensure that round_robin load balancing is configured
      StartCall(test_call.get());
      // Make sure the test is doing what it's meant to be doing
      grpc_channel_info channel_info;
      memset(&channel_info, 0, sizeof(channel_info));
      char* lb_policy_name = nullptr;
      channel_info.lb_policy_name = &lb_policy_name;
      grpc_channel_get_info(channel, &channel_info);
      EXPECT_EQ(std::string(lb_policy_name), "round_robin")
          << "not using round robin; this test has a low chance of hitting the "
             "bug that it's meant to try to hit";
      gpr_free(lb_policy_name);
      // Receive initial metadata
      gpr_log(GPR_DEBUG,
              "now receive initial metadata on call with server address:%s",
              test_server->address().c_str());
      ReceiveInitialMetadata(test_call.get(),
                             grpc_timeout_seconds_to_deadline(30));
      for (int i = 1; i <= kNumMessagePingPongsPerCall; i++) {
        {
          grpc_core::MutexLock lock(&ping_pong_round_mu);
          ping_pong_round_cv.SignalAll();
          while (int(ping_pong_round) != i) {
            ping_pong_round_cv.Wait(&ping_pong_round_mu);
          }
        }
        SendMessage(test_call->call, test_call->cq);
        ReceiveMessage(test_call->call, test_call->cq);
        {
          grpc_core::MutexLock lock(&ping_pong_round_mu);
          ping_pongs_done++;
          ping_pong_round_cv.SignalAll();
        }
      }
      gpr_log(GPR_DEBUG, "now receive status on call with server address:%s",
              test_server->address().c_str());
      FinishCall(test_call.get());
      GPR_ASSERT(test_call->status.has_value());
      GPR_ASSERT(test_call->status.value() == GRPC_STATUS_PERMISSION_DENIED);
      {
        grpc_core::ExecCtx exec_ctx;
        fake_resolver_response_generator.reset();
      }
    }));
  }
  for (size_t i = 1; i <= kNumMessagePingPongsPerCall; i++) {
    {
      grpc_core::MutexLock lock(&ping_pong_round_mu);
      while (ping_pongs_done < ping_pong_round * kNumCalls) {
        ping_pong_round_cv.Wait(&ping_pong_round_mu);
      }
      ping_pong_round++;
      ping_pong_round_cv.SignalAll();
      gpr_log(GPR_DEBUG, "initiate ping pong round: %ld", ping_pong_round);
    }
  }
  for (auto& thread : threads) {
    thread.join();
  }
  gpr_log(GPR_DEBUG, "All RPCs completed!");
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
