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

#include "src/core/iomgr/alarm_heap.h"

#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "test/core/util/test_config.h"

static gpr_timespec random_deadline(void) {
  gpr_timespec ts;
  ts.tv_sec = rand();
  ts.tv_nsec = rand();
  ts.clock_type = GPR_CLOCK_REALTIME;
  return ts;
}

static grpc_alarm *create_test_elements(int num_elements) {
  grpc_alarm *elems = gpr_malloc(num_elements * sizeof(grpc_alarm));
  int i;
  for (i = 0; i < num_elements; i++) {
    elems[i].deadline = random_deadline();
  }
  return elems;
}

static int cmp_elem(const void *a, const void *b) {
  int i = *(const int *)a;
  int j = *(const int *)b;
  return i - j;
}

static int *all_top(grpc_alarm_heap *pq, int *n) {
  int *vec = NULL;
  int *need_to_check_children;
  int num_need_to_check_children = 0;

  *n = 0;
  if (pq->alarm_count == 0) return vec;
  need_to_check_children = gpr_malloc(pq->alarm_count * sizeof(int));
  need_to_check_children[num_need_to_check_children++] = 0;
  vec = gpr_malloc(pq->alarm_count * sizeof(int));
  while (num_need_to_check_children > 0) {
    int ind = need_to_check_children[0];
    int leftchild, rightchild;
    num_need_to_check_children--;
    memmove(need_to_check_children, need_to_check_children + 1,
            num_need_to_check_children * sizeof(int));
    vec[(*n)++] = ind;
    leftchild = 1 + 2 * ind;
    if (leftchild < pq->alarm_count) {
      if (gpr_time_cmp(pq->alarms[leftchild]->deadline,
                       pq->alarms[ind]->deadline) >= 0) {
        need_to_check_children[num_need_to_check_children++] = leftchild;
      }
      rightchild = leftchild + 1;
      if (rightchild < pq->alarm_count &&
          gpr_time_cmp(pq->alarms[rightchild]->deadline,
                       pq->alarms[ind]->deadline) >= 0) {
        need_to_check_children[num_need_to_check_children++] = rightchild;
      }
    }
  }

  gpr_free(need_to_check_children);

  return vec;
}

static void check_pq_top(grpc_alarm *elements, grpc_alarm_heap *pq,
                         gpr_uint8 *inpq, int num_elements) {
  gpr_timespec max_deadline = gpr_inf_past(GPR_CLOCK_REALTIME);
  int *max_deadline_indices = gpr_malloc(num_elements * sizeof(int));
  int *top_elements;
  int num_max_deadline_indices = 0;
  int num_top_elements;
  int i;
  for (i = 0; i < num_elements; ++i) {
    if (inpq[i] && gpr_time_cmp(elements[i].deadline, max_deadline) >= 0) {
      if (gpr_time_cmp(elements[i].deadline, max_deadline) > 0) {
        num_max_deadline_indices = 0;
        max_deadline = elements[i].deadline;
      }
      max_deadline_indices[num_max_deadline_indices++] = elements[i].heap_index;
    }
  }
  qsort(max_deadline_indices, num_max_deadline_indices, sizeof(int), cmp_elem);
  top_elements = all_top(pq, &num_top_elements);
  GPR_ASSERT(num_top_elements == num_max_deadline_indices);
  for (i = 0; i < num_top_elements; i++) {
    GPR_ASSERT(max_deadline_indices[i] == top_elements[i]);
  }
  gpr_free(max_deadline_indices);
  gpr_free(top_elements);
}

static int contains(grpc_alarm_heap *pq, grpc_alarm *el) {
  int i;
  for (i = 0; i < pq->alarm_count; i++) {
    if (pq->alarms[i] == el) return 1;
  }
  return 0;
}

static void check_valid(grpc_alarm_heap *pq) {
  int i;
  for (i = 0; i < pq->alarm_count; ++i) {
    int left_child = 1 + 2 * i;
    int right_child = left_child + 1;
    if (left_child < pq->alarm_count) {
      GPR_ASSERT(gpr_time_cmp(pq->alarms[i]->deadline,
                              pq->alarms[left_child]->deadline) >= 0);
    }
    if (right_child < pq->alarm_count) {
      GPR_ASSERT(gpr_time_cmp(pq->alarms[i]->deadline,
                              pq->alarms[right_child]->deadline) >= 0);
    }
  }
}

