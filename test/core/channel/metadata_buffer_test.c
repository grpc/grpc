/*
 *
 * Copyright 2015, Google Inc.
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

#include "src/core/channel/metadata_buffer.h"
#include "src/core/support/string.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "test/core/util/test_config.h"

#include <string.h>
#include <stdio.h>

/* construct a buffer with some prefix followed by an integer converted to
   a string */
static gpr_slice construct_buffer(size_t prefix_length, size_t index) {
  gpr_slice buffer = gpr_slice_malloc(prefix_length + GPR_LTOA_MIN_BUFSIZE);
  memset(GPR_SLICE_START_PTR(buffer), 'a', prefix_length);
  GPR_SLICE_SET_LENGTH(
      buffer,
      prefix_length +
          gpr_ltoa(index, (char *)GPR_SLICE_START_PTR(buffer) + prefix_length));
  return buffer;
}

static void do_nothing(void *ignored, grpc_op_error also_ignored) {}

/* we need a fake channel & call stack, which is defined here */

/* a fake channel needs to track some information about the test */
typedef struct {
  size_t key_prefix_len;
  size_t value_prefix_len;
} channel_data;

static void fail_call_op(grpc_call_element *elem, grpc_call_element *from_elem,
                         grpc_call_op *op) {
  abort();
}

/* verify that the metadata passed on during flush is the same as we expect */
static void expect_call_op(grpc_call_element *elem,
                           grpc_call_element *from_elem, grpc_call_op *op) {
  size_t *n = elem->call_data;
  channel_data *cd = elem->channel_data;
  gpr_slice key = construct_buffer(cd->key_prefix_len, *n);
  gpr_slice value = construct_buffer(cd->value_prefix_len, *n);

  GPR_ASSERT(op->type == GRPC_SEND_METADATA);
  GPR_ASSERT(op->dir == GRPC_CALL_DOWN);
  GPR_ASSERT(op->flags == *n);
  GPR_ASSERT(op->done_cb == do_nothing);
  GPR_ASSERT(op->user_data == (void *)(gpr_uintptr) * n);
  GPR_ASSERT(0 == gpr_slice_cmp(op->data.metadata->key->slice, key));
  GPR_ASSERT(0 == gpr_slice_cmp(op->data.metadata->value->slice, value));

  ++*n;

  gpr_slice_unref(key);
  gpr_slice_unref(value);
  grpc_mdelem_unref(op->data.metadata);
}

static void fail_channel_op(grpc_channel_element *elem,
                            grpc_channel_element *from_elem,
                            grpc_channel_op *op) {
  abort();
}

static void init_call_elem(grpc_call_element *elem,
                           const void *transport_server_data) {
  *(size_t *)elem->call_data = 0;
}

static void destroy_call_elem(grpc_call_element *elem) {}

static void init_channel_elem(grpc_channel_element *elem,
                              const grpc_channel_args *args, grpc_mdctx *mdctx,
                              int is_first, int is_last) {}

static void destroy_channel_elem(grpc_channel_element *elem) {}

static const grpc_channel_filter top_filter = {
    fail_call_op,      fail_channel_op,      sizeof(size_t),
    init_call_elem,    destroy_call_elem,    sizeof(channel_data),
    init_channel_elem, destroy_channel_elem, "top_filter"};

static const grpc_channel_filter bottom_filter = {
    expect_call_op,    fail_channel_op,      sizeof(size_t),
    init_call_elem,    destroy_call_elem,    sizeof(channel_data),
    init_channel_elem, destroy_channel_elem, "bottom_filter"};

static const grpc_channel_filter *filters[2] = {&top_filter, &bottom_filter};

/* run a test with differently sized keys, and values, some number of times. */
static void test_case(size_t key_prefix_len, size_t value_prefix_len,
                      size_t num_calls) {
  size_t i;
  size_t got_calls;
  grpc_metadata_buffer buffer;
  grpc_channel_stack *stk;
  grpc_call_stack *call;
  grpc_mdctx *mdctx;

  gpr_log(GPR_INFO, "Test %d calls, {key,value}_prefix_len = {%d, %d}",
          (int)num_calls, (int)key_prefix_len, (int)value_prefix_len);

  mdctx = grpc_mdctx_create();

  grpc_metadata_buffer_init(&buffer);

  /* queue metadata elements */
  for (i = 0; i < num_calls; i++) {
    grpc_call_op op;
    gpr_slice key = construct_buffer(key_prefix_len, i);
    gpr_slice value = construct_buffer(value_prefix_len, i);

    op.type = GRPC_SEND_METADATA;
    op.dir = GRPC_CALL_DOWN;
    op.flags = i;
    op.data.metadata = grpc_mdelem_from_slices(mdctx, key, value);
    op.done_cb = do_nothing;
    op.user_data = (void *)(gpr_uintptr) i;

    grpc_metadata_buffer_queue(&buffer, &op);
  }

  /* construct a test channel, call stack */
  stk = gpr_malloc(grpc_channel_stack_size(filters, 2));
  grpc_channel_stack_init(filters, 2, NULL, mdctx, stk);

  for (i = 0; i < 2; i++) {
    channel_data *cd =
        (channel_data *)grpc_channel_stack_element(stk, i)->channel_data;
    cd->key_prefix_len = key_prefix_len;
    cd->value_prefix_len = value_prefix_len;
  }

  call = gpr_malloc(stk->call_stack_size);
  grpc_call_stack_init(stk, NULL, call);

  /* flush out metadata, verifying each element (see expect_call_op) */
  grpc_metadata_buffer_flush(&buffer, grpc_call_stack_element(call, 0));

  /* verify expect_call_op was called an appropriate number of times */
  got_calls = *(size_t *)grpc_call_stack_element(call, 1)->call_data;
  GPR_ASSERT(num_calls == got_calls);

  /* clean up the things */
  grpc_call_stack_destroy(call);
  gpr_free(call);
  grpc_channel_stack_destroy(stk);
  gpr_free(stk);

  grpc_metadata_buffer_destroy(&buffer, GRPC_OP_OK);
  grpc_mdctx_unref(mdctx);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_case(0, 0, 0);
  test_case(0, 0, 1);
  test_case(0, 0, 2);
  test_case(0, 0, 10000);
  test_case(10, 10, 1);
  test_case(10, 10, 2);
  test_case(10, 10, 10000);
  test_case(100, 100, 1);
  test_case(100, 100, 2);
  test_case(100, 100, 10000);
  return 0;
}
