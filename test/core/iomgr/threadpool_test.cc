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

static const int kSmallThreadPoolSize = 20;
static const int kLargeThreadPoolSize = 100;
static const int kThreadSmallIter = 100;
static const int kThreadLargeIter = 10000;

static void test_size_zero(void) {
  gpr_log(GPR_INFO, "test_size_zero");
  grpc_core::ThreadPool* pool_size_zero = new grpc_core::ThreadPool(0);
  GPR_ASSERT(pool_size_zero->pool_capacity() == 1);
  delete pool_size_zero;
}

static void test_constructor_option(void) {
  gpr_log(GPR_INFO, "test_constructor_option");
  // Tests options
  grpc_core::Thread::Options options;
  options.set_stack_size(192 * 1024);  // Random non-default value
  grpc_core::ThreadPool* pool =
      new grpc_core::ThreadPool(0, "test_constructor_option", options);
  GPR_ASSERT(pool->thread_options().stack_size() == options.stack_size());
  delete pool;
}

// Simple functor for testing. It will count how many times being called.
class SimpleFunctorForAdd : public grpc_experimental_completion_queue_functor {
 public:
  friend class SimpleFunctorCheckForAdd;
  SimpleFunctorForAdd() {
    functor_run = &SimpleFunctorForAdd::Run;
    inlineable = true;
    internal_next = this;
    internal_success = 0;
  }
  ~SimpleFunctorForAdd() {}
  static void Run(struct grpc_experimental_completion_queue_functor* cb,
                  int /*ok*/) {
    auto* callback = static_cast<SimpleFunctorForAdd*>(cb);
    callback->count_.FetchAdd(1, grpc_core::MemoryOrder::RELAXED);
  }

  int count() { return count_.Load(grpc_core::MemoryOrder::RELAXED); }

 private:
  grpc_core::Atomic<int> count_{0};
};

static void test_add(void) {
  gpr_log(GPR_INFO, "test_add");
  grpc_core::ThreadPool* pool =
      new grpc_core::ThreadPool(kSmallThreadPoolSize, "test_add");

  SimpleFunctorForAdd* functor = new SimpleFunctorForAdd();
  for (int i = 0; i < kThreadSmallIter; ++i) {
    pool->Add(functor);
  }
  delete pool;
  GPR_ASSERT(functor->count() == kThreadSmallIter);
  delete functor;
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
  grpc_core::ThreadPool* pool =
      new grpc_core::ThreadPool(kLargeThreadPoolSize, "test_multi_add");
  SimpleFunctorForAdd* functor = new SimpleFunctorForAdd();
  WorkThread** work_thds = static_cast<WorkThread**>(
      gpr_zalloc(sizeof(WorkThread*) * num_work_thds));
  gpr_log(GPR_DEBUG, "Fork threads for adding...");
  for (int i = 0; i < num_work_thds; ++i) {
    work_thds[i] = new WorkThread(pool, functor, kThreadLargeIter);
    work_thds[i]->Start();
  }
  // Wait for all threads finish
  gpr_log(GPR_DEBUG, "Waiting for all work threads finish...");
  for (int i = 0; i < num_work_thds; ++i) {
    work_thds[i]->Join();
    delete work_thds[i];
  }
  gpr_free(work_thds);
  gpr_log(GPR_DEBUG, "Done.");
  gpr_log(GPR_DEBUG, "Waiting for all closures finish...");
  // Destructor of thread pool will wait for all closures to finish
  delete pool;
  GPR_ASSERT(functor->count() == kThreadLargeIter * num_work_thds);
  delete functor;
  gpr_log(GPR_DEBUG, "Done.");
}

// Checks the current count with a given number.
class SimpleFunctorCheckForAdd
    : public grpc_experimental_completion_queue_functor {
 public:
  SimpleFunctorCheckForAdd(int ok, int* count) : count_(count) {
    functor_run = &SimpleFunctorCheckForAdd::Run;
    inlineable = true;
    internal_success = ok;
  }
  ~SimpleFunctorCheckForAdd() {}
  static void Run(struct grpc_experimental_completion_queue_functor* cb,
                  int /*ok*/) {
    auto* callback = static_cast<SimpleFunctorCheckForAdd*>(cb);
    (*callback->count_)++;
    GPR_ASSERT(*callback->count_ == callback->internal_success);
  }

 private:
  int* count_;
};

static void test_one_thread_FIFO(void) {
  gpr_log(GPR_INFO, "test_one_thread_FIFO");
  int counter = 0;
  grpc_core::ThreadPool* pool =
      new grpc_core::ThreadPool(1, "test_one_thread_FIFO");
  SimpleFunctorCheckForAdd** check_functors =
      static_cast<SimpleFunctorCheckForAdd**>(
          gpr_zalloc(sizeof(SimpleFunctorCheckForAdd*) * kThreadSmallIter));
  for (int i = 0; i < kThreadSmallIter; ++i) {
    check_functors[i] = new SimpleFunctorCheckForAdd(i + 1, &counter);
    pool->Add(check_functors[i]);
  }
  // Destructor of pool will wait until all closures finished.
  delete pool;
  for (int i = 0; i < kThreadSmallIter; ++i) {
    delete check_functors[i];
  }
  gpr_free(check_functors);
  gpr_log(GPR_DEBUG, "Done.");
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  test_size_zero();
  test_constructor_option();
  test_add();
  test_multi_add();
  test_one_thread_FIFO();
  grpc_shutdown();
  return 0;
}
