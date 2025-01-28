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

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <inttypes.h>
#include <stdlib.h>

#include <memory>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/util/crash.h"
#include "src/core/util/thd.h"
#include "src/core/util/useful.h"
#include "test/core/test_util/test_config.h"

#define LOG_TEST(x) LOG(INFO) << x

static void* create_test_tag(void) {
  static intptr_t i = 0;
  return reinterpret_cast<void*>(++i);
}

// helper for tests to shutdown correctly and tersely
static void shutdown_and_destroy(grpc_completion_queue* cc) {
  grpc_event ev;
  grpc_completion_queue_shutdown(cc);

  switch (grpc_get_cq_completion_type(cc)) {
    case GRPC_CQ_NEXT: {
      ev = grpc_completion_queue_next(cc, gpr_inf_past(GPR_CLOCK_REALTIME),
                                      nullptr);
      break;
    }
    case GRPC_CQ_PLUCK: {
      ev = grpc_completion_queue_pluck(
          cc, create_test_tag(), gpr_inf_past(GPR_CLOCK_REALTIME), nullptr);
      break;
    }
    default: {
      LOG(ERROR) << "Unknown completion type";
      break;
    }
  }

  ASSERT_EQ(ev.type, GRPC_QUEUE_SHUTDOWN);
  grpc_completion_queue_destroy(cc);
}

static void do_nothing_end_completion(void* /*arg*/,
                                      grpc_cq_completion* /*c*/) {}

struct thread_state {
  grpc_completion_queue* cc;
  void* tag;
};

static void pluck_one(void* arg) {
  struct thread_state* state = static_cast<struct thread_state*>(arg);
  grpc_completion_queue_pluck(state->cc, state->tag,
                              gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
}

static void test_too_many_plucks(void) {
  grpc_event ev;
  grpc_completion_queue* cc;
  void* tags[GRPC_MAX_COMPLETION_QUEUE_PLUCKERS];
  grpc_cq_completion completions[GPR_ARRAY_SIZE(tags)];
  grpc_core::Thread threads[GPR_ARRAY_SIZE(tags)];
  struct thread_state thread_states[GPR_ARRAY_SIZE(tags)];
  grpc_core::ExecCtx exec_ctx;
  unsigned i, j;

  LOG_TEST("test_too_many_plucks");

  cc = grpc_completion_queue_create_for_pluck(nullptr);

  for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
    tags[i] = create_test_tag();
    for (j = 0; j < i; j++) {
      ASSERT_NE(tags[i], tags[j]);
    }
    thread_states[i].cc = cc;
    thread_states[i].tag = tags[i];
    threads[i] =
        grpc_core::Thread("grpc_pluck_test", pluck_one, thread_states + i);
    threads[i].Start();
  }

  // wait until all other threads are plucking
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(1000));

  ev = grpc_completion_queue_pluck(cc, create_test_tag(),
                                   gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
  ASSERT_EQ(ev.type, GRPC_QUEUE_TIMEOUT);

  for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
    ASSERT_TRUE(grpc_cq_begin_op(cc, tags[i]));
    grpc_cq_end_op(cc, tags[i], absl::OkStatus(), do_nothing_end_completion,
                   nullptr, &completions[i]);
  }

  for (auto& th : threads) {
    th.Join();
  }

  shutdown_and_destroy(cc);
}

#define TEST_THREAD_EVENTS 10000

typedef struct test_thread_options {
  gpr_event on_started;
  gpr_event* phase1;
  gpr_event on_phase1_done;
  gpr_event* phase2;
  gpr_event on_finished;
  size_t events_triggered;
  int id;
  grpc_completion_queue* cc;
} test_thread_options;

gpr_timespec ten_seconds_time(void) {
  return grpc_timeout_seconds_to_deadline(10);
}

static void free_completion(void* /*arg*/, grpc_cq_completion* completion) {
  gpr_free(completion);
}

static void producer_thread(void* arg) {
  test_thread_options* opt = static_cast<test_thread_options*>(arg);
  int i;

  LOG(INFO) << "producer " << opt->id << " started";
  gpr_event_set(&opt->on_started, reinterpret_cast<void*>(1));
  ASSERT_TRUE(gpr_event_wait(opt->phase1, ten_seconds_time()));

  LOG(INFO) << "producer " << opt->id << " phase 1";
  for (i = 0; i < TEST_THREAD_EVENTS; i++) {
    ASSERT_TRUE(grpc_cq_begin_op(opt->cc, (void*)(intptr_t)1));
  }

  LOG(INFO) << "producer " << opt->id << " phase 1 done";
  gpr_event_set(&opt->on_phase1_done, reinterpret_cast<void*>(1));
  ASSERT_TRUE(gpr_event_wait(opt->phase2, ten_seconds_time()));

  LOG(INFO) << "producer " << opt->id << " phase 2";
  for (i = 0; i < TEST_THREAD_EVENTS; i++) {
    grpc_core::ExecCtx exec_ctx;
    grpc_cq_end_op(opt->cc, reinterpret_cast<void*>(1), absl::OkStatus(),
                   free_completion, nullptr,
                   static_cast<grpc_cq_completion*>(
                       gpr_malloc(sizeof(grpc_cq_completion))));
    opt->events_triggered++;
  }

  LOG(INFO) << "producer " << opt->id << " phase 2 done";
  gpr_event_set(&opt->on_finished, reinterpret_cast<void*>(1));
}

