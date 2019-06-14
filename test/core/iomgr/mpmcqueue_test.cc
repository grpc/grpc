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

#include "src/core/lib/iomgr/threadpool/mpmcqueue.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/thd.h"
#include "test/core/util/test_config.h"

#define THREAD_SMALL_ITERATION 100
#define THREAD_LARGE_ITERATION 10000

static void test_no_op(void) {
  gpr_log(GPR_DEBUG, "test_no_op");
  grpc_core::MPMCQueue mpmcqueue;
  gpr_log(GPR_DEBUG, "Checking count...");
  GPR_ASSERT(mpmcqueue.count() == 0);
  gpr_log(GPR_DEBUG, "Done.");
}

// Testing items for queue
struct WorkItem {
  int index;
  bool done;

  WorkItem(int i) : index(i) {
    done = false;
  }
  void* operator new(size_t n) {
    void* p = gpr_malloc(n);
    return p;
  }

  void operator delete(void* p) {
    gpr_free(p);
  }
};

static void test_small_queue(void) {
  gpr_log(GPR_DEBUG, "test_small_queue");
  grpc_core::MPMCQueue small_queue;
  for (int i = 0; i < THREAD_SMALL_ITERATION; ++i) {
    small_queue.Put(static_cast<void*>(new WorkItem(i)));
  }
  GPR_ASSERT(small_queue.count() == THREAD_SMALL_ITERATION);
  // Get items out in FIFO order
  for (int i = 0; i < THREAD_SMALL_ITERATION; ++i) {
    WorkItem* item = static_cast<WorkItem*>(small_queue.Get());
    GPR_ASSERT(i == item->index);
    delete item;
  }
}

static void test_get_thd(void* args) {
  grpc_core::MPMCQueue* mpmcqueue = static_cast<grpc_core::MPMCQueue*>(args);

  // count number of Get() called in this thread
  int count = 0;
  int last_index = -1;
  WorkItem* item;
  while ((item = static_cast<WorkItem*>(mpmcqueue->Get())) != NULL) {
    count++;
    GPR_ASSERT(item->index > last_index);
    last_index = item->index;
    GPR_ASSERT(!item->done);
    delete item;
  }

  gpr_log(GPR_DEBUG, "test_get_thd: %d times of Get() called.", count);
}

static void test_get_empty(void) {
  gpr_log(GPR_DEBUG, "test_get_empty");
  grpc_core::MPMCQueue mpmcqueue;
  const int num_threads = 10;
  grpc_core::Thread thds[num_threads];

  // Fork threads. Threads should block at the beginning since queue is empty.
  for (int i = 0; i < num_threads; ++i) {
    thds[i] = grpc_core::Thread("mpmcq_test_ge_thd", test_get_thd, &mpmcqueue);
    thds[i].Start();
  }

  for (int i = 0; i < THREAD_LARGE_ITERATION; ++i) {
    mpmcqueue.Put(static_cast<void*>(new WorkItem(i)));
  }

  gpr_log(GPR_DEBUG, "Terminating threads...");
  for (int i = 0; i < num_threads; ++i) {
    mpmcqueue.Put(NULL);
  }
  for (int i = 0; i < num_threads; ++i) {
    thds[i].Join();
  }
  gpr_log(GPR_DEBUG, "Done.");
}

static void test_large_queue(void) {
  gpr_log(GPR_DEBUG, "test_large_queue");
  grpc_core::MPMCQueue large_queue;
  for (int i = 0; i < THREAD_LARGE_ITERATION; ++i) {
    large_queue.Put(static_cast<void*>(new WorkItem(i)));
  }
  GPR_ASSERT(large_queue.count() == THREAD_LARGE_ITERATION);
  for (int i = 0; i < THREAD_LARGE_ITERATION; ++i) {
    WorkItem* item = static_cast<WorkItem*>(large_queue.Get());
    GPR_ASSERT(i == item->index);
    delete item;
  }
}

