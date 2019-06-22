/*
 *
 * Copyright 2019 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/lib/iomgr/executor/mpmcqueue.h"

#include <grpc/grpc.h>

#include "src/core/lib/gprpp/thd.h"
#include "test/core/util/test_config.h"

#define THREAD_LARGE_ITERATION 10000

// Testing items for queue
struct WorkItem {
  int index;
  bool done;

  WorkItem(int i) : index(i) { done = false; }
};

// Thread for put items into queue
class ProducerThread {
 public:
  ProducerThread(grpc_core::InfLenFIFOQueue* queue, int start_index,
                 int num_items)
      : start_index_(start_index), num_items_(num_items), queue_(queue) {
    items_ = nullptr;
    thd_ = grpc_core::Thread(
        "mpmcq_test_put_thd",
        [](void* th) { static_cast<ProducerThread*>(th)->Run(); }, this);
  }
  ~ProducerThread() {
    for (int i = 0; i < num_items_; ++i) {
      GPR_ASSERT(items_[i]->done);
      grpc_core::Delete(items_[i]);
    }
    gpr_free(items_);
  }

  void Start() { thd_.Start(); }
  void Join() { thd_.Join(); }

 private:
  void Run() {
    items_ =
        static_cast<WorkItem**>(gpr_zalloc(num_items_ * sizeof(WorkItem*)));
    for (int i = 0; i < num_items_; ++i) {
      items_[i] = grpc_core::New<WorkItem>(start_index_ + i);
      queue_->Put(items_[i]);
    }
  }

  int start_index_;
  int num_items_;
  grpc_core::InfLenFIFOQueue* queue_;
  grpc_core::Thread thd_;
  WorkItem** items_;
};

class ConsumerThread {
 public:
  ConsumerThread(grpc_core::InfLenFIFOQueue* queue) : queue_(queue) {
    thd_ = grpc_core::Thread(
        "mpmcq_test_get_thd",
        [](void* th) { static_cast<ConsumerThread*>(th)->Run(); }, this);
  }
  ~ConsumerThread() {}

  void Start() { thd_.Start(); }
  void Join() { thd_.Join(); }

 private:
  void Run() {
    // count number of Get() called in this thread
    int count = 0;

    WorkItem* item;
    while ((item = static_cast<WorkItem*>(queue_->Get())) != nullptr) {
      count++;
      GPR_ASSERT(!item->done);
      item->done = true;
    }

    gpr_log(GPR_DEBUG, "ConsumerThread: %d times of Get() called.", count);
  }
  grpc_core::InfLenFIFOQueue* queue_;
  grpc_core::Thread thd_;
};

static void test_get_empty(void) {
  gpr_log(GPR_INFO, "test_get_empty");
  grpc_core::InfLenFIFOQueue queue;
  GPR_ASSERT(queue.count() == 0);
  const int num_threads = 10;
  ConsumerThread** consumer_thds = static_cast<ConsumerThread**>(
      gpr_zalloc(num_threads * sizeof(ConsumerThread*)));

  // Fork threads. Threads should block at the beginning since queue is empty.
  for (int i = 0; i < num_threads; ++i) {
    consumer_thds[i] = grpc_core::New<ConsumerThread>(&queue);
    consumer_thds[i]->Start();
  }

  WorkItem** items = static_cast<WorkItem**>(
      gpr_zalloc(THREAD_LARGE_ITERATION * sizeof(WorkItem*)));
  for (int i = 0; i < THREAD_LARGE_ITERATION; ++i) {
    items[i] = grpc_core::New<WorkItem>(i);
    queue.Put(static_cast<void*>(items[i]));
  }

  gpr_log(GPR_DEBUG, "Terminating threads...");
  for (int i = 0; i < num_threads; ++i) {
    queue.Put(nullptr);
  }
  for (int i = 0; i < num_threads; ++i) {
    consumer_thds[i]->Join();
  }
  gpr_log(GPR_DEBUG, "Checking and Cleaning Up...");
  for (int i = 0; i < THREAD_LARGE_ITERATION; ++i) {
    GPR_ASSERT(items[i]->done);
    grpc_core::Delete(items[i]);
  }
  gpr_free(items);
  for (int i = 0; i < num_threads; ++i) {
    grpc_core::Delete(consumer_thds[i]);
  }
  gpr_free(consumer_thds);
  gpr_log(GPR_DEBUG, "Done.");
}

static void test_FIFO(void) {
  gpr_log(GPR_INFO, "test_FIFO");
  grpc_core::InfLenFIFOQueue large_queue;
  for (int i = 0; i < THREAD_LARGE_ITERATION; ++i) {
    large_queue.Put(static_cast<void*>(grpc_core::New<WorkItem>(i)));
  }
  GPR_ASSERT(large_queue.count() == THREAD_LARGE_ITERATION);
  for (int i = 0; i < THREAD_LARGE_ITERATION; ++i) {
    WorkItem* item = static_cast<WorkItem*>(large_queue.Get());
    GPR_ASSERT(i == item->index);
    grpc_core::Delete(item);
  }
}

static void test_many_thread(void) {
  gpr_log(GPR_INFO, "test_many_thread");
  const int num_work_thd = 10;
  const int num_get_thd = 20;
  grpc_core::InfLenFIFOQueue queue;
  ProducerThread** work_thds = static_cast<ProducerThread**>(
      gpr_zalloc(num_work_thd * sizeof(ProducerThread*)));
  ConsumerThread** consumer_thds = static_cast<ConsumerThread**>(
      gpr_zalloc(num_get_thd * sizeof(ConsumerThread*)));

  gpr_log(GPR_DEBUG, "Fork ProducerThread...");
  for (int i = 0; i < num_work_thd; ++i) {
    work_thds[i] = grpc_core::New<ProducerThread>(
        &queue, i * THREAD_LARGE_ITERATION, THREAD_LARGE_ITERATION);
    work_thds[i]->Start();
  }
  gpr_log(GPR_DEBUG, "ProducerThread Started.");
  gpr_log(GPR_DEBUG, "Fork Getter Thread...");
  for (int i = 0; i < num_get_thd; ++i) {
    consumer_thds[i] = grpc_core::New<ConsumerThread>(&queue);
    consumer_thds[i]->Start();
  }
  gpr_log(GPR_DEBUG, "Getter Thread Started.");
  gpr_log(GPR_DEBUG, "Waiting ProducerThread to finish...");
  for (int i = 0; i < num_work_thd; ++i) {
    work_thds[i]->Join();
  }
  gpr_log(GPR_DEBUG, "All ProducerThread Terminated.");
  gpr_log(GPR_DEBUG, "Terminating Getter Thread...");
  for (int i = 0; i < num_get_thd; ++i) {
    queue.Put(nullptr);
  }
  for (int i = 0; i < num_get_thd; ++i) {
    consumer_thds[i]->Join();
  }
  gpr_log(GPR_DEBUG, "All Getter Thread Terminated.");
  gpr_log(GPR_DEBUG, "Checking WorkItems and Cleaning Up...");
  for (int i = 0; i < num_work_thd; ++i) {
    grpc_core::Delete(work_thds[i]);
  }
  gpr_free(work_thds);
  for (int i = 0; i < num_get_thd; ++i) {
    grpc_core::Delete(consumer_thds[i]);
  }
  gpr_free(consumer_thds);
  gpr_log(GPR_DEBUG, "Done.");
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  gpr_set_log_verbosity(GPR_LOG_SEVERITY_DEBUG);
  test_get_empty();
  test_FIFO();
  test_many_thread();
  grpc_shutdown();
  return 0;
}
