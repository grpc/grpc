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

#include <stdio.h>
#include <string.h>

#include <thread>

#include <gflags/gflags.h>
#include <gmock/gmock.h>

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include "include/grpc/support/string_util.h"
#include "src/core/ext/filters/client_channel/resolver.h"
#include "src/core/ext/filters/client_channel/resolver/dns/dns_resolver_selection.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/cmdline.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/naming/dns_test_util.h"

#ifdef GPR_WINDOWS
#include "src/core/lib/iomgr/sockaddr_windows.h"
#include "src/core/lib/iomgr/socket_windows.h"
#define BAD_SOCKET_RETURN_VAL INVALID_SOCKET
#else
#include "src/core/lib/iomgr/sockaddr_posix.h"
#define BAD_SOCKET_RETURN_VAL -1
#endif

namespace {

void* Tag(intptr_t t) { return (void*)t; }

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

struct ArgsStruct {
  gpr_atm done_atm;
  gpr_mu* mu;
  grpc_pollset* pollset;
  grpc_pollset_set* pollset_set;
  grpc_combiner* lock;
  grpc_channel_args* channel_args;
};

void ArgsInit(ArgsStruct* args) {
  args->pollset = (grpc_pollset*)gpr_zalloc(grpc_pollset_size());
  grpc_pollset_init(args->pollset, &args->mu);
  args->pollset_set = grpc_pollset_set_create();
  grpc_pollset_set_add_pollset(args->pollset_set, args->pollset);
  args->lock = grpc_combiner_create();
  gpr_atm_rel_store(&args->done_atm, 0);
  args->channel_args = nullptr;
}

void DoNothing(void* arg, grpc_error* error) {}

void ArgsFinish(ArgsStruct* args) {
  grpc_pollset_set_del_pollset(args->pollset_set, args->pollset);
  grpc_pollset_set_destroy(args->pollset_set);
  grpc_closure DoNothing_cb;
  GRPC_CLOSURE_INIT(&DoNothing_cb, DoNothing, nullptr,
                    grpc_schedule_on_exec_ctx);
  grpc_pollset_shutdown(args->pollset, &DoNothing_cb);
  // exec_ctx needs to be flushed before calling grpc_pollset_destroy()
  grpc_channel_args_destroy(args->channel_args);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_pollset_destroy(args->pollset);
  gpr_free(args->pollset);
  GRPC_COMBINER_UNREF(args->lock, nullptr);
}

void PollPollsetUntilRequestDone(ArgsStruct* args) {
  while (true) {
    bool done = gpr_atm_acq_load(&args->done_atm) != 0;
    if (done) {
      break;
    }
    grpc_pollset_worker* worker = nullptr;
    grpc_core::ExecCtx exec_ctx;
    gpr_mu_lock(args->mu);
    GRPC_LOG_IF_ERROR(
        "pollset_work",
        grpc_pollset_work(args->pollset, &worker,
                          grpc_timespec_to_millis_round_up(
                              gpr_inf_future(GPR_CLOCK_REALTIME))));
    gpr_mu_unlock(args->mu);
  }
}

class AssertFailureResultHandler : public grpc_core::Resolver::ResultHandler {
 public:
  explicit AssertFailureResultHandler(ArgsStruct* args) : args_(args) {}

  ~AssertFailureResultHandler() override {
    gpr_atm_rel_store(&args_->done_atm, 1);
    gpr_mu_lock(args_->mu);
    GRPC_LOG_IF_ERROR("pollset_kick",
                      grpc_pollset_kick(args_->pollset, nullptr));
    gpr_mu_unlock(args_->mu);
  }

  void ReturnResult(grpc_core::Resolver::Result result) override {
    GPR_ASSERT(false);
  }

  void ReturnError(grpc_error* error) override { GPR_ASSERT(false); }

 private:
  ArgsStruct* args_;
};

void TestCancelActiveDNSQuery(ArgsStruct* args) {
  int fake_dns_port = grpc_pick_unused_port_or_die();
  grpc::testing::FakeNonResponsiveDNSServer fake_dns_server(fake_dns_port);
  char* client_target;
  GPR_ASSERT(gpr_asprintf(
      &client_target,
      "dns://[::1]:%d/dont-care-since-wont-be-resolved.test.com:1234",
      fake_dns_port));
  // create resolver and resolve
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      grpc_core::ResolverRegistry::CreateResolver(
          client_target, nullptr, args->pollset_set, args->lock,
          grpc_core::UniquePtr<grpc_core::Resolver::ResultHandler>(
              grpc_core::New<AssertFailureResultHandler>(args)));
  gpr_free(client_target);
  resolver->StartLocked();
  // Without resetting and causing resolver shutdown, the
  // PollPollsetUntilRequestDone call should never finish.
  resolver.reset();
  grpc_core::ExecCtx::Get()->Flush();
  PollPollsetUntilRequestDone(args);
  ArgsFinish(args);
}

class CancelDuringAresQuery : public ::testing::Test {
 protected:
  static void SetUpTestCase() {
    GPR_GLOBAL_CONFIG_SET(grpc_dns_resolver, "ares");
    // Sanity check the time that it takes to run the test
    // including the teardown time (the teardown
    // part of the test involves cancelling the DNS query,
    // which is the main point of interest for this test).
    overall_deadline = grpc_timeout_seconds_to_deadline(4);
    grpc_init();
  }