// Thread for put items into queue
class WorkThread {
 public:
  WorkThread(grpc_core::MPMCQueue* mpmcqueue, int start_index, int num_items)
      : start_index_(start_index), num_items_(num_items),
        mpmcqueue_(mpmcqueue) {
    items_ = NULL;
    thd_ = grpc_core::Thread(
        "mpmcq_test_mt_put_thd",
        [](void* th) { static_cast<WorkThread*>(th)->Run(); },
        this);
  }
  ~WorkThread() {
    for (int i = 0; i < num_items_; ++i) {
      GPR_ASSERT(items_[i]->done);
      delete items_[i];
    }
    gpr_free(items_);
  }

  void Start() { thd_.Start(); }
  void Join() { thd_.Join(); }

  void* operator new(size_t n) {
    void* p = gpr_malloc(n);
    return p;
  }

  void operator delete(void* p) {
    gpr_free(p);
  }

 private:
  void Run() {
    items_ = static_cast<WorkItem**>(
        gpr_malloc(sizeof(WorkItem*) * num_items_));
    for (int i = 0; i < num_items_; ++i) {
      items_[i] = new WorkItem(start_index_ + i);
      mpmcqueue_->Put(items_[i]);
    }
  }

  int start_index_;
  int num_items_;
  grpc_core::MPMCQueue* mpmcqueue_;
  grpc_core::Thread thd_;
  WorkItem** items_;
};


static void test_many_get_thd(void* args) {
  grpc_core::MPMCQueue* mpmcqueue = static_cast<grpc_core::MPMCQueue*>(args);

  // count number of Get() called in this thread
  int count = 0;

  WorkItem* item;
  while ((item = static_cast<WorkItem*>(mpmcqueue->Get())) != NULL) {
    count++;
    GPR_ASSERT(!item->done);
    item->done = true;
  }

  gpr_log(GPR_DEBUG, "test_many_get_thd: %d times of Get() called.", count);
}

static void test_many_thread(void) {
  gpr_log(GPR_DEBUG, "test_many_thread");
  const int num_work_thd = 10;
  const int num_get_thd = 20;
  grpc_core::MPMCQueue mpmcqueue;
  WorkThread** work_thds =
      static_cast<WorkThread**>(gpr_malloc(sizeof(WorkThread*) * num_work_thd));
  grpc_core::Thread get_thds[num_get_thd];

  gpr_log(GPR_DEBUG, "Fork WorkThread...");
  for (int i = 0; i < num_work_thd; ++i) {
    work_thds[i] = new WorkThread(&mpmcqueue, i * THREAD_LARGE_ITERATION,
                                  THREAD_LARGE_ITERATION);
    work_thds[i]->Start();
  }
  gpr_log(GPR_DEBUG, "WorkThread Started.");
  gpr_log(GPR_DEBUG, "For Getter Thread...");
  for (int i = 0; i < num_get_thd; ++i) {
    get_thds[i] = grpc_core::Thread("mpmcq_test_mt_get_thd",
                                    test_many_get_thd, &mpmcqueue);
    get_thds[i].Start();
  }
  gpr_log(GPR_DEBUG, "Getter Thread Started.");
  gpr_log(GPR_DEBUG, "Waiting WorkThread to finish...");
  for (int i = 0; i < num_work_thd; ++i) {
    work_thds[i]->Join();
  }
  gpr_log(GPR_DEBUG, "All WorkThread Terminated.");
  gpr_log(GPR_DEBUG, "Terminating Getter Thread...");
  for (int i = 0; i < num_get_thd; ++i) {
    mpmcqueue.Put(NULL);
  }
  for (int i = 0; i < num_get_thd; ++i) {
    get_thds[i].Join();
  }
  gpr_log(GPR_DEBUG, "All Getter Thread Terminated.");
  gpr_log(GPR_DEBUG, "Checking WorkItems and Cleaning Up...");
  for (int i = 0; i < num_work_thd; ++i) {
    delete work_thds[i];
  }
  gpr_free(work_thds);
  gpr_log(GPR_DEBUG, "Done.");
}


int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  gpr_set_log_verbosity(GPR_LOG_SEVERITY_DEBUG);
  test_no_op();
  test_small_queue();
  test_get_empty();
  test_large_queue();
  test_many_thread();
  grpc_shutdown();
  return 0;
}
