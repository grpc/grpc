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

#include "src/core/lib/surface/completion_queue.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/iomgr.h"
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

  switch (grpc_get_cq_completion_type(cc)) {
    case GRPC_CQ_NEXT: {
      ev = grpc_completion_queue_next(cc, gpr_inf_past(GPR_CLOCK_REALTIME),
                                      nullptr);
      GPR_ASSERT(ev.type == GRPC_QUEUE_SHUTDOWN);
      break;
    }
    case GRPC_CQ_PLUCK: {
      ev = grpc_completion_queue_pluck(
          cc, create_test_tag(), gpr_inf_past(GPR_CLOCK_REALTIME), nullptr);
      GPR_ASSERT(ev.type == GRPC_QUEUE_SHUTDOWN);
      break;
    }
    case GRPC_CQ_CALLBACK: {
      // Nothing to do here. The shutdown callback will be invoked when
      // possible.
      break;
    }
    default: {
      gpr_log(GPR_ERROR, "Unknown completion type");
      break;
    }
  }

  grpc_completion_queue_destroy(cc);
}

/* ensure we can create and destroy a completion channel */
static void test_no_op(void) {
  grpc_cq_completion_type completion_types[] = {GRPC_CQ_NEXT, GRPC_CQ_PLUCK};
  grpc_cq_polling_type polling_types[] = {
      GRPC_CQ_DEFAULT_POLLING, GRPC_CQ_NON_LISTENING, GRPC_CQ_NON_POLLING};
  grpc_completion_queue_attributes attr;
  LOG_TEST("test_no_op");

  attr.version = 1;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(completion_types); i++) {
    for (size_t j = 0; j < GPR_ARRAY_SIZE(polling_types); j++) {
      attr.cq_completion_type = completion_types[i];
      attr.cq_polling_type = polling_types[j];
      shutdown_and_destroy(grpc_completion_queue_create(
          grpc_completion_queue_factory_lookup(&attr), &attr, nullptr));
    }
  }
}

static void test_pollset_conversion(void) {
  grpc_cq_completion_type completion_types[] = {GRPC_CQ_NEXT, GRPC_CQ_PLUCK};
  grpc_cq_polling_type polling_types[] = {GRPC_CQ_DEFAULT_POLLING,
                                          GRPC_CQ_NON_LISTENING};
  grpc_completion_queue* cq;
  grpc_completion_queue_attributes attr;

  LOG_TEST("test_pollset_conversion");

  attr.version = 1;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(completion_types); i++) {
    for (size_t j = 0; j < GPR_ARRAY_SIZE(polling_types); j++) {
      attr.cq_completion_type = completion_types[i];
      attr.cq_polling_type = polling_types[j];
      cq = grpc_completion_queue_create(
          grpc_completion_queue_factory_lookup(&attr), &attr, nullptr);
      GPR_ASSERT(grpc_cq_pollset(cq) != nullptr);
      shutdown_and_destroy(cq);
    }
  }
}

static void test_wait_empty(void) {
  grpc_cq_polling_type polling_types[] = {
      GRPC_CQ_DEFAULT_POLLING, GRPC_CQ_NON_LISTENING, GRPC_CQ_NON_POLLING};
  grpc_completion_queue* cc;
  grpc_completion_queue_attributes attr;
  grpc_event event;

  LOG_TEST("test_wait_empty");

  attr.version = 1;
  attr.cq_completion_type = GRPC_CQ_NEXT;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(polling_types); i++) {
    attr.cq_polling_type = polling_types[i];
    cc = grpc_completion_queue_create(
        grpc_completion_queue_factory_lookup(&attr), &attr, nullptr);
    event =
        grpc_completion_queue_next(cc, gpr_now(GPR_CLOCK_REALTIME), nullptr);
    GPR_ASSERT(event.type == GRPC_QUEUE_TIMEOUT);
    shutdown_and_destroy(cc);
  }
}

static void do_nothing_end_completion(void* /*arg*/,
                                      grpc_cq_completion* /*c*/) {}

static void test_cq_end_op(void) {
  grpc_event ev;
  grpc_completion_queue* cc;
  grpc_cq_completion completion;
  grpc_cq_polling_type polling_types[] = {
      GRPC_CQ_DEFAULT_POLLING, GRPC_CQ_NON_LISTENING, GRPC_CQ_NON_POLLING};
  grpc_completion_queue_attributes attr;
  void* tag = create_test_tag();

  LOG_TEST("test_cq_end_op");

  attr.version = 1;
  attr.cq_completion_type = GRPC_CQ_NEXT;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(polling_types); i++) {
    grpc_core::ExecCtx exec_ctx;
    attr.cq_polling_type = polling_types[i];
    cc = grpc_completion_queue_create(
        grpc_completion_queue_factory_lookup(&attr), &attr, nullptr);

    GPR_ASSERT(grpc_cq_begin_op(cc, tag));
    grpc_cq_end_op(cc, tag, GRPC_ERROR_NONE, do_nothing_end_completion, nullptr,
                   &completion);

    ev = grpc_completion_queue_next(cc, gpr_inf_past(GPR_CLOCK_REALTIME),
                                    nullptr);
    GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
    GPR_ASSERT(ev.tag == tag);
    GPR_ASSERT(ev.success);

    shutdown_and_destroy(cc);
  }
}

