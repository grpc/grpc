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

#include <grpc/support/port_platform.h>
#include <stdint.h>

#include <algorithm>

#include "src/core/lib/event_engine/posix_engine/timer.h"

namespace grpc_event_engine {
namespace experimental {

// Adjusts a heap so as to move a hole at position i closer to the root,
// until a suitable position is found for element t. Then, copies t into that
// position. This functor is called each time immediately after modifying a
// value in the underlying container, with the offset of the modified element as
// its argument.
void TimerHeap::AdjustUpwards(size_t i, Timer* t) {
  while (i > 0) {
    size_t parent = (i - 1) / 2;
    if (timers_[parent]->deadline <= t->deadline) break;
    timers_[i] = timers_[parent];
    timers_[i]->heap_index = i;
    i = parent;
  }
  timers_[i] = t;
  t->heap_index = i;
}

// Adjusts a heap so as to move a hole at position i farther away from the root,
// until a suitable position is found for element t.  Then, copies t into that
// position.
void TimerHeap::AdjustDownwards(size_t i, Timer* t) {
  for (;;) {
    size_t left_child = 1 + (2 * i);
    if (left_child >= timers_.size()) break;
    size_t right_child = left_child + 1;
    size_t next_i =
        right_child < timers_.size() &&
                timers_[left_child]->deadline > timers_[right_child]->deadline
            ? right_child
            : left_child;
    if (t->deadline <= timers_[next_i]->deadline) break;
    timers_[i] = timers_[next_i];
    timers_[i]->heap_index = i;
    i = next_i;
  }
  timers_[i] = t;
  t->heap_index = i;
}

void TimerHeap::NoteChangedPriority(Timer* timer) {
  uint32_t i = timer->heap_index;
  uint32_t parent = static_cast<uint32_t>((static_cast<int>(i) - 1) / 2);
  if (timers_[parent]->deadline > timer->deadline) {
    AdjustUpwards(i, timer);
  } else {
    AdjustDownwards(i, timer);
  }
}

bool TimerHeap::Add(Timer* timer) {
  timer->heap_index = timers_.size();
  timers_.push_back(timer);
  AdjustUpwards(timer->heap_index, timer);
  return timer->heap_index == 0;
}

void TimerHeap::Remove(Timer* timer) {
  uint32_t i = timer->heap_index;
  if (i == timers_.size() - 1) {
    timers_.pop_back();
    return;
  }
  timers_[i] = timers_[timers_.size() - 1];
  timers_[i]->heap_index = i;
  timers_.pop_back();
  NoteChangedPriority(timers_[i]);
}

bool TimerHeap::is_empty() { return timers_.empty(); }

Timer* TimerHeap::Top() { return timers_[0]; }

void TimerHeap::Pop() { Remove(Top()); }

}  // namespace experimental
}  // namespace grpc_event_engine
