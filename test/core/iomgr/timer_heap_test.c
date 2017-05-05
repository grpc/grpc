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

#include "src/core/lib/iomgr/port.h"

// This test only works with the generic timer implementation
#ifdef GRPC_TIMER_USE_GENERIC

#include "src/core/lib/iomgr/timer_heap.h"

#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

#include "test/core/util/test_config.h"

static gpr_atm random_deadline(void) { return rand(); }

static grpc_timer *create_test_elements(size_t num_elements) {
  grpc_timer *elems = gpr_malloc(num_elements * sizeof(grpc_timer));
  size_t i;
  for (i = 0; i < num_elements; i++) {
    elems[i].deadline = random_deadline();
  }
  return elems;
}

static int contains(grpc_timer_heap *pq, grpc_timer *el) {
  size_t i;
  for (i = 0; i < pq->timer_count; i++) {
    if (pq->timers[i] == el) return 1;
  }
  return 0;
}

static void check_valid(grpc_timer_heap *pq) {
  size_t i;
  for (i = 0; i < pq->timer_count; ++i) {
    size_t left_child = 1u + 2u * i;
    size_t right_child = left_child + 1u;
    if (left_child < pq->timer_count) {
      GPR_ASSERT(pq->timers[i]->deadline <= pq->timers[left_child]->deadline);
    }
    if (right_child < pq->timer_count) {
      GPR_ASSERT(pq->timers[i]->deadline <= pq->timers[right_child]->deadline);
    }
  }
}

/*******************************************************************************
 * test1
 */

static void test1(void) {
  grpc_timer_heap pq;
  const size_t num_test_elements = 200;
  const size_t num_test_operations = 10000;
  size_t i;
  grpc_timer *test_elements = create_test_elements(num_test_elements);
  uint8_t *inpq = gpr_malloc(num_test_elements);

  gpr_log(GPR_INFO, "test1");

  grpc_timer_heap_init(&pq);
  memset(inpq, 0, num_test_elements);
  GPR_ASSERT(grpc_timer_heap_is_empty(&pq));
  check_valid(&pq);
  for (i = 0; i < num_test_elements; ++i) {
    GPR_ASSERT(!contains(&pq, &test_elements[i]));
    grpc_timer_heap_add(&pq, &test_elements[i]);
    check_valid(&pq);
    GPR_ASSERT(contains(&pq, &test_elements[i]));
    inpq[i] = 1;
  }
  for (i = 0; i < num_test_elements; ++i) {
    /* Test that check still succeeds even for element that wasn't just
       inserted. */
    GPR_ASSERT(contains(&pq, &test_elements[i]));
  }

  GPR_ASSERT(pq.timer_count == num_test_elements);

  check_valid(&pq);

  for (i = 0; i < num_test_operations; ++i) {
    size_t elem_num = (size_t)rand() % num_test_elements;
    grpc_timer *el = &test_elements[elem_num];
    if (!inpq[elem_num]) { /* not in pq */
      GPR_ASSERT(!contains(&pq, el));
      el->deadline = random_deadline();
      grpc_timer_heap_add(&pq, el);
      GPR_ASSERT(contains(&pq, el));
      inpq[elem_num] = 1;
      check_valid(&pq);
    } else {
      GPR_ASSERT(contains(&pq, el));
      grpc_timer_heap_remove(&pq, el);
      GPR_ASSERT(!contains(&pq, el));
      inpq[elem_num] = 0;
      check_valid(&pq);
    }
  }

  grpc_timer_heap_destroy(&pq);
  gpr_free(test_elements);
  gpr_free(inpq);
}

/*******************************************************************************
 * test2
 */

typedef struct {
  grpc_timer elem;
  bool inserted;
} elem_struct;

static elem_struct *search_elems(elem_struct *elems, size_t count,
                                 bool inserted) {
  size_t *search_order = gpr_malloc(count * sizeof(*search_order));
  for (size_t i = 0; i < count; i++) {
    search_order[i] = i;
  }
  for (size_t i = 0; i < count * 2; i++) {
    size_t a = (size_t)rand() % count;
    size_t b = (size_t)rand() % count;
    GPR_SWAP(size_t, search_order[a], search_order[b]);
  }
  elem_struct *out = NULL;
  for (size_t i = 0; out == NULL && i < count; i++) {
    if (elems[search_order[i]].inserted == inserted) {
      out = &elems[search_order[i]];
    }
  }
  gpr_free(search_order);
  return out;
}