static void test_cq_tls_cache_full(void) {
  grpc_event ev;
  grpc_completion_queue* cc;
  grpc_cq_completion completion;
  grpc_cq_polling_type polling_types[] = {
      GRPC_CQ_DEFAULT_POLLING, GRPC_CQ_NON_LISTENING, GRPC_CQ_NON_POLLING};
  grpc_completion_queue_attributes attr;
  void* tag = create_test_tag();
  void* res_tag;
  int ok;

  LOG_TEST("test_cq_tls_cache_full");

  attr.version = 1;
  attr.cq_completion_type = GRPC_CQ_NEXT;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(polling_types); i++) {
    grpc_core::ExecCtx exec_ctx;  // Reset exec_ctx
    attr.cq_polling_type = polling_types[i];
    cc = grpc_completion_queue_create(
        grpc_completion_queue_factory_lookup(&attr), &attr, nullptr);

    grpc_completion_queue_thread_local_cache_init(cc);
    GPR_ASSERT(grpc_cq_begin_op(cc, tag));
    grpc_cq_end_op(cc, tag, GRPC_ERROR_NONE, do_nothing_end_completion, nullptr,
                   &completion);

    ev = grpc_completion_queue_next(cc, gpr_inf_past(GPR_CLOCK_REALTIME),
                                    nullptr);
    GPR_ASSERT(ev.type == GRPC_QUEUE_TIMEOUT);

    GPR_ASSERT(
        grpc_completion_queue_thread_local_cache_flush(cc, &res_tag, &ok) == 1);
    GPR_ASSERT(res_tag == tag);
    GPR_ASSERT(ok);

    ev = grpc_completion_queue_next(cc, gpr_inf_past(GPR_CLOCK_REALTIME),
                                    nullptr);
    GPR_ASSERT(ev.type == GRPC_QUEUE_TIMEOUT);

    shutdown_and_destroy(cc);
  }
}

static void test_cq_tls_cache_empty(void) {
  grpc_completion_queue* cc;
  grpc_cq_polling_type polling_types[] = {
      GRPC_CQ_DEFAULT_POLLING, GRPC_CQ_NON_LISTENING, GRPC_CQ_NON_POLLING};
  grpc_completion_queue_attributes attr;
  void* res_tag;
  int ok;

  LOG_TEST("test_cq_tls_cache_empty");

  attr.version = 1;
  attr.cq_completion_type = GRPC_CQ_NEXT;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(polling_types); i++) {
    grpc_core::ExecCtx exec_ctx;  // Reset exec_ctx
    attr.cq_polling_type = polling_types[i];
    cc = grpc_completion_queue_create(
        grpc_completion_queue_factory_lookup(&attr), &attr, nullptr);

    GPR_ASSERT(
        grpc_completion_queue_thread_local_cache_flush(cc, &res_tag, &ok) == 0);
    grpc_completion_queue_thread_local_cache_init(cc);
    GPR_ASSERT(
        grpc_completion_queue_thread_local_cache_flush(cc, &res_tag, &ok) == 0);
    shutdown_and_destroy(cc);
  }
}

static void test_shutdown_then_next_polling(void) {
  grpc_cq_polling_type polling_types[] = {
      GRPC_CQ_DEFAULT_POLLING, GRPC_CQ_NON_LISTENING, GRPC_CQ_NON_POLLING};
  grpc_completion_queue* cc;
  grpc_completion_queue_attributes attr;
  grpc_event event;
  LOG_TEST("test_shutdown_then_next_polling");

  attr.version = 1;
  attr.cq_completion_type = GRPC_CQ_NEXT;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(polling_types); i++) {
    attr.cq_polling_type = polling_types[i];
    cc = grpc_completion_queue_create(
        grpc_completion_queue_factory_lookup(&attr), &attr, nullptr);
    grpc_completion_queue_shutdown(cc);
    event = grpc_completion_queue_next(cc, gpr_inf_past(GPR_CLOCK_REALTIME),
                                       nullptr);
    GPR_ASSERT(event.type == GRPC_QUEUE_SHUTDOWN);
    grpc_completion_queue_destroy(cc);
  }
}

