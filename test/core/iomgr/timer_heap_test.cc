//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "src/core/lib/iomgr/timer_heap.h"

#include <grpc/support/alloc.h>
#include <stdlib.h>
#include <string.h>

#include "absl/log/log.h"
#include "gtest/gtest.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/util/crash.h"
#include "src/core/util/useful.h"
#include "test/core/test_util/test_config.h"

static gpr_atm random_deadline(void) { return rand(); }

static grpc_timer* create_test_elements(size_t num_elements) {
  grpc_timer* elems =
      static_cast<grpc_timer*>(gpr_malloc(num_elements * sizeof(grpc_timer)));
  size_t i;
  for (i = 0; i < num_elements; i++) {
    elems[i].deadline = random_deadline();
  }
  return elems;
}

static int contains(grpc_timer_heap* pq, grpc_timer* el) {
  size_t i;
  for (i = 0; i < pq->timer_count; i++) {
    if (pq->timers[i] == el) return 1;
  }
  return 0;
}

static void check_valid(grpc_timer_heap* pq) {
  size_t i;
  for (i = 0; i < pq->timer_count; ++i) {
    size_t left_child = 1u + (2u * i);
    size_t right_child = left_child + 1u;
    if (left_child < pq->timer_count) {
      ASSERT_LE(pq->timers[i]->deadline, pq->timers[left_child]->deadline);
    }
    if (right_child < pq->timer_count) {
      ASSERT_LE(pq->timers[i]->deadline, pq->timers[right_child]->deadline);
    }
  }
}

//******************************************************************************
// test1
//

static void test1(void) {
  grpc_timer_heap pq;
  const size_t num_test_elements = 200;
  const size_t num_test_operations = 10000;
  size_t i;
  grpc_timer* test_elements = create_test_elements(num_test_elements);
  uint8_t* inpq = static_cast<uint8_t*>(gpr_malloc(num_test_elements));

  LOG(INFO) << "test1";

  grpc_timer_heap_init(&pq);
  memset(inpq, 0, num_test_elements);
  ASSERT_TRUE(grpc_timer_heap_is_empty(&pq));
  check_valid(&pq);
  for (i = 0; i < num_test_elements; ++i) {
    ASSERT_FALSE(contains(&pq, &test_elements[i]));
    grpc_timer_heap_add(&pq, &test_elements[i]);
    check_valid(&pq);
    ASSERT_TRUE(contains(&pq, &test_elements[i]));
    inpq[i] = 1;
  }
  for (i = 0; i < num_test_elements; ++i) {
    // Test that check still succeeds even for element that wasn't just
    // inserted.
    ASSERT_TRUE(contains(&pq, &test_elements[i]));
  }

  ASSERT_EQ(pq.timer_count, num_test_elements);

  check_valid(&pq);

  for (i = 0; i < num_test_operations; ++i) {
    size_t elem_num = static_cast<size_t>(rand()) % num_test_elements;
    grpc_timer* el = &test_elements[elem_num];
    if (!inpq[elem_num]) {  // not in pq
      ASSERT_FALSE(contains(&pq, el));
      el->deadline = random_deadline();
      grpc_timer_heap_add(&pq, el);
      ASSERT_TRUE(contains(&pq, el));
      inpq[elem_num] = 1;
      check_valid(&pq);
    } else {
      ASSERT_TRUE(contains(&pq, el));
      grpc_timer_heap_remove(&pq, el);
      ASSERT_FALSE(contains(&pq, el));
      inpq[elem_num] = 0;
      check_valid(&pq);
    }
  }

  grpc_timer_heap_destroy(&pq);
  gpr_free(test_elements);
  gpr_free(inpq);
}

//******************************************************************************
// test2
//

typedef struct {
  grpc_timer elem;
  bool inserted;
} elem_struct;

static elem_struct* search_elems(elem_struct* elems, size_t count,
                                 bool inserted) {
  size_t* search_order =
      static_cast<size_t*>(gpr_malloc(count * sizeof(*search_order)));
  for (size_t i = 0; i < count; i++) {
    search_order[i] = i;
  }
  for (size_t i = 0; i < count * 2; i++) {
    size_t a = static_cast<size_t>(rand()) % count;
    size_t b = static_cast<size_t>(rand()) % count;
    std::swap(search_order[a], search_order[b]);
  }
  elem_struct* out = nullptr;
  for (size_t i = 0; out == nullptr && i < count; i++) {
    if (elems[search_order[i]].inserted == inserted) {
      out = &elems[search_order[i]];
    }
  }
  gpr_free(search_order);
  return out;
}

