/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/core/lib/iomgr/buffer_pool.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "test/core/util/test_config.h"

static void inc_int_cb(grpc_exec_ctx *exec_ctx, void *a, grpc_error *error) {
  ++*(int *)a;
}

static void set_bool_cb(grpc_exec_ctx *exec_ctx, void *a, grpc_error *error) {
  *(bool *)a = true;
}
grpc_closure *set_bool(bool *p) { return grpc_closure_create(set_bool_cb, p); }

typedef struct {
  size_t size;
  grpc_buffer_user *buffer_user;
  grpc_closure *then;
} reclaimer_args;
static void reclaimer_cb(grpc_exec_ctx *exec_ctx, void *args,
                         grpc_error *error) {
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  reclaimer_args *a = args;
  grpc_buffer_user_free(exec_ctx, a->buffer_user, a->size);
  grpc_buffer_user_finish_reclaimation(exec_ctx, a->buffer_user);
  grpc_closure_run(exec_ctx, a->then, GRPC_ERROR_NONE);
  gpr_free(a);
}
grpc_closure *make_reclaimer(grpc_buffer_user *buffer_user, size_t size,
                             grpc_closure *then) {
  reclaimer_args *a = gpr_malloc(sizeof(*a));
  a->size = size;
  a->buffer_user = buffer_user;
  a->then = then;
  return grpc_closure_create(reclaimer_cb, a);
}

static void unused_reclaimer_cb(grpc_exec_ctx *exec_ctx, void *arg,
                                grpc_error *error) {
  GPR_ASSERT(error == GRPC_ERROR_CANCELLED);
  grpc_closure_run(exec_ctx, arg, GRPC_ERROR_NONE);
}
grpc_closure *make_unused_reclaimer(grpc_closure *then) {
  return grpc_closure_create(unused_reclaimer_cb, then);
}

static void destroy_user(grpc_buffer_user *usr) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  bool done = false;
  grpc_buffer_user_shutdown(&exec_ctx, usr, set_bool(&done));
  grpc_exec_ctx_flush(&exec_ctx);
  GPR_ASSERT(done);
  grpc_buffer_user_destroy(&exec_ctx, usr);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_no_op(void) {
  gpr_log(GPR_INFO, "** test_no_op **");
  grpc_buffer_pool_unref(grpc_buffer_pool_create());
}

static void test_resize_then_destroy(void) {
  gpr_log(GPR_INFO, "** test_resize_then_destroy **");
  grpc_buffer_pool *p = grpc_buffer_pool_create();
  grpc_buffer_pool_resize(p, 1024 * 1024);
  grpc_buffer_pool_unref(p);
}

static void test_buffer_user_no_op(void) {
  gpr_log(GPR_INFO, "** test_buffer_user_no_op **");
  grpc_buffer_pool *p = grpc_buffer_pool_create();
  grpc_buffer_user usr;
  grpc_buffer_user_init(&usr, p);
  grpc_buffer_pool_unref(p);
  destroy_user(&usr);
}