  static void TearDownTestCase() {
    grpc_shutdown();
    if (gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), overall_deadline) > 0) {
      gpr_log(GPR_ERROR, "Test took too long");
      abort();
    }
  }

 private:
  static gpr_timespec overall_deadline;
};
gpr_timespec CancelDuringAresQuery::overall_deadline;

TEST_F(CancelDuringAresQuery, TestCancelActiveDNSQuery) {
  grpc_core::ExecCtx exec_ctx;
  ArgsStruct args;
  ArgsInit(&args);
  TestCancelActiveDNSQuery(&args);
}

#ifdef GPR_WINDOWS

void MaybePollArbitraryPollsetTwice() {
  grpc_pollset* pollset = (grpc_pollset*)gpr_zalloc(grpc_pollset_size());
  gpr_mu* mu;
  grpc_pollset_init(pollset, &mu);
  grpc_pollset_worker* worker = nullptr;
  // Make a zero timeout poll
  gpr_mu_lock(mu);
  GRPC_LOG_IF_ERROR(
      "pollset_work",
      grpc_pollset_work(pollset, &worker, grpc_core::ExecCtx::Get()->Now()));
  gpr_mu_unlock(mu);
  grpc_core::ExecCtx::Get()->Flush();
  // Make a second zero-timeout poll (in case the first one
  // short-circuited by picking up a previous "kick")
  gpr_mu_lock(mu);
  GRPC_LOG_IF_ERROR(
      "pollset_work",
      grpc_pollset_work(pollset, &worker, grpc_core::ExecCtx::Get()->Now()));
  gpr_mu_unlock(mu);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_pollset_destroy(pollset);
  gpr_free(pollset);
}

#else

void MaybePollArbitraryPollsetTwice() {}

#endif

TEST_F(CancelDuringAresQuery, TestFdsAreDeletedFromPollsetSet) {
  grpc_core::ExecCtx exec_ctx;
  ArgsStruct args;
  ArgsInit(&args);
  // Add fake_other_pollset_set into the mix to test
  // that we're explicitly deleting fd's from their pollset.
  // If we aren't doing so, then the remaining presence of
  // "fake_other_pollset_set" after the request is done and the resolver
  // pollset set is destroyed should keep the resolver's fd alive and
  // fail the test.
  grpc_pollset_set* fake_other_pollset_set = grpc_pollset_set_create();
  grpc_pollset_set_add_pollset_set(fake_other_pollset_set, args.pollset_set);
  // Note that running the cancellation c-ares test is somewhat irrelevant for
  // this test. This test only cares about what happens to fd's that c-ares
  // opens.
  TestCancelActiveDNSQuery(&args);
  // This test relies on the assumption that cancelling a c-ares query
  // will flush out all callbacks on the current exec ctx, which is true
  // on posix platforms but not on Windows, because fd shutdown on Windows
  // requires a trip through the polling loop to schedule the callback.
  // So we need to do extra polling work on Windows to free things up.
  MaybePollArbitraryPollsetTwice();
  EXPECT_EQ(grpc_iomgr_count_objects_for_testing(), 0u);
  grpc_pollset_set_destroy(fake_other_pollset_set);
}

// Settings for TestCancelDuringActiveQuery test
typedef enum {
  NONE,
  SHORT,
  ZERO,
  TOLERATE_DEADLINE_EXCEEDED_OR_UNAVAILABLE,
} cancellation_test_query_timeout_setting;

