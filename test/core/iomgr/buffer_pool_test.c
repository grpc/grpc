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

#include <grpc/support/log.h>

#include "test/core/util/test_config.h"

static void set_bool_cb(grpc_exec_ctx *exec_ctx, void *a, grpc_error *error) {
  *(bool *)a = true;
}
grpc_closure *set_bool(bool *p) { return grpc_closure_create(set_bool_cb, p); }

static void destroy_user(grpc_buffer_user *usr) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  bool done = false;
  grpc_buffer_user_destroy(&exec_ctx, usr, set_bool(&done));
  grpc_exec_ctx_finish(&exec_ctx);
  GPR_ASSERT(done);
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

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_no_op();
  test_resize_then_destroy();
  test_buffer_user_no_op();
  test_instant_alloc_then_free();
  test_instant_alloc_free_pair();
  grpc_shutdown();
  return 0;
}