static void test2(void) {
  gpr_log(GPR_INFO, "test2");

  grpc_timer_heap pq;

  static const size_t elems_size = 1000;
  elem_struct *elems = gpr_malloc(elems_size * sizeof(elem_struct));
  size_t num_inserted = 0;

  grpc_timer_heap_init(&pq);
  memset(elems, 0, elems_size);

  for (size_t round = 0; round < 10000; round++) {
    int r = rand() % 1000;
    if (r <= 550) {
      /* 55% of the time we try to add something */
      elem_struct *el = search_elems(elems, GPR_ARRAY_SIZE(elems), false);
      if (el != NULL) {
        el->elem.deadline = random_deadline();
        grpc_timer_heap_add(&pq, &el->elem);
        el->inserted = true;
        num_inserted++;
        check_valid(&pq);
      }
    } else if (r <= 650) {
      /* 10% of the time we try to remove something */
      elem_struct *el = search_elems(elems, GPR_ARRAY_SIZE(elems), true);
      if (el != NULL) {
        grpc_timer_heap_remove(&pq, &el->elem);
        el->inserted = false;
        num_inserted--;
        check_valid(&pq);
      }
    } else {
      /* the remaining times we pop */
      if (num_inserted > 0) {
        grpc_timer *top = grpc_timer_heap_top(&pq);
        grpc_timer_heap_pop(&pq);
        for (size_t i = 0; i < elems_size; i++) {
          if (top == &elems[i].elem) {
            GPR_ASSERT(elems[i].inserted);
            elems[i].inserted = false;
          }
        }
        num_inserted--;
        check_valid(&pq);
      }
    }

    if (num_inserted) {
      gpr_atm *min_deadline = NULL;
      for (size_t i = 0; i < elems_size; i++) {
        if (elems[i].inserted) {
          if (min_deadline == NULL) {
            min_deadline = &elems[i].elem.deadline;
          } else {
            if (elems[i].elem.deadline < *min_deadline) {
              min_deadline = &elems[i].elem.deadline;
            }
          }
        }
      }
      GPR_ASSERT(grpc_timer_heap_top(&pq)->deadline == *min_deadline);
    }
  }

  grpc_timer_heap_destroy(&pq);
  gpr_free(elems);
}

static void shrink_test(void) {
  gpr_log(GPR_INFO, "shrink_test");

  grpc_timer_heap pq;
  size_t i;
  size_t expected_size;

  /* A large random number to allow for multiple shrinkages, at least 512. */
  const size_t num_elements = (size_t)rand() % 2000 + 512;

  grpc_timer_heap_init(&pq);

  /* Create a priority queue with many elements.  Make sure the Size() is
     correct. */
  for (i = 0; i < num_elements; ++i) {
    GPR_ASSERT(i == pq.timer_count);
    grpc_timer_heap_add(&pq, create_test_elements(1));
  }
  GPR_ASSERT(num_elements == pq.timer_count);

  /* Remove elements until the Size is 1/4 the original size. */
  while (pq.timer_count > num_elements / 4) {
    grpc_timer *const te = pq.timers[pq.timer_count - 1];
    grpc_timer_heap_remove(&pq, te);
    gpr_free(te);
  }
  GPR_ASSERT(num_elements / 4 == pq.timer_count);

  /* Expect that Capacity is in the right range:
     Size * 2 <= Capacity <= Size * 4 */
  GPR_ASSERT(pq.timer_count * 2 <= pq.timer_capacity);
  GPR_ASSERT(pq.timer_capacity <= pq.timer_count * 4);
  check_valid(&pq);

  /* Remove the rest of the elements.  Check that the Capacity is not more than
     4 times the Size and not less than 2 times, but never goes below 16. */
  expected_size = pq.timer_count;
  while (pq.timer_count > 0) {
    const size_t which = (size_t)rand() % pq.timer_count;
    grpc_timer *te = pq.timers[which];
    grpc_timer_heap_remove(&pq, te);
    gpr_free(te);
    expected_size--;
    GPR_ASSERT(expected_size == pq.timer_count);
    GPR_ASSERT(pq.timer_count * 2 <= pq.timer_capacity);
    if (pq.timer_count >= 8) {
      GPR_ASSERT(pq.timer_capacity <= pq.timer_count * 4);
    } else {
      GPR_ASSERT(16 <= pq.timer_capacity);
    }
    check_valid(&pq);
  }

  GPR_ASSERT(0 == pq.timer_count);
  GPR_ASSERT(pq.timer_capacity >= 16 && pq.timer_capacity < 32);

  grpc_timer_heap_destroy(&pq);
}

int main(int argc, char **argv) {
  int i;

  grpc_test_init(argc, argv);

  for (i = 0; i < 5; i++) {
    test1();
    test2();
    shrink_test();
  }

  return 0;
}

#else /* GRPC_TIMER_USE_GENERIC */

int main(int argc, char **argv) { return 1; }

#endif /* GRPC_TIMER_USE_GENERIC */
