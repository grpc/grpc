/*
 *
 * Copyright 2016 gRPC authors.
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

#include "src/core/lib/iomgr/resource_quota.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"

gpr_mu g_mu;
gpr_cv g_cv;

static void inc_int_cb(grpc_exec_ctx *exec_ctx, void *a, grpc_error *error) {
  gpr_mu_lock(&g_mu);
  ++*(int *)a;
  gpr_cv_signal(&g_cv);
  gpr_mu_unlock(&g_mu);
}

static void assert_counter_becomes(int *ctr, int value) {
  gpr_mu_lock(&g_mu);
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(5);
  while (*ctr != value) {
    GPR_ASSERT(!gpr_cv_wait(&g_cv, &g_mu, deadline));
  }
  gpr_mu_unlock(&g_mu);
}

static void set_event_cb(grpc_exec_ctx *exec_ctx, void *a, grpc_error *error) {
  gpr_event_set((gpr_event *)a, (void *)1);
}
grpc_closure *set_event(gpr_event *ev) {
  return GRPC_CLOSURE_CREATE(set_event_cb, ev, grpc_schedule_on_exec_ctx);
}

typedef struct {
  size_t size;
  grpc_resource_user *resource_user;
  grpc_closure *then;
} reclaimer_args;
static void reclaimer_cb(grpc_exec_ctx *exec_ctx, void *args,
                         grpc_error *error) {
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  reclaimer_args *a = args;
  grpc_resource_user_free(exec_ctx, a->resource_user, a->size);
  grpc_resource_user_finish_reclamation(exec_ctx, a->resource_user);
  GRPC_CLOSURE_RUN(exec_ctx, a->then, GRPC_ERROR_NONE);
  gpr_free(a);
}
grpc_closure *make_reclaimer(grpc_resource_user *resource_user, size_t size,
                             grpc_closure *then) {
  reclaimer_args *a = gpr_malloc(sizeof(*a));
  a->size = size;
  a->resource_user = resource_user;
  a->then = then;
  return GRPC_CLOSURE_CREATE(reclaimer_cb, a, grpc_schedule_on_exec_ctx);
}

static void unused_reclaimer_cb(grpc_exec_ctx *exec_ctx, void *arg,
                                grpc_error *error) {
  GPR_ASSERT(error == GRPC_ERROR_CANCELLED);
  GRPC_CLOSURE_RUN(exec_ctx, arg, GRPC_ERROR_NONE);
}
grpc_closure *make_unused_reclaimer(grpc_closure *then) {
  return GRPC_CLOSURE_CREATE(unused_reclaimer_cb, then,
                             grpc_schedule_on_exec_ctx);
}

static void destroy_user(grpc_resource_user *usr) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_resource_user_unref(&exec_ctx, usr);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_no_op(void) {
  gpr_log(GPR_INFO, "** test_no_op **");
  grpc_resource_quota_unref(grpc_resource_quota_create("test_no_op"));
}

static void test_resize_then_destroy(void) {
  gpr_log(GPR_INFO, "** test_resize_then_destroy **");
  grpc_resource_quota *q =
      grpc_resource_quota_create("test_resize_then_destroy");
  grpc_resource_quota_resize(q, 1024 * 1024);
  grpc_resource_quota_unref(q);
}

static void test_resource_user_no_op(void) {
  gpr_log(GPR_INFO, "** test_resource_user_no_op **");
  grpc_resource_quota *q =
      grpc_resource_quota_create("test_resource_user_no_op");
  grpc_resource_user *usr = grpc_resource_user_create(q, "usr");
  grpc_resource_quota_unref(q);
  destroy_user(usr);
}

static void test_instant_alloc_then_free(void) {
  gpr_log(GPR_INFO, "** test_instant_alloc_then_free **");
  grpc_resource_quota *q =
      grpc_resource_quota_create("test_instant_alloc_then_free");
  grpc_resource_quota_resize(q, 1024 * 1024);
  grpc_resource_user *usr = grpc_resource_user_create(q, "usr");
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_alloc(&exec_ctx, usr, 1024, NULL);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_free(&exec_ctx, usr, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_resource_quota_unref(q);
  destroy_user(usr);
}

static void test_instant_alloc_free_pair(void) {
  gpr_log(GPR_INFO, "** test_instant_alloc_free_pair **");
  grpc_resource_quota *q =
      grpc_resource_quota_create("test_instant_alloc_free_pair");
  grpc_resource_quota_resize(q, 1024 * 1024);
  grpc_resource_user *usr = grpc_resource_user_create(q, "usr");
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_alloc(&exec_ctx, usr, 1024, NULL);
    grpc_resource_user_free(&exec_ctx, usr, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_resource_quota_unref(q);
  destroy_user(usr);
}

static void test_simple_async_alloc(void) {
  gpr_log(GPR_INFO, "** test_simple_async_alloc **");
  grpc_resource_quota *q =
      grpc_resource_quota_create("test_simple_async_alloc");
  grpc_resource_quota_resize(q, 1024 * 1024);
  grpc_resource_user *usr = grpc_resource_user_create(q, "usr");
  {
    gpr_event ev;
    gpr_event_init(&ev);
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_alloc(&exec_ctx, usr, 1024, set_event(&ev));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(gpr_event_wait(&ev, grpc_timeout_seconds_to_deadline(5)) !=
               NULL);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_free(&exec_ctx, usr, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_resource_quota_unref(q);
  destroy_user(usr);
}

static void test_async_alloc_blocked_by_size(void) {
  gpr_log(GPR_INFO, "** test_async_alloc_blocked_by_size **");
  grpc_resource_quota *q =
      grpc_resource_quota_create("test_async_alloc_blocked_by_size");
  grpc_resource_quota_resize(q, 1);
  grpc_resource_user *usr = grpc_resource_user_create(q, "usr");
  gpr_event ev;
  gpr_event_init(&ev);
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_alloc(&exec_ctx, usr, 1024, set_event(&ev));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(gpr_event_wait(
                   &ev, grpc_timeout_milliseconds_to_deadline(100)) == NULL);
  }
  grpc_resource_quota_resize(q, 1024);
  GPR_ASSERT(gpr_event_wait(&ev, grpc_timeout_seconds_to_deadline(5)) != NULL);
  ;
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_free(&exec_ctx, usr, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_resource_quota_unref(q);
  destroy_user(usr);
}

static void test_scavenge(void) {
  gpr_log(GPR_INFO, "** test_scavenge **");
  grpc_resource_quota *q = grpc_resource_quota_create("test_scavenge");
  grpc_resource_quota_resize(q, 1024);
  grpc_resource_user *usr1 = grpc_resource_user_create(q, "usr1");
  grpc_resource_user *usr2 = grpc_resource_user_create(q, "usr2");
  {
    gpr_event ev;
    gpr_event_init(&ev);
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_alloc(&exec_ctx, usr1, 1024, set_event(&ev));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(gpr_event_wait(&ev, grpc_timeout_seconds_to_deadline(5)) !=
               NULL);
    ;
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_free(&exec_ctx, usr1, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  {
    gpr_event ev;
    gpr_event_init(&ev);
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_alloc(&exec_ctx, usr2, 1024, set_event(&ev));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(gpr_event_wait(&ev, grpc_timeout_seconds_to_deadline(5)) !=
               NULL);
    ;
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_free(&exec_ctx, usr2, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_resource_quota_unref(q);
  destroy_user(usr1);
  destroy_user(usr2);
}

static void test_scavenge_blocked(void) {
  gpr_log(GPR_INFO, "** test_scavenge_blocked **");
  grpc_resource_quota *q = grpc_resource_quota_create("test_scavenge_blocked");
  grpc_resource_quota_resize(q, 1024);
  grpc_resource_user *usr1 = grpc_resource_user_create(q, "usr1");
  grpc_resource_user *usr2 = grpc_resource_user_create(q, "usr2");
  gpr_event ev;
  {
    gpr_event_init(&ev);
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_alloc(&exec_ctx, usr1, 1024, set_event(&ev));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(gpr_event_wait(&ev, grpc_timeout_seconds_to_deadline(5)) !=
               NULL);
    ;
  }
  {
    gpr_event_init(&ev);
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_alloc(&exec_ctx, usr2, 1024, set_event(&ev));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(gpr_event_wait(
                   &ev, grpc_timeout_milliseconds_to_deadline(100)) == NULL);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_free(&exec_ctx, usr1, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(gpr_event_wait(&ev, grpc_timeout_seconds_to_deadline(5)) !=
               NULL);
    ;
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_free(&exec_ctx, usr2, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_resource_quota_unref(q);
  destroy_user(usr1);
  destroy_user(usr2);
}

static void test_blocked_until_scheduled_reclaim(void) {
  gpr_log(GPR_INFO, "** test_blocked_until_scheduled_reclaim **");
  grpc_resource_quota *q =
      grpc_resource_quota_create("test_blocked_until_scheduled_reclaim");
  grpc_resource_quota_resize(q, 1024);
  grpc_resource_user *usr = grpc_resource_user_create(q, "usr");
  {
    gpr_event ev;
    gpr_event_init(&ev);
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_alloc(&exec_ctx, usr, 1024, set_event(&ev));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(gpr_event_wait(&ev, grpc_timeout_seconds_to_deadline(5)) !=
               NULL);
    ;
  }
  gpr_event reclaim_done;
  gpr_event_init(&reclaim_done);
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_post_reclaimer(
        &exec_ctx, usr, false,
        make_reclaimer(usr, 1024, set_event(&reclaim_done)));
    grpc_exec_ctx_finish(&exec_ctx);
  }
  {
    gpr_event ev;
    gpr_event_init(&ev);
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_alloc(&exec_ctx, usr, 1024, set_event(&ev));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(gpr_event_wait(&reclaim_done,
                              grpc_timeout_seconds_to_deadline(5)) != NULL);
    GPR_ASSERT(gpr_event_wait(&ev, grpc_timeout_seconds_to_deadline(5)) !=
               NULL);
    ;
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_free(&exec_ctx, usr, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_resource_quota_unref(q);
  destroy_user(usr);
}

static void test_blocked_until_scheduled_reclaim_and_scavenge(void) {
  gpr_log(GPR_INFO, "** test_blocked_until_scheduled_reclaim_and_scavenge **");
  grpc_resource_quota *q = grpc_resource_quota_create(
      "test_blocked_until_scheduled_reclaim_and_scavenge");
  grpc_resource_quota_resize(q, 1024);
  grpc_resource_user *usr1 = grpc_resource_user_create(q, "usr1");
  grpc_resource_user *usr2 = grpc_resource_user_create(q, "usr2");
  {
    gpr_event ev;
    gpr_event_init(&ev);
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_alloc(&exec_ctx, usr1, 1024, set_event(&ev));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(gpr_event_wait(&ev, grpc_timeout_seconds_to_deadline(5)) !=
               NULL);
    ;
  }
  gpr_event reclaim_done;
  gpr_event_init(&reclaim_done);
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_post_reclaimer(
        &exec_ctx, usr1, false,
        make_reclaimer(usr1, 1024, set_event(&reclaim_done)));
    grpc_exec_ctx_finish(&exec_ctx);
  }
  {
    gpr_event ev;
    gpr_event_init(&ev);
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_alloc(&exec_ctx, usr2, 1024, set_event(&ev));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(gpr_event_wait(&reclaim_done,
                              grpc_timeout_seconds_to_deadline(5)) != NULL);
    GPR_ASSERT(gpr_event_wait(&ev, grpc_timeout_seconds_to_deadline(5)) !=
               NULL);
    ;
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_free(&exec_ctx, usr2, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_resource_quota_unref(q);
  destroy_user(usr1);
  destroy_user(usr2);
}

static void test_blocked_until_scheduled_destructive_reclaim(void) {
  gpr_log(GPR_INFO, "** test_blocked_until_scheduled_destructive_reclaim **");
  grpc_resource_quota *q = grpc_resource_quota_create(
      "test_blocked_until_scheduled_destructive_reclaim");
  grpc_resource_quota_resize(q, 1024);
  grpc_resource_user *usr = grpc_resource_user_create(q, "usr");
  {
    gpr_event ev;
    gpr_event_init(&ev);
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_alloc(&exec_ctx, usr, 1024, set_event(&ev));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(gpr_event_wait(&ev, grpc_timeout_seconds_to_deadline(5)) !=
               NULL);
    ;
  }
  gpr_event reclaim_done;
  gpr_event_init(&reclaim_done);
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_post_reclaimer(
        &exec_ctx, usr, true,
        make_reclaimer(usr, 1024, set_event(&reclaim_done)));
    grpc_exec_ctx_finish(&exec_ctx);
  }
  {
    gpr_event ev;
    gpr_event_init(&ev);
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_alloc(&exec_ctx, usr, 1024, set_event(&ev));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(gpr_event_wait(&reclaim_done,
                              grpc_timeout_seconds_to_deadline(5)) != NULL);
    GPR_ASSERT(gpr_event_wait(&ev, grpc_timeout_seconds_to_deadline(5)) !=
               NULL);
    ;
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_free(&exec_ctx, usr, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_resource_quota_unref(q);
  destroy_user(usr);
}

static void test_unused_reclaim_is_cancelled(void) {
  gpr_log(GPR_INFO, "** test_unused_reclaim_is_cancelled **");
  grpc_resource_quota *q =
      grpc_resource_quota_create("test_unused_reclaim_is_cancelled");
  grpc_resource_quota_resize(q, 1024);
  grpc_resource_user *usr = grpc_resource_user_create(q, "usr");
  gpr_event benign_done;
  gpr_event_init(&benign_done);
  gpr_event destructive_done;
  gpr_event_init(&destructive_done);
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_post_reclaimer(
        &exec_ctx, usr, false, make_unused_reclaimer(set_event(&benign_done)));
    grpc_resource_user_post_reclaimer(
        &exec_ctx, usr, true,
        make_unused_reclaimer(set_event(&destructive_done)));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(gpr_event_wait(&benign_done,
                              grpc_timeout_milliseconds_to_deadline(100)) ==
               NULL);
    GPR_ASSERT(gpr_event_wait(&destructive_done,
                              grpc_timeout_milliseconds_to_deadline(100)) ==
               NULL);
  }
  grpc_resource_quota_unref(q);
  destroy_user(usr);
  GPR_ASSERT(gpr_event_wait(&benign_done,
                            grpc_timeout_seconds_to_deadline(5)) != NULL);
  GPR_ASSERT(gpr_event_wait(&destructive_done,
                            grpc_timeout_seconds_to_deadline(5)) != NULL);
}

static void test_benign_reclaim_is_preferred(void) {
  gpr_log(GPR_INFO, "** test_benign_reclaim_is_preferred **");
  grpc_resource_quota *q =
      grpc_resource_quota_create("test_benign_reclaim_is_preferred");
  grpc_resource_quota_resize(q, 1024);
  grpc_resource_user *usr = grpc_resource_user_create(q, "usr");
  gpr_event benign_done;
  gpr_event_init(&benign_done);
  gpr_event destructive_done;
  gpr_event_init(&destructive_done);
  {
    gpr_event ev;
    gpr_event_init(&ev);
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_alloc(&exec_ctx, usr, 1024, set_event(&ev));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(gpr_event_wait(&ev, grpc_timeout_seconds_to_deadline(5)) !=
               NULL);
    ;
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_post_reclaimer(
        &exec_ctx, usr, false,
        make_reclaimer(usr, 1024, set_event(&benign_done)));
    grpc_resource_user_post_reclaimer(
        &exec_ctx, usr, true,
        make_unused_reclaimer(set_event(&destructive_done)));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(gpr_event_wait(&benign_done,
                              grpc_timeout_milliseconds_to_deadline(100)) ==
               NULL);
    GPR_ASSERT(gpr_event_wait(&destructive_done,
                              grpc_timeout_milliseconds_to_deadline(100)) ==
               NULL);
  }
  {
    gpr_event ev;
    gpr_event_init(&ev);
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_alloc(&exec_ctx, usr, 1024, set_event(&ev));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(gpr_event_wait(&benign_done,
                              grpc_timeout_seconds_to_deadline(5)) != NULL);
    GPR_ASSERT(gpr_event_wait(&destructive_done,
                              grpc_timeout_milliseconds_to_deadline(100)) ==
               NULL);
    GPR_ASSERT(gpr_event_wait(&ev, grpc_timeout_seconds_to_deadline(5)) !=
               NULL);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_free(&exec_ctx, usr, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_resource_quota_unref(q);
  destroy_user(usr);
  GPR_ASSERT(gpr_event_wait(&benign_done,
                            grpc_timeout_seconds_to_deadline(5)) != NULL);
  GPR_ASSERT(gpr_event_wait(&destructive_done,
                            grpc_timeout_seconds_to_deadline(5)) != NULL);
}

static void test_multiple_reclaims_can_be_triggered(void) {
  gpr_log(GPR_INFO, "** test_multiple_reclaims_can_be_triggered **");
  grpc_resource_quota *q =
      grpc_resource_quota_create("test_multiple_reclaims_can_be_triggered");
  grpc_resource_quota_resize(q, 1024);
  grpc_resource_user *usr = grpc_resource_user_create(q, "usr");
  gpr_event benign_done;
  gpr_event_init(&benign_done);
  gpr_event destructive_done;
  gpr_event_init(&destructive_done);
  {
    gpr_event ev;
    gpr_event_init(&ev);
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_alloc(&exec_ctx, usr, 1024, set_event(&ev));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(gpr_event_wait(&ev, grpc_timeout_seconds_to_deadline(5)) !=
               NULL);
    ;
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_post_reclaimer(
        &exec_ctx, usr, false,
        make_reclaimer(usr, 512, set_event(&benign_done)));
    grpc_resource_user_post_reclaimer(
        &exec_ctx, usr, true,
        make_reclaimer(usr, 512, set_event(&destructive_done)));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(gpr_event_wait(&benign_done,
                              grpc_timeout_milliseconds_to_deadline(100)) ==
               NULL);
    GPR_ASSERT(gpr_event_wait(&destructive_done,
                              grpc_timeout_milliseconds_to_deadline(100)) ==
               NULL);
  }
  {
    gpr_event ev;
    gpr_event_init(&ev);
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_alloc(&exec_ctx, usr, 1024, set_event(&ev));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(gpr_event_wait(&benign_done,
                              grpc_timeout_seconds_to_deadline(5)) != NULL);
    GPR_ASSERT(gpr_event_wait(&destructive_done,
                              grpc_timeout_seconds_to_deadline(5)) != NULL);
    GPR_ASSERT(gpr_event_wait(&ev, grpc_timeout_seconds_to_deadline(5)) !=
               NULL);
    ;
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_free(&exec_ctx, usr, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_resource_quota_unref(q);
  destroy_user(usr);
  GPR_ASSERT(gpr_event_wait(&benign_done,
                            grpc_timeout_seconds_to_deadline(5)) != NULL);
  GPR_ASSERT(gpr_event_wait(&destructive_done,
                            grpc_timeout_seconds_to_deadline(5)) != NULL);
}

static void test_resource_user_stays_allocated_until_memory_released(void) {
  gpr_log(GPR_INFO,
          "** test_resource_user_stays_allocated_until_memory_released **");
  grpc_resource_quota *q = grpc_resource_quota_create(
      "test_resource_user_stays_allocated_until_memory_released");
  grpc_resource_quota_resize(q, 1024 * 1024);
  grpc_resource_user *usr = grpc_resource_user_create(q, "usr");
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_alloc(&exec_ctx, usr, 1024, NULL);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_quota_unref(q);
    grpc_resource_user_unref(&exec_ctx, usr);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_free(&exec_ctx, usr, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
}

static void
test_resource_user_stays_allocated_and_reclaimers_unrun_until_memory_released(
    void) {
  gpr_log(GPR_INFO,
          "** "
          "test_resource_user_stays_allocated_and_reclaimers_unrun_until_"
          "memory_released **");
  grpc_resource_quota *q = grpc_resource_quota_create(
      "test_resource_user_stays_allocated_and_reclaimers_unrun_until_memory_"
      "released");
  grpc_resource_quota_resize(q, 1024);
  for (int i = 0; i < 10; i++) {
    grpc_resource_user *usr = grpc_resource_user_create(q, "usr");
    gpr_event reclaimer_cancelled;
    gpr_event_init(&reclaimer_cancelled);
    {
      grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
      grpc_resource_user_post_reclaimer(
          &exec_ctx, usr, false,
          make_unused_reclaimer(set_event(&reclaimer_cancelled)));
      grpc_exec_ctx_finish(&exec_ctx);
      GPR_ASSERT(gpr_event_wait(&reclaimer_cancelled,
                                grpc_timeout_milliseconds_to_deadline(100)) ==
                 NULL);
    }
    {
      gpr_event allocated;
      gpr_event_init(&allocated);
      grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
      grpc_resource_user_alloc(&exec_ctx, usr, 1024, set_event(&allocated));
      grpc_exec_ctx_finish(&exec_ctx);
      GPR_ASSERT(gpr_event_wait(&allocated,
                                grpc_timeout_seconds_to_deadline(5)) != NULL);
      GPR_ASSERT(gpr_event_wait(&reclaimer_cancelled,
                                grpc_timeout_milliseconds_to_deadline(100)) ==
                 NULL);
    }
    {
      grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
      grpc_resource_user_unref(&exec_ctx, usr);
      grpc_exec_ctx_finish(&exec_ctx);
      GPR_ASSERT(gpr_event_wait(&reclaimer_cancelled,
                                grpc_timeout_milliseconds_to_deadline(100)) ==
                 NULL);
    }
    {
      grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
      grpc_resource_user_free(&exec_ctx, usr, 1024);
      grpc_exec_ctx_finish(&exec_ctx);
      GPR_ASSERT(gpr_event_wait(&reclaimer_cancelled,
                                grpc_timeout_seconds_to_deadline(5)) != NULL);
    }
  }
  grpc_resource_quota_unref(q);
}

static void test_reclaimers_can_be_posted_repeatedly(void) {
  gpr_log(GPR_INFO, "** test_reclaimers_can_be_posted_repeatedly **");
  grpc_resource_quota *q =
      grpc_resource_quota_create("test_reclaimers_can_be_posted_repeatedly");
  grpc_resource_quota_resize(q, 1024);
  grpc_resource_user *usr = grpc_resource_user_create(q, "usr");
  {
    gpr_event allocated;
    gpr_event_init(&allocated);
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_alloc(&exec_ctx, usr, 1024, set_event(&allocated));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(gpr_event_wait(&allocated,
                              grpc_timeout_seconds_to_deadline(5)) != NULL);
  }
  for (int i = 0; i < 10; i++) {
    gpr_event reclaimer_done;
    gpr_event_init(&reclaimer_done);
    {
      grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
      grpc_resource_user_post_reclaimer(
          &exec_ctx, usr, false,
          make_reclaimer(usr, 1024, set_event(&reclaimer_done)));
      grpc_exec_ctx_finish(&exec_ctx);
      GPR_ASSERT(gpr_event_wait(&reclaimer_done,
                                grpc_timeout_milliseconds_to_deadline(100)) ==
                 NULL);
    }
    {
      gpr_event allocated;
      gpr_event_init(&allocated);
      grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
      grpc_resource_user_alloc(&exec_ctx, usr, 1024, set_event(&allocated));
      grpc_exec_ctx_finish(&exec_ctx);
      GPR_ASSERT(gpr_event_wait(&allocated,
                                grpc_timeout_seconds_to_deadline(5)) != NULL);
      GPR_ASSERT(gpr_event_wait(&reclaimer_done,
                                grpc_timeout_seconds_to_deadline(5)) != NULL);
    }
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_free(&exec_ctx, usr, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  destroy_user(usr);
  grpc_resource_quota_unref(q);
}

static void test_one_slice(void) {
  gpr_log(GPR_INFO, "** test_one_slice **");

  grpc_resource_quota *q = grpc_resource_quota_create("test_one_slice");
  grpc_resource_quota_resize(q, 1024);

  grpc_resource_user *usr = grpc_resource_user_create(q, "usr");

  grpc_resource_user_slice_allocator alloc;
  int num_allocs = 0;
  grpc_resource_user_slice_allocator_init(&alloc, usr, inc_int_cb, &num_allocs);

  grpc_slice_buffer buffer;
  grpc_slice_buffer_init(&buffer);

  {
    const int start_allocs = num_allocs;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_alloc_slices(&exec_ctx, &alloc, 1024, 1, &buffer);
    grpc_exec_ctx_finish(&exec_ctx);
    assert_counter_becomes(&num_allocs, start_allocs + 1);
  }

  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_slice_buffer_destroy_internal(&exec_ctx, &buffer);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  destroy_user(usr);
  grpc_resource_quota_unref(q);
}

static void test_one_slice_deleted_late(void) {
  gpr_log(GPR_INFO, "** test_one_slice_deleted_late **");

  grpc_resource_quota *q =
      grpc_resource_quota_create("test_one_slice_deleted_late");
  grpc_resource_quota_resize(q, 1024);

  grpc_resource_user *usr = grpc_resource_user_create(q, "usr");

  grpc_resource_user_slice_allocator alloc;
  int num_allocs = 0;
  grpc_resource_user_slice_allocator_init(&alloc, usr, inc_int_cb, &num_allocs);

  grpc_slice_buffer buffer;
  grpc_slice_buffer_init(&buffer);

  {
    const int start_allocs = num_allocs;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_alloc_slices(&exec_ctx, &alloc, 1024, 1, &buffer);
    grpc_exec_ctx_finish(&exec_ctx);
    assert_counter_becomes(&num_allocs, start_allocs + 1);
  }

  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_unref(&exec_ctx, usr);
    grpc_exec_ctx_finish(&exec_ctx);
  }

  grpc_resource_quota_unref(q);
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_slice_buffer_destroy_internal(&exec_ctx, &buffer);
    grpc_exec_ctx_finish(&exec_ctx);
  }
}

static void test_resize_to_zero(void) {
  gpr_log(GPR_INFO, "** test_resize_to_zero **");
  grpc_resource_quota *q = grpc_resource_quota_create("test_resize_to_zero");
  grpc_resource_quota_resize(q, 0);
  grpc_resource_quota_unref(q);
}

static void test_negative_rq_free_pool(void) {
  gpr_log(GPR_INFO, "** test_negative_rq_free_pool **");
  grpc_resource_quota *q =
      grpc_resource_quota_create("test_negative_rq_free_pool");
  grpc_resource_quota_resize(q, 1024);

  grpc_resource_user *usr = grpc_resource_user_create(q, "usr");

  grpc_resource_user_slice_allocator alloc;
  int num_allocs = 0;
  grpc_resource_user_slice_allocator_init(&alloc, usr, inc_int_cb, &num_allocs);

  grpc_slice_buffer buffer;
  grpc_slice_buffer_init(&buffer);

  {
    const int start_allocs = num_allocs;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_alloc_slices(&exec_ctx, &alloc, 1024, 1, &buffer);
    grpc_exec_ctx_finish(&exec_ctx);
    assert_counter_becomes(&num_allocs, start_allocs + 1);
  }

  grpc_resource_quota_resize(q, 512);

  double eps = 0.0001;
  GPR_ASSERT(grpc_resource_quota_get_memory_pressure(q) < 1 + eps);
  GPR_ASSERT(grpc_resource_quota_get_memory_pressure(q) > 1 - eps);

  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_unref(&exec_ctx, usr);
    grpc_exec_ctx_finish(&exec_ctx);
  }

  grpc_resource_quota_unref(q);
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_slice_buffer_destroy_internal(&exec_ctx, &buffer);
    grpc_exec_ctx_finish(&exec_ctx);
  }
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  gpr_mu_init(&g_mu);
  gpr_cv_init(&g_cv);
  test_no_op();
  test_resize_then_destroy();
  test_resource_user_no_op();
  test_instant_alloc_then_free();
  test_instant_alloc_free_pair();
  test_simple_async_alloc();
  test_async_alloc_blocked_by_size();
  test_scavenge();
  test_scavenge_blocked();
  test_blocked_until_scheduled_reclaim();
  test_blocked_until_scheduled_reclaim_and_scavenge();
  test_blocked_until_scheduled_destructive_reclaim();
  test_unused_reclaim_is_cancelled();
  test_benign_reclaim_is_preferred();
  test_multiple_reclaims_can_be_triggered();
  test_resource_user_stays_allocated_until_memory_released();
  test_resource_user_stays_allocated_and_reclaimers_unrun_until_memory_released();
  test_reclaimers_can_be_posted_repeatedly();
  test_one_slice();
  test_one_slice_deleted_late();
  test_resize_to_zero();
  test_negative_rq_free_pool();
  gpr_mu_destroy(&g_mu);
  gpr_cv_destroy(&g_cv);
  grpc_shutdown();
  return 0;
}