static void test1(void) {
  grpc_alarm_heap pq;
  const int num_test_elements = 200;
  const int num_test_operations = 10000;
  int i;
  grpc_alarm *test_elements = create_test_elements(num_test_elements);
  gpr_uint8 *inpq = gpr_malloc(num_test_elements);

  grpc_alarm_heap_init(&pq);
  memset(inpq, 0, num_test_elements);
  GPR_ASSERT(grpc_alarm_heap_is_empty(&pq));
  check_valid(&pq);
  for (i = 0; i < num_test_elements; ++i) {
    GPR_ASSERT(!contains(&pq, &test_elements[i]));
    grpc_alarm_heap_add(&pq, &test_elements[i]);
    check_valid(&pq);
    GPR_ASSERT(contains(&pq, &test_elements[i]));
    inpq[i] = 1;
    check_pq_top(test_elements, &pq, inpq, num_test_elements);
  }
  for (i = 0; i < num_test_elements; ++i) {
    /* Test that check still succeeds even for element that wasn't just
       inserted. */
    GPR_ASSERT(contains(&pq, &test_elements[i]));
  }

  GPR_ASSERT(pq.alarm_count == num_test_elements);

  check_pq_top(test_elements, &pq, inpq, num_test_elements);

  for (i = 0; i < num_test_operations; ++i) {
    int elem_num = rand() % num_test_elements;
    grpc_alarm *el = &test_elements[elem_num];
    if (!inpq[elem_num]) { /* not in pq */
      GPR_ASSERT(!contains(&pq, el));
      el->deadline = random_deadline();
      grpc_alarm_heap_add(&pq, el);
      GPR_ASSERT(contains(&pq, el));
      inpq[elem_num] = 1;
      check_pq_top(test_elements, &pq, inpq, num_test_elements);
      check_valid(&pq);
    } else {
      GPR_ASSERT(contains(&pq, el));
      grpc_alarm_heap_remove(&pq, el);
      GPR_ASSERT(!contains(&pq, el));
      inpq[elem_num] = 0;
      check_pq_top(test_elements, &pq, inpq, num_test_elements);
      check_valid(&pq);
    }
  }

  grpc_alarm_heap_destroy(&pq);
  gpr_free(test_elements);
  gpr_free(inpq);
}

static void shrink_test(void) {
  grpc_alarm_heap pq;
  int i;
  int expected_size;

  /* A large random number to allow for multiple shrinkages, at least 512. */
  const int num_elements = rand() % 2000 + 512;

  grpc_alarm_heap_init(&pq);

  /* Create a priority queue with many elements.  Make sure the Size() is
     correct. */
  for (i = 0; i < num_elements; ++i) {
    GPR_ASSERT(i == pq.alarm_count);
    grpc_alarm_heap_add(&pq, create_test_elements(1));
  }
  GPR_ASSERT(num_elements == pq.alarm_count);

  /* Remove elements until the Size is 1/4 the original size. */
  while (pq.alarm_count > num_elements / 4) {
    grpc_alarm *const te = pq.alarms[pq.alarm_count - 1];
    grpc_alarm_heap_remove(&pq, te);
    gpr_free(te);
  }
  GPR_ASSERT(num_elements / 4 == pq.alarm_count);

  /* Expect that Capacity is in the right range:
     Size * 2 <= Capacity <= Size * 4 */
  GPR_ASSERT(pq.alarm_count * 2 <= pq.alarm_capacity);
  GPR_ASSERT(pq.alarm_capacity <= pq.alarm_count * 4);
  check_valid(&pq);

  /* Remove the rest of the elements.  Check that the Capacity is not more than
     4 times the Size and not less than 2 times, but never goes below 16. */
  expected_size = pq.alarm_count;
  while (pq.alarm_count > 0) {
    const int which = rand() % pq.alarm_count;
    grpc_alarm *te = pq.alarms[which];
    grpc_alarm_heap_remove(&pq, te);
    gpr_free(te);
    expected_size--;
    GPR_ASSERT(expected_size == pq.alarm_count);
    GPR_ASSERT(pq.alarm_count * 2 <= pq.alarm_capacity);
    if (pq.alarm_count >= 8) {
      GPR_ASSERT(pq.alarm_capacity <= pq.alarm_count * 4);
    } else {
      GPR_ASSERT(16 <= pq.alarm_capacity);
    }
    check_valid(&pq);
  }

  GPR_ASSERT(0 == pq.alarm_count);
  GPR_ASSERT(pq.alarm_capacity >= 16 && pq.alarm_capacity < 32);

  grpc_alarm_heap_destroy(&pq);
}

int main(int argc, char **argv) {
  int i;

  grpc_test_init(argc, argv);

  for (i = 0; i < 5; i++) {
    test1();
    shrink_test();
  }

  return 0;
}