void PerformFailingRPC(
    const char* client_target,
    cancellation_test_query_timeout_setting query_timeout_setting) {
  gpr_log(GPR_DEBUG, "TestCancelActiveDNSQuery. query timeout setting: %d",
          query_timeout_setting);
  grpc_channel_args* client_args = nullptr;
  grpc_status_code expected_status_code = GRPC_STATUS_OK;
  if (query_timeout_setting == NONE ||
      query_timeout_setting == TOLERATE_DEADLINE_EXCEEDED_OR_UNAVAILABLE) {
    expected_status_code = GRPC_STATUS_DEADLINE_EXCEEDED;
    client_args = nullptr;
  } else if (query_timeout_setting == SHORT) {
    expected_status_code = GRPC_STATUS_UNAVAILABLE;
    grpc_arg arg;
    arg.type = GRPC_ARG_INTEGER;
    arg.key = const_cast<char*>(GRPC_ARG_DNS_ARES_QUERY_TIMEOUT_MS);
    arg.value.integer =
        1;  // Set this shorter than the call deadline so that it goes off.
    client_args = grpc_channel_args_copy_and_add(nullptr, &arg, 1);
  } else if (query_timeout_setting == ZERO) {
    expected_status_code = GRPC_STATUS_DEADLINE_EXCEEDED;
    grpc_arg arg;
    arg.type = GRPC_ARG_INTEGER;
    arg.key = const_cast<char*>(GRPC_ARG_DNS_ARES_QUERY_TIMEOUT_MS);
    arg.value.integer = 0;  // Set this to zero to disable query timeouts.
    client_args = grpc_channel_args_copy_and_add(nullptr, &arg, 1);
  } else {
    abort();
  }
  grpc_channel* client =
      grpc_insecure_channel_create(client_target, client_args, nullptr);
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  cq_verifier* cqv = cq_verifier_create(cq);
  gpr_timespec deadline = grpc_timeout_milliseconds_to_deadline(10);
  grpc_call* call = grpc_channel_create_call(
      client, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
      grpc_slice_from_static_string("/foo"), nullptr, deadline, nullptr);
  GPR_ASSERT(call);
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details call_details;
  grpc_call_details_init(&call_details);
  grpc_status_code status;
  const char* error_string;
  grpc_slice details;
  // Set ops for client the request
  grpc_op ops_base[6];
  memset(ops_base, 0, sizeof(ops_base));
  grpc_op* op = ops_base;
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
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->data.recv_status_on_client.error_string = &error_string;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  // Run the call and sanity check it failed as expected
  grpc_call_error error = grpc_call_start_batch(
      call, ops_base, static_cast<size_t>(op - ops_base), Tag(1), nullptr);
  EXPECT_EQ(GRPC_CALL_OK, error);
  CQ_EXPECT_COMPLETION(cqv, Tag(1), 1);
  cq_verify(cqv);
  if (query_timeout_setting == TOLERATE_DEADLINE_EXCEEDED_OR_UNAVAILABLE) {
    if (status != GRPC_STATUS_UNAVAILABLE &&
        status != GRPC_STATUS_DEADLINE_EXCEEDED) {
      gpr_log(GPR_ERROR,
              "Expected UNAVAILABLE or DEADLINE_EXCEEDED status. Got: %d",
              status);
      abort();
    }
  } else {
    EXPECT_EQ(status, expected_status_code);
  }
  // Teardown
  grpc_channel_args_destroy(client_args);
  grpc_slice_unref(details);
  gpr_free((void*)error_string);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_call_unref(call);
  cq_verifier_destroy(cqv);
  EndTest(client, cq);
}

void TestCancelDuringActiveQuery(
    cancellation_test_query_timeout_setting query_timeout_setting,
    int num_thds) {
  // Start up fake non responsive DNS server
  int fake_dns_port = grpc_pick_unused_port_or_die();
  grpc::testing::FakeNonResponsiveDNSServer fake_dns_server(fake_dns_port);
  // Create a call that will try to use the fake DNS server
  char* client_target = nullptr;
  GPR_ASSERT(gpr_asprintf(
      &client_target,
      "dns://[::1]:%d/dont-care-since-wont-be-resolved.test.com:1234",
      fake_dns_port));
  std::vector<std::thread> thds;
  thds.reserve(num_thds);
  for (int i = 0; i < num_thds; i++) {
    thds.push_back(
        std::thread(PerformFailingRPC, client_target, query_timeout_setting));
  }
  for (int i = 0; i < thds.size(); i++) {
    thds[i].join();
  }
  gpr_free(client_target);
}

TEST_F(CancelDuringAresQuery,
       TestHitDeadlineAndDestroyChannelDuringAresResolutionIsGraceful) {
  TestCancelDuringActiveQuery(NONE /* don't set query timeouts */,
                              1 /* number of threads */);
}

TEST_F(
    CancelDuringAresQuery,
    TestHitDeadlineAndDestroyChannelDuringAresResolutionWithQueryTimeoutIsGraceful) {
  TestCancelDuringActiveQuery(SHORT /* set short query timeout */,
                              1 /* number of threads */);
}

TEST_F(
    CancelDuringAresQuery,
    TestHitDeadlineAndDestroyChannelDuringAresResolutionWithZeroQueryTimeoutIsGraceful) {
  TestCancelDuringActiveQuery(ZERO /* disable query timeouts */,
                              1 /* number of threads */);
}

TEST_F(
    CancelDuringAresQuery,
    TestConcurrentChannelsHitDeadlineAndDestroyDuringAresResolutionIsGraceful) {
  TestCancelDuringActiveQuery(TOLERATE_DEADLINE_EXCEEDED_OR_UNAVAILABLE,
                              100 /* number of threads */);
}

}  // namespace

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
