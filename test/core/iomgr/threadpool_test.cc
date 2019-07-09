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

#include "src/core/lib/iomgr/executor/threadpool.h"

#include "test/core/util/test_config.h"

#define SMALL_THREAD_POOL_SIZE 20
#define LARGE_THREAD_POOL_SIZE 100
#define THREAD_SMALL_ITERATION 100
#define THREAD_LARGE_ITERATION 10000

// Simple functor for testing. It will count how many times being called.
class SimpleFunctorForAdd : public grpc_experimental_completion_queue_functor {
 public:
  friend class SimpleFunctorCheckForAdd;
  SimpleFunctorForAdd() : count_(0) {
    functor_run = &SimpleFunctorForAdd::Run;
    internal_next = this;
    internal_success = 0;
  }
  ~SimpleFunctorForAdd() {}
  static void Run(struct grpc_experimental_completion_queue_functor* cb,
                  int ok) {
    auto* callback = static_cast<SimpleFunctorForAdd*>(cb);
    callback->count_.FetchAdd(1, grpc_core::MemoryOrder::RELAXED);
  }

  int count() { return count_.Load(grpc_core::MemoryOrder::RELAXED); }

 private:
  grpc_core::Atomic<int> count_{0};
};

// Checks the given SimpleFunctorForAdd's count with a given number.
class SimpleFunctorCheckForAdd
    : public grpc_experimental_completion_queue_functor {
 public:
  SimpleFunctorCheckForAdd(
      struct grpc_experimental_completion_queue_functor* cb, int ok) {
    functor_run = &SimpleFunctorCheckForAdd::Run;
    internal_next = cb;
    internal_success = ok;
  }
  ~SimpleFunctorCheckForAdd() {}
  static void Run(struct grpc_experimental_completion_queue_functor* cb,
                  int ok) {
    auto* callback = static_cast<SimpleFunctorCheckForAdd*>(cb);
    auto* cb_check = static_cast<SimpleFunctorForAdd*>(callback->internal_next);
    GPR_ASSERT(cb_check->count_.Load(grpc_core::MemoryOrder::RELAXED) == ok);
  }
};

static void test_add(void) {
  gpr_log(GPR_INFO, "test_add");
  grpc_core::ThreadPool* pool =
      grpc_core::New<grpc_core::ThreadPool>(SMALL_THREAD_POOL_SIZE, "test_add");

  SimpleFunctorForAdd* functor = grpc_core::New<SimpleFunctorForAdd>();
  for (int i = 0; i < THREAD_SMALL_ITERATION; ++i) {
    pool->Add(functor);
  }
  grpc_core::Delete(pool);
  GPR_ASSERT(functor->count() == THREAD_SMALL_ITERATION);
  grpc_core::Delete(functor);
  gpr_log(GPR_DEBUG, "Done.");
}

// Thread that adds closures to pool
class WorkThread {
 public:
  WorkThread(grpc_core::ThreadPool* pool, SimpleFunctorForAdd* cb, int num_add)
      : num_add_(num_add), cb_(cb), pool_(pool) {
    thd_ = grpc_core::Thread(
        "thread_pool_test_add_thd",
        [](void* th) { static_cast<WorkThread*>(th)->Run(); }, this);
  }
  ~WorkThread() {}

  void Start() { thd_.Start(); }
  void Join() { thd_.Join(); }

 private:
  void Run() {
    for (int i = 0; i < num_add_; ++i) {
      pool_->Add(cb_);
    }
  }

  int num_add_;
  SimpleFunctorForAdd* cb_;
  grpc_core::ThreadPool* pool_;
  grpc_core::Thread thd_;
};

static void test_multi_add(void) {
  gpr_log(GPR_INFO, "test_multi_add");
  const int num_work_thds = 10;
  grpc_core::ThreadPool* pool = grpc_core::New<grpc_core::ThreadPool>(
      LARGE_THREAD_POOL_SIZE, "test_multi_add");
  SimpleFunctorForAdd* functor = grpc_core::New<SimpleFunctorForAdd>();
  WorkThread** work_thds = static_cast<WorkThread**>(
      gpr_zalloc(sizeof(WorkThread*) * num_work_thds));
  gpr_log(GPR_DEBUG, "Fork threads for adding...");
  for (int i = 0; i < num_work_thds; ++i) {
    work_thds[i] =
        grpc_core::New<WorkThread>(pool, functor, THREAD_LARGE_ITERATION);
    work_thds[i]->Start();
  }
  // Wait for all threads finish
  gpr_log(GPR_DEBUG, "Waiting for all work threads finish...");
  for (int i = 0; i < num_work_thds; ++i) {
    work_thds[i]->Join();
    grpc_core::Delete(work_thds[i]);
  }
  gpr_free(work_thds);
  gpr_log(GPR_DEBUG, "Done.");
  gpr_log(GPR_DEBUG, "Waiting for all closures finish...");
  // Destructor of thread pool will wait for all closures to finish
  grpc_core::Delete(pool);
  GPR_ASSERT(functor->count() == THREAD_LARGE_ITERATION * num_work_thds);
  grpc_core::Delete(functor);
  gpr_log(GPR_DEBUG, "Done.");
}

static void test_one_thread_FIFO(void) {
  gpr_log(GPR_INFO, "test_one_thread_FIFO");
  grpc_core::ThreadPool* pool =
      grpc_core::New<grpc_core::ThreadPool>(1, "test_one_thread_FIFO");
  SimpleFunctorForAdd* functor = grpc_core::New<SimpleFunctorForAdd>();
  SimpleFunctorCheckForAdd** check_functors =
      static_cast<SimpleFunctorCheckForAdd**>(gpr_zalloc(
          sizeof(SimpleFunctorCheckForAdd*) * THREAD_SMALL_ITERATION));
  for (int i = 0; i < THREAD_SMALL_ITERATION; ++i) {
    pool->Add(functor);
    check_functors[i] =
        grpc_core::New<SimpleFunctorCheckForAdd>(functor, i + 1);
    pool->Add(check_functors[i]);
  }
  // Destructor of pool will wait until all closures finished.
  grpc_core::Delete(pool);
  grpc_core::Delete(functor);
  for (int i = 0; i < THREAD_SMALL_ITERATION; ++i) {
    grpc_core::Delete(check_functors[i]);
  }
  gpr_free(check_functors);
  gpr_log(GPR_DEBUG, "Done.");
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  test_add();
  test_multi_add();
  test_one_thread_FIFO();
  grpc_shutdown();
  return 0;
}
