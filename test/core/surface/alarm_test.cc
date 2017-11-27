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

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "test/core/util/test_config.h"

#define LOG_TEST(x) gpr_log(GPR_INFO, "%s", x)

static void* create_test_tag(void) {
  static intptr_t i = 0;
  return (void*)(++i);
}

/* helper for tests to shutdown correctly and tersely */
static void shutdown_and_destroy(grpc_completion_queue* cc) {
  grpc_event ev;
  grpc_completion_queue_shutdown(cc);
  ev =
      grpc_completion_queue_next(cc, gpr_inf_past(GPR_CLOCK_REALTIME), nullptr);
  GPR_ASSERT(ev.type == GRPC_QUEUE_SHUTDOWN);
  grpc_completion_queue_destroy(cc);
}

static void test_alarm(void) {
  grpc_completion_queue* cc;

  LOG_TEST("test_alarm");
  cc = grpc_completion_queue_create_for_next(nullptr);
  {
    /* regular expiry */
    grpc_event ev;
    void* tag = create_test_tag();
    grpc_alarm* alarm = grpc_alarm_create(nullptr);
    grpc_alarm_set(alarm, cc, grpc_timeout_seconds_to_deadline(1), tag,
                   nullptr);

    ev = grpc_completion_queue_next(cc, grpc_timeout_seconds_to_deadline(2),
                                    nullptr);
    GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
    GPR_ASSERT(ev.tag == tag);
    GPR_ASSERT(ev.success);
    grpc_alarm_destroy(alarm, nullptr);
  }
  {
    /* cancellation */
    grpc_event ev;
    void* tag = create_test_tag();
    grpc_alarm* alarm = grpc_alarm_create(nullptr);
    grpc_alarm_set(alarm, cc, grpc_timeout_seconds_to_deadline(2), tag,
                   nullptr);

    grpc_alarm_cancel(alarm, nullptr);
    ev = grpc_completion_queue_next(cc, grpc_timeout_seconds_to_deadline(1),
                                    nullptr);
    GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
    GPR_ASSERT(ev.tag == tag);
    GPR_ASSERT(ev.success == 0);
    grpc_alarm_destroy(alarm, nullptr);
  }
  {
    /* alarm_destroy before cq_next */
    grpc_event ev;
    void* tag = create_test_tag();
    grpc_alarm* alarm = grpc_alarm_create(nullptr);
    grpc_alarm_set(alarm, cc, grpc_timeout_seconds_to_deadline(2), tag,
                   nullptr);

    grpc_alarm_destroy(alarm, nullptr);
    ev = grpc_completion_queue_next(cc, grpc_timeout_seconds_to_deadline(1),
                                    nullptr);
    GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
    GPR_ASSERT(ev.tag == tag);
    GPR_ASSERT(ev.success == 0);
  }
  {
    /* alarm_destroy before set */
    grpc_alarm* alarm = grpc_alarm_create(nullptr);
    grpc_alarm_destroy(alarm, nullptr);
  }

  shutdown_and_destroy(cc);
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_alarm();
  grpc_shutdown();
  return 0;
}