static void test2(void) {
  LOG(INFO) << "test2";

  grpc_timer_heap pq;

  static const size_t elems_size = 1000;
  elem_struct* elems =
      static_cast<elem_struct*>(gpr_malloc(elems_size * sizeof(elem_struct)));
  size_t num_inserted = 0;

  grpc_timer_heap_init(&pq);
  memset(elems, 0, elems_size * sizeof(elems[0]));

  for (size_t round = 0; round < 10000; round++) {
    int r = rand() % 1000;
    if (r <= 550) {
      // 55% of the time we try to add something
      elem_struct* el = search_elems(elems, elems_size, false);
      if (el != nullptr) {
        el->elem.deadline = random_deadline();
        grpc_timer_heap_add(&pq, &el->elem);
        el->inserted = true;
        num_inserted++;
        check_valid(&pq);
      }
    } else if (r <= 650) {
      // 10% of the time we try to remove something
      elem_struct* el = search_elems(elems, elems_size, true);
      if (el != nullptr) {
        grpc_timer_heap_remove(&pq, &el->elem);
        el->inserted = false;
        num_inserted--;
        check_valid(&pq);
      }
    } else {
      // the remaining times we pop
      if (num_inserted > 0) {
        grpc_timer* top = grpc_timer_heap_top(&pq);
        grpc_timer_heap_pop(&pq);
        for (size_t i = 0; i < elems_size; i++) {
          if (top == &elems[i].elem) {
            ASSERT_TRUE(elems[i].inserted);
            elems[i].inserted = false;
          }
        }
        num_inserted--;
        check_valid(&pq);
      }
    }

    if (num_inserted) {
      int64_t* min_deadline = nullptr;
      for (size_t i = 0; i < elems_size; i++) {
        if (elems[i].inserted) {
          if (min_deadline == nullptr) {
            min_deadline = &elems[i].elem.deadline;
          } else {
            if (elems[i].elem.deadline < *min_deadline) {
              min_deadline = &elems[i].elem.deadline;
            }
          }
        }
      }
      ASSERT_EQ(grpc_timer_heap_top(&pq)->deadline, *min_deadline);
    }
  }

  grpc_timer_heap_destroy(&pq);
  gpr_free(elems);
}

static void shrink_test(void) {
  LOG(INFO) << "shrink_test";

  grpc_timer_heap pq;
  size_t i;
  size_t expected_size;

  // A large random number to allow for multiple shrinkages, at least 512.
  const size_t num_elements = (static_cast<size_t>(rand()) % 2000) + 512;

  grpc_timer_heap_init(&pq);

  // Create a priority queue with many elements.  Make sure the Size() is
  // correct.
  for (i = 0; i < num_elements; ++i) {
    ASSERT_EQ(i, pq.timer_count);
    grpc_timer_heap_add(&pq, create_test_elements(1));
  }
  ASSERT_EQ(num_elements, pq.timer_count);

  // Remove elements until the Size is 1/4 the original size.
  while (pq.timer_count > num_elements / 4) {
    grpc_timer* const te = pq.timers[pq.timer_count - 1];
    grpc_timer_heap_remove(&pq, te);
    gpr_free(te);
  }
  ASSERT_EQ(num_elements / 4, pq.timer_count);

  // Expect that Capacity is in the right range:
  // Size * 2 <= Capacity <= Size * 4
  ASSERT_LE(pq.timer_count * 2, pq.timer_capacity);
  ASSERT_LE(pq.timer_capacity, pq.timer_count * 4);
  check_valid(&pq);

  // Remove the rest of the elements.  Check that the Capacity is not more than
  // 4 times the Size and not less than 2 times, but never goes below 16.
  expected_size = pq.timer_count;
  while (pq.timer_count > 0) {
    const size_t which = static_cast<size_t>(rand()) % pq.timer_count;
    grpc_timer* te = pq.timers[which];
    grpc_timer_heap_remove(&pq, te);
    gpr_free(te);
    expected_size--;
    ASSERT_EQ(expected_size, pq.timer_count);
    ASSERT_LE(pq.timer_count * 2, pq.timer_capacity);
    if (pq.timer_count >= 8) {
      ASSERT_LE(pq.timer_capacity, pq.timer_count * 4);
    } else {
      ASSERT_LE(16, pq.timer_capacity);
    }
    check_valid(&pq);
  }

  ASSERT_EQ(pq.timer_count, 0);
  ASSERT_GE(pq.timer_capacity, 16);
  ASSERT_LT(pq.timer_capacity, 32);

  grpc_timer_heap_destroy(&pq);
}

TEST(TimerHeapTest, MainTest) {
  for (int i = 0; i < 5; i++) {
    test1();
    test2();
    shrink_test();
  }
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
