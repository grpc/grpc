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

#define TEST_NUM_ITEMS 10000

// Testing items for queue
struct WorkItem {
  int index;
  bool done;

  explicit WorkItem(int i) : index(i) { done = false; }
};

// Thread to "produce" items and put items into queue
// It will also check that all items has been marked done and clean up all
// produced items on destructing.
class ProducerThread {
 public:
  ProducerThread(grpc_core::InfLenFIFOQueue* queue, int start_index,
                 int num_items)
      : start_index_(start_index), num_items_(num_items), queue_(queue) {
    items_ = nullptr;
    thd_ = grpc_core::Thread(
        "mpmcq_test_producer_thd",
        [](void* th) { static_cast<ProducerThread*>(th)->Run(); }, this);
  }
  ~ProducerThread() {
    for (int i = 0; i < num_items_; ++i) {
      GPR_ASSERT(items_[i]->done);
      delete items_[i];
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
      items_[i] = new WorkItem(start_index_ + i);
      queue_->Put(items_[i]);
    }
  }

  int start_index_;
  int num_items_;
  grpc_core::InfLenFIFOQueue* queue_;
  grpc_core::Thread thd_;
  WorkItem** items_;
};

// Thread to pull out items from queue
class ConsumerThread {
 public:
  explicit ConsumerThread(grpc_core::InfLenFIFOQueue* queue) : queue_(queue) {
    thd_ = grpc_core::Thread(
        "mpmcq_test_consumer_thd",
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
    while ((item = static_cast<WorkItem*>(queue_->Get(nullptr))) != nullptr) {
      count++;
      GPR_ASSERT(!item->done);
      item->done = true;
    }

    gpr_log(GPR_DEBUG, "ConsumerThread: %d times of Get() called.", count);
  }
  grpc_core::InfLenFIFOQueue* queue_;
  grpc_core::Thread thd_;
};

static void test_FIFO(void) {
  gpr_log(GPR_INFO, "test_FIFO");
  grpc_core::InfLenFIFOQueue large_queue;
  for (int i = 0; i < TEST_NUM_ITEMS; ++i) {
    large_queue.Put(static_cast<void*>(new WorkItem(i)));
  }
  GPR_ASSERT(large_queue.count() == TEST_NUM_ITEMS);
  for (int i = 0; i < TEST_NUM_ITEMS; ++i) {
    WorkItem* item = static_cast<WorkItem*>(large_queue.Get(nullptr));
    GPR_ASSERT(i == item->index);
    delete item;
  }
}

// Test if queue's behavior of expanding is correct. (Only does expansion when
// it gets full, and each time expands to doubled size).
static void test_space_efficiency(void) {
  gpr_log(GPR_INFO, "test_space_efficiency");
  grpc_core::InfLenFIFOQueue queue;
  for (int i = 0; i < queue.init_num_nodes(); ++i) {
    queue.Put(static_cast<void*>(new WorkItem(i)));
  }
  // Queue should not have been expanded at this time.
  GPR_ASSERT(queue.num_nodes() == queue.init_num_nodes());
  for (int i = 0; i < queue.init_num_nodes(); ++i) {
    WorkItem* item = static_cast<WorkItem*>(queue.Get(nullptr));
    queue.Put(item);
  }
  GPR_ASSERT(queue.num_nodes() == queue.init_num_nodes());
  for (int i = 0; i < queue.init_num_nodes(); ++i) {
    WorkItem* item = static_cast<WorkItem*>(queue.Get(nullptr));
    delete item;
  }
  // Queue never shrinks even it is empty.
  GPR_ASSERT(queue.num_nodes() == queue.init_num_nodes());
  GPR_ASSERT(queue.count() == 0);
  // queue empty now
  for (int i = 0; i < queue.init_num_nodes() * 2; ++i) {
    queue.Put(static_cast<void*>(new WorkItem(i)));
  }
  GPR_ASSERT(queue.count() == queue.init_num_nodes() * 2);
  // Queue should have been expanded once.
  GPR_ASSERT(queue.num_nodes() == queue.init_num_nodes() * 2);
  for (int i = 0; i < queue.init_num_nodes(); ++i) {
    WorkItem* item = static_cast<WorkItem*>(queue.Get(nullptr));
    delete item;
  }
  GPR_ASSERT(queue.count() == queue.init_num_nodes());
  // Queue will never shrink, should keep same number of node as before.
  GPR_ASSERT(queue.num_nodes() == queue.init_num_nodes() * 2);
  for (int i = 0; i < queue.init_num_nodes() + 1; ++i) {
    queue.Put(static_cast<void*>(new WorkItem(i)));
  }
  GPR_ASSERT(queue.count() == queue.init_num_nodes() * 2 + 1);
  // Queue should have been expanded twice.
  GPR_ASSERT(queue.num_nodes() == queue.init_num_nodes() * 4);
  for (int i = 0; i < queue.init_num_nodes() * 2 + 1; ++i) {
    WorkItem* item = static_cast<WorkItem*>(queue.Get(nullptr));
    delete item;
  }
  GPR_ASSERT(queue.count() == 0);
  GPR_ASSERT(queue.num_nodes() == queue.init_num_nodes() * 4);
  gpr_log(GPR_DEBUG, "Done.");
}

static void test_many_thread(void) {
  gpr_log(GPR_INFO, "test_many_thread");
  const int num_producer_threads = 10;
  const int num_consumer_threads = 20;
  grpc_core::InfLenFIFOQueue queue;
  ProducerThread** producer_threads = static_cast<ProducerThread**>(
      gpr_zalloc(num_producer_threads * sizeof(ProducerThread*)));
  ConsumerThread** consumer_threads = static_cast<ConsumerThread**>(
      gpr_zalloc(num_consumer_threads * sizeof(ConsumerThread*)));

  gpr_log(GPR_DEBUG, "Fork ProducerThreads...");
  for (int i = 0; i < num_producer_threads; ++i) {
    producer_threads[i] =
        new ProducerThread(&queue, i * TEST_NUM_ITEMS, TEST_NUM_ITEMS);
    producer_threads[i]->Start();
  }
  gpr_log(GPR_DEBUG, "ProducerThreads Started.");
  gpr_log(GPR_DEBUG, "Fork ConsumerThreads...");
  for (int i = 0; i < num_consumer_threads; ++i) {
    consumer_threads[i] = new ConsumerThread(&queue);
    consumer_threads[i]->Start();
  }
  gpr_log(GPR_DEBUG, "ConsumerThreads Started.");
  gpr_log(GPR_DEBUG, "Waiting ProducerThreads to finish...");
  for (int i = 0; i < num_producer_threads; ++i) {
    producer_threads[i]->Join();
  }
  gpr_log(GPR_DEBUG, "All ProducerThreads Terminated.");
  gpr_log(GPR_DEBUG, "Terminating ConsumerThreads...");
  for (int i = 0; i < num_consumer_threads; ++i) {
    queue.Put(nullptr);
  }
  for (int i = 0; i < num_consumer_threads; ++i) {
    consumer_threads[i]->Join();
  }
  gpr_log(GPR_DEBUG, "All ConsumerThreads Terminated.");
  gpr_log(GPR_DEBUG, "Checking WorkItems and Cleaning Up...");
  for (int i = 0; i < num_producer_threads; ++i) {
    // Destructor of ProducerThread will do the check of WorkItems
    delete producer_threads[i];
  }
  gpr_free(producer_threads);
  for (int i = 0; i < num_consumer_threads; ++i) {
    delete consumer_threads[i];
  }
  gpr_free(consumer_threads);
  gpr_log(GPR_DEBUG, "Done.");
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  test_FIFO();
  test_space_efficiency();
  test_many_thread();
  grpc_shutdown();
  return 0;
}
