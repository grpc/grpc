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

#include "src/core/lib/profiling/timers.h"
#include <stdlib.h>
#include "test/core/util/test_config.h"

void test_log_events(size_t num_seqs) {
  size_t start = 0;
  size_t *state;
  state = calloc(num_seqs, sizeof(state[0]));
  while (start < num_seqs) {
    size_t i;
    size_t row;
    if (state[start] == 3) { /* Already done with this posn */
      start++;
      continue;
    }

    row = (size_t)rand() % 10; /* how many in a row */
    for (i = start; (i < start + row) && (i < num_seqs); i++) {
      size_t j;
      size_t advance = 1 + (size_t)rand() % 3; /* how many to advance by */
      for (j = 0; j < advance; j++) {
        switch (state[i]) {
          case 0:
            GPR_TIMER_MARK(STATE_0, i);
            state[i]++;
            break;
          case 1:
            GPR_TIMER_MARK(STATE_1, i);
            state[i]++;
            break;
          case 2:
            GPR_TIMER_MARK(STATE_2, i);
            state[i]++;
            break;
          case 3:
            break;
        }
      }
    }
  }
  free(state);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  gpr_timers_global_init();
  test_log_events(1000000);
  gpr_timers_global_destroy();
  return 0;
}