static void test_instant_alloc_then_free(void) {
  gpr_log(GPR_INFO, "** test_instant_alloc_then_free **");
  grpc_buffer_pool *p = grpc_buffer_pool_create();
  grpc_buffer_pool_resize(p, 1024 * 1024);
  grpc_buffer_user usr;
  grpc_buffer_user_init(&usr, p);
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_alloc(&exec_ctx, &usr, 1024, NULL);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_free(&exec_ctx, &usr, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_buffer_pool_unref(p);
  destroy_user(&usr);
}

static void test_instant_alloc_free_pair(void) {
  gpr_log(GPR_INFO, "** test_instant_alloc_free_pair **");
  grpc_buffer_pool *p = grpc_buffer_pool_create();
  grpc_buffer_pool_resize(p, 1024 * 1024);
  grpc_buffer_user usr;
  grpc_buffer_user_init(&usr, p);
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_alloc(&exec_ctx, &usr, 1024, NULL);
    grpc_buffer_user_free(&exec_ctx, &usr, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_buffer_pool_unref(p);
  destroy_user(&usr);
}

static void test_simple_async_alloc(void) {
  gpr_log(GPR_INFO, "** test_simple_async_alloc **");
  grpc_buffer_pool *p = grpc_buffer_pool_create();
  grpc_buffer_pool_resize(p, 1024 * 1024);
  grpc_buffer_user usr;
  grpc_buffer_user_init(&usr, p);
  {
    bool done = false;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_alloc(&exec_ctx, &usr, 1024, set_bool(&done));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(done);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_free(&exec_ctx, &usr, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_buffer_pool_unref(p);
  destroy_user(&usr);
}

static void test_async_alloc_blocked_by_size(void) {
  gpr_log(GPR_INFO, "** test_async_alloc_blocked_by_size **");
  grpc_buffer_pool *p = grpc_buffer_pool_create();
  grpc_buffer_pool_resize(p, 1);
  grpc_buffer_user usr;
  grpc_buffer_user_init(&usr, p);
  bool done = false;
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_alloc(&exec_ctx, &usr, 1024, set_bool(&done));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(!done);
  }
  grpc_buffer_pool_resize(p, 1024);
  GPR_ASSERT(done);
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_free(&exec_ctx, &usr, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_buffer_pool_unref(p);
  destroy_user(&usr);
}

static void test_scavenge(void) {
  gpr_log(GPR_INFO, "** test_scavenge **");
  grpc_buffer_pool *p = grpc_buffer_pool_create();
  grpc_buffer_pool_resize(p, 1024);
  grpc_buffer_user usr1;
  grpc_buffer_user usr2;
  grpc_buffer_user_init(&usr1, p);
  grpc_buffer_user_init(&usr2, p);
  {
    bool done = false;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_alloc(&exec_ctx, &usr1, 1024, set_bool(&done));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(done);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_free(&exec_ctx, &usr1, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  {
    bool done = false;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_alloc(&exec_ctx, &usr2, 1024, set_bool(&done));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(done);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_free(&exec_ctx, &usr2, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_buffer_pool_unref(p);
  destroy_user(&usr1);
  destroy_user(&usr2);
}

static void test_scavenge_blocked(void) {
  gpr_log(GPR_INFO, "** test_scavenge_blocked **");
  grpc_buffer_pool *p = grpc_buffer_pool_create();
  grpc_buffer_pool_resize(p, 1024);
  grpc_buffer_user usr1;
  grpc_buffer_user usr2;
  grpc_buffer_user_init(&usr1, p);
  grpc_buffer_user_init(&usr2, p);
  bool done;
  {
    done = false;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_alloc(&exec_ctx, &usr1, 1024, set_bool(&done));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(done);
  }
  {
    done = false;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_alloc(&exec_ctx, &usr2, 1024, set_bool(&done));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(!done);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_free(&exec_ctx, &usr1, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(done);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_free(&exec_ctx, &usr2, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_buffer_pool_unref(p);
  destroy_user(&usr1);
  destroy_user(&usr2);
}

static void test_blocked_until_scheduled_reclaim(void) {
  gpr_log(GPR_INFO, "** test_blocked_until_scheduled_reclaim **");
  grpc_buffer_pool *p = grpc_buffer_pool_create();
  grpc_buffer_pool_resize(p, 1024);
  grpc_buffer_user usr;
  grpc_buffer_user_init(&usr, p);
  {
    bool done = false;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_alloc(&exec_ctx, &usr, 1024, set_bool(&done));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(done);
  }
  bool reclaim_done = false;
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_post_reclaimer(
        &exec_ctx, &usr, false,
        make_reclaimer(&usr, 1024, set_bool(&reclaim_done)));
    grpc_exec_ctx_finish(&exec_ctx);
  }
  {
    bool done = false;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_alloc(&exec_ctx, &usr, 1024, set_bool(&done));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(reclaim_done);
    GPR_ASSERT(done);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_free(&exec_ctx, &usr, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_buffer_pool_unref(p);
  destroy_user(&usr);
}

static void test_blocked_until_scheduled_reclaim_and_scavenge(void) {
  gpr_log(GPR_INFO, "** test_blocked_until_scheduled_reclaim_and_scavenge **");
  grpc_buffer_pool *p = grpc_buffer_pool_create();
  grpc_buffer_pool_resize(p, 1024);
  grpc_buffer_user usr1;
  grpc_buffer_user usr2;
  grpc_buffer_user_init(&usr1, p);
  grpc_buffer_user_init(&usr2, p);
  {
    bool done = false;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_alloc(&exec_ctx, &usr1, 1024, set_bool(&done));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(done);
  }
  bool reclaim_done = false;
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_post_reclaimer(
        &exec_ctx, &usr1, false,
        make_reclaimer(&usr1, 1024, set_bool(&reclaim_done)));
    grpc_exec_ctx_finish(&exec_ctx);
  }
  {
    bool done = false;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_alloc(&exec_ctx, &usr2, 1024, set_bool(&done));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(reclaim_done);
    GPR_ASSERT(done);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_free(&exec_ctx, &usr2, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_buffer_pool_unref(p);
  destroy_user(&usr1);
  destroy_user(&usr2);
}

static void test_blocked_until_scheduled_destructive_reclaim(void) {
  gpr_log(GPR_INFO, "** test_blocked_until_scheduled_destructive_reclaim **");
  grpc_buffer_pool *p = grpc_buffer_pool_create();
  grpc_buffer_pool_resize(p, 1024);
  grpc_buffer_user usr;
  grpc_buffer_user_init(&usr, p);
  {
    bool done = false;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_alloc(&exec_ctx, &usr, 1024, set_bool(&done));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(done);
  }
  bool reclaim_done = false;
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_post_reclaimer(
        &exec_ctx, &usr, true,
        make_reclaimer(&usr, 1024, set_bool(&reclaim_done)));
    grpc_exec_ctx_finish(&exec_ctx);
  }
  {
    bool done = false;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_alloc(&exec_ctx, &usr, 1024, set_bool(&done));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(reclaim_done);
    GPR_ASSERT(done);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_free(&exec_ctx, &usr, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_buffer_pool_unref(p);
  destroy_user(&usr);
}

static void test_unused_reclaim_is_cancelled(void) {
  gpr_log(GPR_INFO, "** test_unused_reclaim_is_cancelled **");
  grpc_buffer_pool *p = grpc_buffer_pool_create();
  grpc_buffer_pool_resize(p, 1024);
  grpc_buffer_user usr;
  grpc_buffer_user_init(&usr, p);
  bool benign_done = false;
  bool destructive_done = false;
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_post_reclaimer(
        &exec_ctx, &usr, false, make_unused_reclaimer(set_bool(&benign_done)));
    grpc_buffer_user_post_reclaimer(
        &exec_ctx, &usr, true,
        make_unused_reclaimer(set_bool(&destructive_done)));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(!benign_done);
    GPR_ASSERT(!destructive_done);
  }
  grpc_buffer_pool_unref(p);
  destroy_user(&usr);
  GPR_ASSERT(benign_done);
  GPR_ASSERT(destructive_done);
}

static void test_benign_reclaim_is_preferred(void) {
  gpr_log(GPR_INFO, "** test_benign_reclaim_is_preferred **");
  grpc_buffer_pool *p = grpc_buffer_pool_create();
  grpc_buffer_pool_resize(p, 1024);
  grpc_buffer_user usr;
  grpc_buffer_user_init(&usr, p);
  bool benign_done = false;
  bool destructive_done = false;
  {
    bool done = false;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_alloc(&exec_ctx, &usr, 1024, set_bool(&done));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(done);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_post_reclaimer(
        &exec_ctx, &usr, false,
        make_reclaimer(&usr, 1024, set_bool(&benign_done)));
    grpc_buffer_user_post_reclaimer(
        &exec_ctx, &usr, true,
        make_unused_reclaimer(set_bool(&destructive_done)));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(!benign_done);
    GPR_ASSERT(!destructive_done);
  }
  {
    bool done = false;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_alloc(&exec_ctx, &usr, 1024, set_bool(&done));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(benign_done);
    GPR_ASSERT(!destructive_done);
    GPR_ASSERT(done);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_free(&exec_ctx, &usr, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_buffer_pool_unref(p);
  destroy_user(&usr);
  GPR_ASSERT(benign_done);
  GPR_ASSERT(destructive_done);
}

static void test_multiple_reclaims_can_be_triggered(void) {
  gpr_log(GPR_INFO, "** test_multiple_reclaims_can_be_triggered **");
  grpc_buffer_pool *p = grpc_buffer_pool_create();
  grpc_buffer_pool_resize(p, 1024);
  grpc_buffer_user usr;
  grpc_buffer_user_init(&usr, p);
  bool benign_done = false;
  bool destructive_done = false;
  {
    bool done = false;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_alloc(&exec_ctx, &usr, 1024, set_bool(&done));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(done);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_post_reclaimer(
        &exec_ctx, &usr, false,
        make_reclaimer(&usr, 512, set_bool(&benign_done)));
    grpc_buffer_user_post_reclaimer(
        &exec_ctx, &usr, true,
        make_reclaimer(&usr, 512, set_bool(&destructive_done)));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(!benign_done);
    GPR_ASSERT(!destructive_done);
  }
  {
    bool done = false;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_alloc(&exec_ctx, &usr, 1024, set_bool(&done));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(benign_done);
    GPR_ASSERT(destructive_done);
    GPR_ASSERT(done);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_free(&exec_ctx, &usr, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_buffer_pool_unref(p);
  destroy_user(&usr);
  GPR_ASSERT(benign_done);
  GPR_ASSERT(destructive_done);
}

static void test_buffer_user_stays_allocated_until_memory_released(void) {
  gpr_log(GPR_INFO,
          "** test_buffer_user_stays_allocated_until_memory_released **");
  grpc_buffer_pool *p = grpc_buffer_pool_create();
  grpc_buffer_pool_resize(p, 1024 * 1024);
  grpc_buffer_user usr;
  grpc_buffer_user_init(&usr, p);
  bool done = false;
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_alloc(&exec_ctx, &usr, 1024, NULL);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_pool_unref(p);
    grpc_buffer_user_shutdown(&exec_ctx, &usr, set_bool(&done));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(!done);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_free(&exec_ctx, &usr, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(done);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_destroy(&exec_ctx, &usr);
    grpc_exec_ctx_finish(&exec_ctx);
  }
}

static void test_pools_merged_on_buffer_user_deletion(void) {
  gpr_log(GPR_INFO, "** test_pools_merged_on_buffer_user_deletion **");
  grpc_buffer_pool *p = grpc_buffer_pool_create();
  grpc_buffer_pool_resize(p, 1024);
  for (int i = 0; i < 10; i++) {
    grpc_buffer_user usr;
    grpc_buffer_user_init(&usr, p);
    bool done = false;
    bool reclaimer_cancelled = false;
    {
      grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
      grpc_buffer_user_post_reclaimer(
          &exec_ctx, &usr, false,
          make_unused_reclaimer(set_bool(&reclaimer_cancelled)));
      grpc_exec_ctx_finish(&exec_ctx);
      GPR_ASSERT(!reclaimer_cancelled);
    }
    {
      bool allocated = false;
      grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
      grpc_buffer_user_alloc(&exec_ctx, &usr, 1024, set_bool(&allocated));
      grpc_exec_ctx_finish(&exec_ctx);
      GPR_ASSERT(allocated);
      GPR_ASSERT(!reclaimer_cancelled);
    }
    {
      grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
      grpc_buffer_user_shutdown(&exec_ctx, &usr, set_bool(&done));
      grpc_exec_ctx_finish(&exec_ctx);
      GPR_ASSERT(!done);
      GPR_ASSERT(!reclaimer_cancelled);
    }
    {
      grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
      grpc_buffer_user_free(&exec_ctx, &usr, 1024);
      grpc_exec_ctx_finish(&exec_ctx);
      GPR_ASSERT(done);
      GPR_ASSERT(reclaimer_cancelled);
    }
    {
      grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
      grpc_buffer_user_destroy(&exec_ctx, &usr);
      grpc_exec_ctx_finish(&exec_ctx);
    }
  }
  grpc_buffer_pool_unref(p);
}

static void test_reclaimers_can_be_posted_repeatedly(void) {
  gpr_log(GPR_INFO, "** test_reclaimers_can_be_posted_repeatedly **");
  grpc_buffer_pool *p = grpc_buffer_pool_create();
  grpc_buffer_pool_resize(p, 1024);
  grpc_buffer_user usr;
  grpc_buffer_user_init(&usr, p);
  {
    bool allocated = false;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_alloc(&exec_ctx, &usr, 1024, set_bool(&allocated));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(allocated);
  }
  for (int i = 0; i < 10; i++) {
    bool reclaimer_done = false;
    {
      grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
      grpc_buffer_user_post_reclaimer(
          &exec_ctx, &usr, false,
          make_reclaimer(&usr, 1024, set_bool(&reclaimer_done)));
      grpc_exec_ctx_finish(&exec_ctx);
      GPR_ASSERT(!reclaimer_done);
    }
    {
      bool allocated = false;
      grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
      grpc_buffer_user_alloc(&exec_ctx, &usr, 1024, set_bool(&allocated));
      grpc_exec_ctx_finish(&exec_ctx);
      GPR_ASSERT(allocated);
      GPR_ASSERT(reclaimer_done);
    }
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_free(&exec_ctx, &usr, 1024);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  destroy_user(&usr);
  grpc_buffer_pool_unref(p);
}

static void test_one_slice(void) {
  gpr_log(GPR_INFO, "** test_one_slice **");

  grpc_buffer_pool *p = grpc_buffer_pool_create();
  grpc_buffer_pool_resize(p, 1024);

  grpc_buffer_user usr;
  grpc_buffer_user_init(&usr, p);

  grpc_buffer_user_slice_allocator alloc;
  int num_allocs = 0;
  grpc_buffer_user_slice_allocator_init(&alloc, &usr, inc_int_cb, &num_allocs);

  gpr_slice_buffer buffer;
  gpr_slice_buffer_init(&buffer);

  {
    const int start_allocs = num_allocs;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_alloc_slices(&exec_ctx, &alloc, 1024, 1, &buffer);
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(num_allocs == start_allocs + 1);
  }

  gpr_slice_buffer_destroy(&buffer);
  destroy_user(&usr);
  grpc_buffer_pool_unref(p);
}

static void test_one_slice_deleted_late(void) {
  gpr_log(GPR_INFO, "** test_one_slice_deleted_late **");

  grpc_buffer_pool *p = grpc_buffer_pool_create();
  grpc_buffer_pool_resize(p, 1024);

  grpc_buffer_user usr;
  grpc_buffer_user_init(&usr, p);

  grpc_buffer_user_slice_allocator alloc;
  int num_allocs = 0;
  grpc_buffer_user_slice_allocator_init(&alloc, &usr, inc_int_cb, &num_allocs);

  gpr_slice_buffer buffer;
  gpr_slice_buffer_init(&buffer);

  {
    const int start_allocs = num_allocs;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_alloc_slices(&exec_ctx, &alloc, 1024, 1, &buffer);
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(num_allocs == start_allocs + 1);
  }

  bool done = false;
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_shutdown(&exec_ctx, &usr, set_bool(&done));
    grpc_exec_ctx_finish(&exec_ctx);
    GPR_ASSERT(!done);
  }

  grpc_buffer_pool_unref(p);
  gpr_slice_buffer_destroy(&buffer);
  GPR_ASSERT(done);
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_destroy(&exec_ctx, &usr);
    grpc_exec_ctx_finish(&exec_ctx);
  }
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_no_op();
  test_resize_then_destroy();
  test_buffer_user_no_op();
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
  test_buffer_user_stays_allocated_until_memory_released();
  test_pools_merged_on_buffer_user_deletion();
  test_reclaimers_can_be_posted_repeatedly();
  test_one_slice();
  test_one_slice_deleted_late();
  grpc_shutdown();
  return 0;
}