static void consumer_thread(void* arg) {
  test_thread_options* opt = static_cast<test_thread_options*>(arg);
  grpc_event ev;

  LOG(INFO) << "consumer " << opt->id << " started";
  gpr_event_set(&opt->on_started, reinterpret_cast<void*>(1));
  ASSERT_TRUE(gpr_event_wait(opt->phase1, ten_seconds_time()));

  LOG(INFO) << "consumer " << opt->id << " phase 1";

  LOG(INFO) << "consumer " << opt->id << " phase 1 done";
  gpr_event_set(&opt->on_phase1_done, reinterpret_cast<void*>(1));
  ASSERT_TRUE(gpr_event_wait(opt->phase2, ten_seconds_time()));

  LOG(INFO) << "consumer " << opt->id << " phase 2";
  for (;;) {
    ev = grpc_completion_queue_next(
        opt->cc, gpr_inf_future(GPR_CLOCK_MONOTONIC), nullptr);
    switch (ev.type) {
      case GRPC_OP_COMPLETE:
        ASSERT_TRUE(ev.success);
        opt->events_triggered++;
        break;
      case GRPC_QUEUE_SHUTDOWN:
        LOG(INFO) << "consumer " << opt->id << " phase 2 done";
        gpr_event_set(&opt->on_finished, reinterpret_cast<void*>(1));
        return;
      case GRPC_QUEUE_TIMEOUT:
        grpc_core::Crash("Invalid timeout received");
    }
  }
}

static void test_threading(size_t producers, size_t consumers) {
  test_thread_options* options = static_cast<test_thread_options*>(
      gpr_malloc((producers + consumers) * sizeof(test_thread_options)));
  gpr_event phase1 = GPR_EVENT_INIT;
  gpr_event phase2 = GPR_EVENT_INIT;
  grpc_completion_queue* cc = grpc_completion_queue_create_for_next(nullptr);
  size_t i;
  size_t total_consumed = 0;
  static int optid = 101;

  LOG(INFO) << "test_threading: " << producers << " producers, " << consumers
            << " consumers";

  // start all threads: they will wait for phase1
  grpc_core::Thread* threads = static_cast<grpc_core::Thread*>(
      gpr_malloc(sizeof(*threads) * (producers + consumers)));
  for (i = 0; i < producers + consumers; i++) {
    gpr_event_init(&options[i].on_started);
    gpr_event_init(&options[i].on_phase1_done);
    gpr_event_init(&options[i].on_finished);
    options[i].phase1 = &phase1;
    options[i].phase2 = &phase2;
    options[i].events_triggered = 0;
    options[i].cc = cc;
    options[i].id = optid++;

    bool ok;
    threads[i] = grpc_core::Thread(
        i < producers ? "grpc_producer" : "grpc_consumer",
        i < producers ? producer_thread : consumer_thread, options + i, &ok);
    ASSERT_TRUE(ok);
    threads[i].Start();
    gpr_event_wait(&options[i].on_started, ten_seconds_time());
  }

  // start phase1: producers will pre-declare all operations they will
  // complete
  LOG(INFO) << "start phase 1";
  gpr_event_set(&phase1, reinterpret_cast<void*>(1));

  LOG(INFO) << "wait phase 1";
  for (i = 0; i < producers + consumers; i++) {
    ASSERT_TRUE(gpr_event_wait(&options[i].on_phase1_done, ten_seconds_time()));
  }
  LOG(INFO) << "done phase 1";

  // start phase2: operations will complete, and consumers will consume them
  LOG(INFO) << "start phase 2";
  gpr_event_set(&phase2, reinterpret_cast<void*>(1));

  // in parallel, we shutdown the completion channel - all events should still
  // be consumed
  grpc_completion_queue_shutdown(cc);

  // join all threads
  LOG(INFO) << "wait phase 2";
  for (i = 0; i < producers + consumers; i++) {
    ASSERT_TRUE(gpr_event_wait(&options[i].on_finished, ten_seconds_time()));
  }
  LOG(INFO) << "done phase 2";

  // destroy the completion channel
  grpc_completion_queue_destroy(cc);

  for (i = 0; i < producers + consumers; i++) {
    threads[i].Join();
  }
  gpr_free(threads);

  // verify that everything was produced and consumed
  for (i = 0; i < producers + consumers; i++) {
    if (i < producers) {
      ASSERT_EQ(options[i].events_triggered, TEST_THREAD_EVENTS);
    } else {
      total_consumed += options[i].events_triggered;
    }
  }
  ASSERT_EQ(total_consumed, producers * TEST_THREAD_EVENTS);

  gpr_free(options);
}

TEST(CompletionQueueThreadingTest, MainTest) {
  grpc_init();
  test_too_many_plucks();
  test_threading(1, 1);
  test_threading(1, 10);
  test_threading(10, 1);
  test_threading(10, 10);
  grpc_shutdown();
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