static void test_shutdown_then_next_with_timeout(void) {
  grpc_cq_polling_type polling_types[] = {
      GRPC_CQ_DEFAULT_POLLING, GRPC_CQ_NON_LISTENING, GRPC_CQ_NON_POLLING};
  grpc_completion_queue* cc;
  grpc_completion_queue_attributes attr;
  grpc_event event;
  LOG_TEST("test_shutdown_then_next_with_timeout");

  attr.version = 1;
  attr.cq_completion_type = GRPC_CQ_NEXT;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(polling_types); i++) {
    attr.cq_polling_type = polling_types[i];
    cc = grpc_completion_queue_create(
        grpc_completion_queue_factory_lookup(&attr), &attr, nullptr);

    grpc_completion_queue_shutdown(cc);
    event = grpc_completion_queue_next(cc, gpr_inf_future(GPR_CLOCK_REALTIME),
                                       nullptr);
    GPR_ASSERT(event.type == GRPC_QUEUE_SHUTDOWN);
    grpc_completion_queue_destroy(cc);
  }
}

static void test_pluck(void) {
  grpc_event ev;
  grpc_completion_queue* cc;
  void* tags[128];
  grpc_cq_completion completions[GPR_ARRAY_SIZE(tags)];
  grpc_cq_polling_type polling_types[] = {
      GRPC_CQ_DEFAULT_POLLING, GRPC_CQ_NON_LISTENING, GRPC_CQ_NON_POLLING};
  grpc_completion_queue_attributes attr;
  unsigned i, j;

  LOG_TEST("test_pluck");

  for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
    tags[i] = create_test_tag();
    for (j = 0; j < i; j++) {
      GPR_ASSERT(tags[i] != tags[j]);
    }
  }

  attr.version = 1;
  attr.cq_completion_type = GRPC_CQ_PLUCK;
  for (size_t pidx = 0; pidx < GPR_ARRAY_SIZE(polling_types); pidx++) {
    grpc_core::ExecCtx exec_ctx;  // reset exec_ctx
    attr.cq_polling_type = polling_types[pidx];
    cc = grpc_completion_queue_create(
        grpc_completion_queue_factory_lookup(&attr), &attr, nullptr);

    for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
      GPR_ASSERT(grpc_cq_begin_op(cc, tags[i]));
      grpc_cq_end_op(cc, tags[i], GRPC_ERROR_NONE, do_nothing_end_completion,
                     nullptr, &completions[i]);
    }

    for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
      ev = grpc_completion_queue_pluck(
          cc, tags[i], gpr_inf_past(GPR_CLOCK_REALTIME), nullptr);
      GPR_ASSERT(ev.tag == tags[i]);
    }

    for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
      GPR_ASSERT(grpc_cq_begin_op(cc, tags[i]));
      grpc_cq_end_op(cc, tags[i], GRPC_ERROR_NONE, do_nothing_end_completion,
                     nullptr, &completions[i]);
    }

    for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
      ev = grpc_completion_queue_pluck(cc, tags[GPR_ARRAY_SIZE(tags) - i - 1],
                                       gpr_inf_past(GPR_CLOCK_REALTIME),
                                       nullptr);
      GPR_ASSERT(ev.tag == tags[GPR_ARRAY_SIZE(tags) - i - 1]);
    }

    shutdown_and_destroy(cc);
  }
}

static void test_pluck_after_shutdown(void) {
  grpc_cq_polling_type polling_types[] = {
      GRPC_CQ_DEFAULT_POLLING, GRPC_CQ_NON_LISTENING, GRPC_CQ_NON_POLLING};
  grpc_event ev;
  grpc_completion_queue* cc;
  grpc_completion_queue_attributes attr;

  LOG_TEST("test_pluck_after_shutdown");

  attr.version = 1;
  attr.cq_completion_type = GRPC_CQ_PLUCK;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(polling_types); i++) {
    attr.cq_polling_type = polling_types[i];
    cc = grpc_completion_queue_create(
        grpc_completion_queue_factory_lookup(&attr), &attr, nullptr);
    grpc_completion_queue_shutdown(cc);
    ev = grpc_completion_queue_pluck(
        cc, nullptr, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    GPR_ASSERT(ev.type == GRPC_QUEUE_SHUTDOWN);
    grpc_completion_queue_destroy(cc);
  }
}

