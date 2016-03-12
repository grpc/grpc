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

#include <stdio.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include "test/core/util/test_config.h"

#define NUM_THREADS 100
static grpc_channel* channels[NUM_THREADS];
static grpc_completion_queue* queues[NUM_THREADS];

void create_loop_destroy(void* actually_an_int) {
  int thread_index = (int)(intptr_t)(actually_an_int);
  for (int i = 0; i < 10; ++i) {
    grpc_completion_queue* cq = grpc_completion_queue_create(NULL);
    grpc_channel* chan = grpc_insecure_channel_create("localhost", NULL, NULL);

    channels[thread_index] = chan;
    queues[thread_index] = cq;

    for (int j = 0; j < 10; ++j) {
      gpr_timespec later_time = GRPC_TIMEOUT_MILLIS_TO_DEADLINE(10);
      grpc_connectivity_state state =
          grpc_channel_check_connectivity_state(chan, 1);
      grpc_channel_watch_connectivity_state(chan, state, later_time, cq, NULL);
      GPR_ASSERT(grpc_completion_queue_next(cq,
                                            GRPC_TIMEOUT_SECONDS_TO_DEADLINE(3),
                                            NULL).type == GRPC_OP_COMPLETE);
    }
    grpc_channel_destroy(channels[thread_index]);
    grpc_completion_queue_destroy(queues[thread_index]);
  }
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  gpr_thd_id threads[NUM_THREADS];
  for (intptr_t i = 0; i < NUM_THREADS; ++i) {
    gpr_thd_options options = gpr_thd_options_default();
    gpr_thd_options_set_joinable(&options);
    gpr_thd_new(&threads[i], create_loop_destroy, (void*)i, &options);
  }
  for (int i = 0; i < NUM_THREADS; ++i) {
    gpr_thd_join(threads[i]);
  }
  grpc_shutdown();
  return 0;
}
