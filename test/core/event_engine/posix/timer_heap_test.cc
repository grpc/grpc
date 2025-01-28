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

#include "src/core/lib/event_engine/posix_engine/timer_heap.h"

#include <stdint.h>
#include <stdlib.h>

#include <algorithm>
#include <utility>

#include "absl/log/check.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/event_engine/posix_engine/timer.h"
#include "src/core/util/bitset.h"

using testing::Contains;
using testing::Not;

namespace grpc_event_engine {
namespace experimental {

namespace {
int64_t RandomDeadline(void) { return rand(); }

std::vector<Timer> CreateTestElements(size_t num_elements) {
  std::vector<Timer> elems(num_elements);
  for (size_t i = 0; i < num_elements; i++) {
    elems[i].deadline = RandomDeadline();
  }
  return elems;
}

void CheckValid(TimerHeap* pq) {
  const std::vector<Timer*>& timers = pq->TestOnlyGetTimers();
  for (size_t i = 0; i < timers.size(); ++i) {
    size_t left_child = 1u + (2u * i);
    size_t right_child = left_child + 1u;
    if (left_child < timers.size()) {
      EXPECT_LE(timers[i]->deadline, timers[left_child]->deadline);
    }
    if (right_child < timers.size()) {
      EXPECT_LE(timers[i]->deadline, timers[right_child]->deadline);
    }
  }
}

TEST(TimerHeapTest, Basics) {
  TimerHeap pq;
  const size_t num_test_elements = 200;
  const size_t num_test_operations = 10000;
  size_t i;
  std::vector<Timer> test_elements = CreateTestElements(num_test_elements);
  grpc_core::BitSet<num_test_elements> inpq;

  EXPECT_TRUE(pq.is_empty());
  CheckValid(&pq);
  for (i = 0; i < num_test_elements; ++i) {
    EXPECT_THAT(pq.TestOnlyGetTimers(), Not(Contains(&test_elements[i])));
    pq.Add(&test_elements[i]);
    CheckValid(&pq);
    EXPECT_THAT(pq.TestOnlyGetTimers(), Contains(&test_elements[i]));
    inpq.set(i);
  }
  for (i = 0; i < num_test_elements; ++i) {
    // Test that check still succeeds even for element that wasn't just
    // inserted.
    EXPECT_THAT(pq.TestOnlyGetTimers(), Contains(&test_elements[i]));
  }

  EXPECT_EQ(pq.TestOnlyGetTimers().size(), num_test_elements);
  CheckValid(&pq);

  for (i = 0; i < num_test_operations; ++i) {
    size_t elem_num = static_cast<size_t>(rand()) % num_test_elements;
    Timer* el = &test_elements[elem_num];
    if (!inpq.is_set(elem_num)) {  // not in pq
      EXPECT_THAT(pq.TestOnlyGetTimers(), Not(Contains(el)));
      el->deadline = RandomDeadline();
      pq.Add(el);
      EXPECT_THAT(pq.TestOnlyGetTimers(), Contains(el));
      inpq.set(elem_num);
      CheckValid(&pq);
    } else {
      EXPECT_THAT(pq.TestOnlyGetTimers(), Contains(el));
      pq.Remove(el);
      EXPECT_THAT(pq.TestOnlyGetTimers(), Not(Contains(el)));
      inpq.clear(elem_num);
      CheckValid(&pq);
    }
  }
}

struct ElemStruct {
  Timer elem;
  bool inserted = false;
};

ElemStruct* SearchElems(std::vector<ElemStruct>& elems, bool inserted) {
  std::vector<size_t> search_order;
  for (size_t i = 0; i < elems.size(); i++) {
    search_order.push_back(i);
  }
  for (size_t i = 0; i < elems.size() * 2; i++) {
    size_t a = static_cast<size_t>(rand()) % elems.size();
    size_t b = static_cast<size_t>(rand()) % elems.size();
    std::swap(search_order[a], search_order[b]);
  }
  ElemStruct* out = nullptr;
  for (size_t i = 0; out == nullptr && i < elems.size(); i++) {
    if (elems[search_order[i]].inserted == inserted) {
      out = &elems[search_order[i]];
    }
  }
  return out;
}

// TODO(ctiller): this should be an actual fuzzer
TEST(TimerHeapTest, RandomMutations) {
  TimerHeap pq;

  static const size_t elems_size = 1000;
  std::vector<ElemStruct> elems(elems_size);
  size_t num_inserted = 0;

  for (size_t round = 0; round < 10000; round++) {
    int r = rand() % 1000;
    if (r <= 550) {
      // 55% of the time we try to add something
      ElemStruct* el = SearchElems(elems, false);
      if (el != nullptr) {
        el->elem.deadline = RandomDeadline();
        pq.Add(&el->elem);
        el->inserted = true;
        num_inserted++;
        CheckValid(&pq);
      }
    } else if (r <= 650) {
      // 10% of the time we try to remove something
      ElemStruct* el = SearchElems(elems, true);
      if (el != nullptr) {
        pq.Remove(&el->elem);
        el->inserted = false;
        num_inserted--;
        CheckValid(&pq);
      }
    } else {
      // the remaining times we pop
      if (num_inserted > 0) {
        Timer* top = pq.Top();
        pq.Pop();
        for (size_t i = 0; i < elems_size; i++) {
          if (top == &elems[i].elem) {
            CHECK(elems[i].inserted);
            elems[i].inserted = false;
          }
        }
        num_inserted--;
        CheckValid(&pq);
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
      CHECK(pq.Top()->deadline == *min_deadline);
    }
  }
}

}  // namespace
}  // namespace experimental
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