static void test_callback(void) {
  grpc_completion_queue* cc;
  static void* tags[128];
  grpc_cq_completion completions[GPR_ARRAY_SIZE(tags)];
  grpc_cq_polling_type polling_types[] = {
      GRPC_CQ_DEFAULT_POLLING, GRPC_CQ_NON_LISTENING, GRPC_CQ_NON_POLLING};
  grpc_completion_queue_attributes attr;
  unsigned i;
  static gpr_mu mu, shutdown_mu;
  static gpr_cv cv, shutdown_cv;
  static int cb_counter;
  gpr_mu_init(&mu);
  gpr_mu_init(&shutdown_mu);
  gpr_cv_init(&cv);
  gpr_cv_init(&shutdown_cv);

  LOG_TEST("test_callback");

  bool got_shutdown = false;
  class ShutdownCallback : public grpc_experimental_completion_queue_functor {
   public:
    ShutdownCallback(bool* done) : done_(done) {
      functor_run = &ShutdownCallback::Run;
      inlineable = false;
    }
    ~ShutdownCallback() {}
    static void Run(grpc_experimental_completion_queue_functor* cb, int ok) {
      gpr_mu_lock(&shutdown_mu);
      *static_cast<ShutdownCallback*>(cb)->done_ = static_cast<bool>(ok);
      // Signal when the shutdown callback is completed.
      gpr_cv_signal(&shutdown_cv);
      gpr_mu_unlock(&shutdown_mu);
    }

   private:
    bool* done_;
  };
  ShutdownCallback shutdown_cb(&got_shutdown);

  attr.version = 2;
  attr.cq_completion_type = GRPC_CQ_CALLBACK;
  attr.cq_shutdown_cb = &shutdown_cb;

  for (size_t pidx = 0; pidx < GPR_ARRAY_SIZE(polling_types); pidx++) {
    int sumtags = 0;
    int counter = 0;
    cb_counter = 0;
    {
      // reset exec_ctx types
      grpc_core::ExecCtx exec_ctx;
      attr.cq_polling_type = polling_types[pidx];
      cc = grpc_completion_queue_create(
          grpc_completion_queue_factory_lookup(&attr), &attr, nullptr);

      class TagCallback : public grpc_experimental_completion_queue_functor {
       public:
        TagCallback(int* counter, int tag) : counter_(counter), tag_(tag) {
          functor_run = &TagCallback::Run;
          // Inlineable should be false since this callback takes locks.
          inlineable = false;
        }
        ~TagCallback() {}
        static void Run(grpc_experimental_completion_queue_functor* cb,
                        int ok) {
          GPR_ASSERT(static_cast<bool>(ok));
          auto* callback = static_cast<TagCallback*>(cb);
          gpr_mu_lock(&mu);
          cb_counter++;
          *callback->counter_ += callback->tag_;
          if (cb_counter == GPR_ARRAY_SIZE(tags)) {
            gpr_cv_signal(&cv);
          }
          gpr_mu_unlock(&mu);
          delete callback;
        };

       private:
        int* counter_;
        int tag_;
      };

      for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
        tags[i] = static_cast<void*>(new TagCallback(&counter, i));
        sumtags += i;
      }

      for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
        GPR_ASSERT(grpc_cq_begin_op(cc, tags[i]));
        grpc_cq_end_op(cc, tags[i], GRPC_ERROR_NONE, do_nothing_end_completion,
                       nullptr, &completions[i]);
      }

      gpr_mu_lock(&mu);
      while (cb_counter != GPR_ARRAY_SIZE(tags)) {
        // Wait for all the callbacks to complete.
        gpr_cv_wait(&cv, &mu, gpr_inf_future(GPR_CLOCK_REALTIME));
      }
      gpr_mu_unlock(&mu);

      shutdown_and_destroy(cc);

      gpr_mu_lock(&shutdown_mu);
      while (!got_shutdown) {
        // Wait for the shutdown callback to complete.
        gpr_cv_wait(&shutdown_cv, &shutdown_mu,
                    gpr_inf_future(GPR_CLOCK_REALTIME));
      }
      gpr_mu_unlock(&shutdown_mu);
    }

    // Run the assertions to check if the test ran successfully.
    GPR_ASSERT(sumtags == counter);
    GPR_ASSERT(got_shutdown);
    got_shutdown = false;
  }

  gpr_cv_destroy(&cv);
  gpr_cv_destroy(&shutdown_cv);
  gpr_mu_destroy(&mu);
  gpr_mu_destroy(&shutdown_mu);
}

struct thread_state {
  grpc_completion_queue* cc;
  void* tag;
};

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  test_no_op();
  test_pollset_conversion();
  test_wait_empty();
  test_shutdown_then_next_polling();
  test_shutdown_then_next_with_timeout();
  test_cq_end_op();
  test_pluck();
  test_pluck_after_shutdown();
  test_cq_tls_cache_full();
  test_cq_tls_cache_empty();
  test_callback();
  grpc_shutdown();
  return 0;
}
