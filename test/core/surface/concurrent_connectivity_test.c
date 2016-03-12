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
  int thread_index = (int)(actually_an_int);
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
      GPR_ASSERT(grpc_completion_queue_next(
                     cq, GRPC_TIMEOUT_SECONDS_TO_DEADLINE(3), NULL)
                     .type == GRPC_OP_COMPLETE);
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
