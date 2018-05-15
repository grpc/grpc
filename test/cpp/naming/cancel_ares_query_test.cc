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

#include "test/core/end2end/end2end_tests.h"

#include <stdio.h>
#include <string.h>

#include <gflags/gflags.h>
#include <gmock/gmock.h>

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include "include/grpc/support/string_util.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/thd.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/cmdline.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

// TODO: pull in different headers when enabling this
// test on windows. Also set BAD_SOCKET_RETURN_VAL
// to INVALID_SOCKET on windows.
#include "src/core/lib/iomgr/sockaddr_posix.h"
#define BAD_SOCKET_RETURN_VAL -1

namespace {

gpr_timespec FiveSecondsFromNow(void) {
  return grpc_timeout_seconds_to_deadline(5);
}

void DrainCq(grpc_completion_queue* cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, FiveSecondsFromNow(), nullptr);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

void EndTest(grpc_channel* client, grpc_completion_queue* cq) {
  grpc_channel_destroy(client);
  grpc_completion_queue_shutdown(cq);
  DrainCq(cq);
  grpc_completion_queue_destroy(cq);
}

class FakeNonResponsiveDNSServer {
 public:
  FakeNonResponsiveDNSServer(int port) {
    socket_ = socket(AF_INET6, SOCK_DGRAM, 0);
    if (socket_ == BAD_SOCKET_RETURN_VAL) {
      gpr_log(GPR_DEBUG, "Failed to create UDP ipv6 socket");
      abort();
    }
    sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    ((char*)&addr.sin6_addr)[15] = 1;
    if (bind(socket_, (const sockaddr*)&addr, sizeof(addr)) != 0) {
      gpr_log(GPR_DEBUG, "Failed to bind UDP ipv6 socket to [::1]:%d", port);
      abort();
    }
  }
  ~FakeNonResponsiveDNSServer() { close(socket_); }
  int socket_;
};

class CallWithNonResponsiveDNSServer {
 public:
  CallWithNonResponsiveDNSServer(int fake_dns_port, gpr_timespec deadline) {
    char* client_target = nullptr;
    GPR_ASSERT(gpr_asprintf(
        &client_target,
        "dns://[::1]:%d/dont-care-since-wont-be-resolved.test.com:1234",
        fake_dns_port));
    client_ = grpc_insecure_channel_create(client_target,
                                           /* client_args */ nullptr, nullptr);
    gpr_free(client_target);
    cq_ = grpc_completion_queue_create_for_next(nullptr);
    cqv_ = cq_verifier_create(cq_);
    call_ = grpc_channel_create_call(client_, nullptr, GRPC_PROPAGATE_DEFAULTS,
                                     cq_, grpc_slice_from_static_string("/foo"),
                                     nullptr, deadline, nullptr);
    GPR_ASSERT(call_);
    grpc_metadata_array_init(&initial_metadata_recv_);
    grpc_metadata_array_init(&trailing_metadata_recv_);
    grpc_metadata_array_init(&request_metadata_recv_);
    grpc_call_details_init(&call_details_);
    // Set ops for client request
    memset(ops_base_, 0, sizeof(ops_base_));
    grpc_op* op = ops_base_;
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
    op->data.recv_initial_metadata.recv_initial_metadata =
        &initial_metadata_recv_;
    op->flags = 0;
    op->reserved = nullptr;
    op++;
    op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv_;
    op->data.recv_status_on_client.status = &status_;
    op->data.recv_status_on_client.status_details = &details_;
    op->data.recv_status_on_client.error_string = &error_string_;
    op->flags = 0;
    op->reserved = nullptr;
    op++;
    // Start a call
    grpc_call_error error = grpc_call_start_batch(
        call_, ops_base_, static_cast<size_t>(op - ops_base_), this, nullptr);
    EXPECT_EQ(GRPC_CALL_OK, error);
  }

  ~CallWithNonResponsiveDNSServer() {
    grpc_slice_unref(details_);
    gpr_free((void*)error_string_);
    grpc_metadata_array_destroy(&initial_metadata_recv_);
    grpc_metadata_array_destroy(&trailing_metadata_recv_);
    grpc_metadata_array_destroy(&request_metadata_recv_);
    grpc_call_details_destroy(&call_details_);
    grpc_call_unref(call_);
    cq_verifier_destroy(cqv_);
    EndTest(client_, cq_);
  }

  void CqVerify() {
    CQ_EXPECT_COMPLETION(cqv_, this, 1);
    cq_verify(cqv_);
  }

  grpc_status_code GetStatus() { return status_; }

  grpc_call* GetCall() { return call_; }

  grpc_channel* client_;
  grpc_call* call_;
  grpc_completion_queue* cq_;
  cq_verifier* cqv_;
  grpc_op ops_base_[6];
  grpc_metadata_array initial_metadata_recv_;
  grpc_metadata_array trailing_metadata_recv_;
  grpc_metadata_array request_metadata_recv_;
  grpc_call_details call_details_;
  grpc_status_code status_;
  const char* error_string_;
  grpc_slice details_;
};

TEST(CancelDuringAresQuery,
     TestHitDeadlineAndDestroyChannelDuringAresResolutionIsGraceful) {
  int fake_dns_port = grpc_pick_unused_port_or_die();
  FakeNonResponsiveDNSServer fake_dns_server(fake_dns_port);
  CallWithNonResponsiveDNSServer test_call(
      fake_dns_port, grpc_timeout_milliseconds_to_deadline(10));
  test_call.CqVerify();
  EXPECT_EQ(test_call.GetStatus(), GRPC_STATUS_DEADLINE_EXCEEDED);
}

}  // namespace

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  gpr_setenv("GRPC_DNS_RESOLVER", "ares");
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
