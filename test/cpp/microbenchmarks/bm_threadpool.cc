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

#include <benchmark/benchmark.h>
#include <grpc/grpc.h>

#include <condition_variable>
#include <mutex>

#include "src/core/lib/iomgr/executor/threadpool.h"
#include "test/core/util/test_config.h"
#include "test/cpp/microbenchmarks/helpers.h"
#include "test/cpp/util/test_config.h"

namespace grpc {
namespace testing {

// This helper class allows a thread to block for a pre-specified number of
// actions. BlockingCounter has an initial non-negative count on initialization.
// Each call to DecrementCount will decrease the count by 1. When making a call
// to Wait, if the count is greater than 0, the thread will be blocked, until
// the count reaches 0.
class BlockingCounter {
 public:
  explicit BlockingCounter(int count) : count_(count) {}
  void DecrementCount() {
    std::lock_guard<std::mutex> l(mu_);
    count_--;
    if (count_ == 0) cv_.notify_all();
  }

  void Wait() {
    std::unique_lock<std::mutex> l(mu_);
    while (count_ > 0) {
      cv_.wait(l);
    }
  }

 private:
  int count_;
  std::mutex mu_;
  std::condition_variable cv_;
};

// This is a functor/closure class for threadpool microbenchmark.
// This functor (closure) class will add another functor into pool if the
// number passed in (num_add) is greater than 0. Otherwise, it will decrement
// the counter to indicate that task is finished. This functor will suicide at
// the end, therefore, no need for caller to do clean-ups.
class AddAnotherFunctor : public grpc_experimental_completion_queue_functor {
 public:
  AddAnotherFunctor(grpc_core::ThreadPool* pool, BlockingCounter* counter,
                    int num_add)
      : pool_(pool), counter_(counter), num_add_(num_add) {
    functor_run = &AddAnotherFunctor::Run;
    inlineable = false;
    internal_next = this;
    internal_success = 0;
  }
  // When the functor gets to run in thread pool, it will take itself as first
  // argument and internal_success as second one.
  static void Run(grpc_experimental_completion_queue_functor* cb, int /*ok*/) {
    auto* callback = static_cast<AddAnotherFunctor*>(cb);
    if (--callback->num_add_ > 0) {
      callback->pool_->Add(new AddAnotherFunctor(
          callback->pool_, callback->counter_, callback->num_add_));
    } else {
      callback->counter_->DecrementCount();
    }
    // Suicides.
    delete callback;
  }

 private:
  grpc_core::ThreadPool* pool_;
  BlockingCounter* counter_;
  int num_add_;
};

template <int kConcurrentFunctor>
static void ThreadPoolAddAnother(benchmark::State& state) {
  const int num_iterations = state.range(0);
  const int num_threads = state.range(1);
  // Number of adds done by each closure.
  const int num_add = num_iterations / kConcurrentFunctor;
  grpc_core::ThreadPool pool(num_threads);
  while (state.KeepRunningBatch(num_iterations)) {
    BlockingCounter counter(kConcurrentFunctor);
    for (int i = 0; i < kConcurrentFunctor; ++i) {
      pool.Add(new AddAnotherFunctor(&pool, &counter, num_add));
    }
    counter.Wait();
  }
  state.SetItemsProcessed(state.iterations());
}

// First pair of arguments is range for number of iterations (num_iterations).
// Second pair of arguments is range for thread pool size (num_threads).
BENCHMARK_TEMPLATE(ThreadPoolAddAnother, 1)->RangePair(524288, 524288, 1, 1024);
BENCHMARK_TEMPLATE(ThreadPoolAddAnother, 4)->RangePair(524288, 524288, 1, 1024);
BENCHMARK_TEMPLATE(ThreadPoolAddAnother, 8)->RangePair(524288, 524288, 1, 1024);
BENCHMARK_TEMPLATE(ThreadPoolAddAnother, 16)
    ->RangePair(524288, 524288, 1, 1024);
BENCHMARK_TEMPLATE(ThreadPoolAddAnother, 32)
    ->RangePair(524288, 524288, 1, 1024);
BENCHMARK_TEMPLATE(ThreadPoolAddAnother, 64)
    ->RangePair(524288, 524288, 1, 1024);
BENCHMARK_TEMPLATE(ThreadPoolAddAnother, 128)
    ->RangePair(524288, 524288, 1, 1024);
BENCHMARK_TEMPLATE(ThreadPoolAddAnother, 512)
    ->RangePair(524288, 524288, 1, 1024);
BENCHMARK_TEMPLATE(ThreadPoolAddAnother, 2048)
    ->RangePair(524288, 524288, 1, 1024);

// A functor class that will delete self on end of running.
class SuicideFunctorForAdd : public grpc_experimental_completion_queue_functor {
 public:
  explicit SuicideFunctorForAdd(BlockingCounter* counter) : counter_(counter) {
    functor_run = &SuicideFunctorForAdd::Run;
    inlineable = false;
    internal_next = this;
    internal_success = 0;
  }

  static void Run(grpc_experimental_completion_queue_functor* cb, int /*ok*/) {
    // On running, the first argument would be itself.
    auto* callback = static_cast<SuicideFunctorForAdd*>(cb);
    callback->counter_->DecrementCount();
    delete callback;
  }

 private:
  BlockingCounter* counter_;
};

// Performs the scenario of external thread(s) adding closures into pool.
static void BM_ThreadPoolExternalAdd(benchmark::State& state) {
  static grpc_core::ThreadPool* external_add_pool = nullptr;
  // Setup for each run of test.
  if (state.thread_index == 0) {
    const int num_threads = state.range(1);
    external_add_pool = new grpc_core::ThreadPool(num_threads);
  }
  const int num_iterations = state.range(0) / state.threads;
  while (state.KeepRunningBatch(num_iterations)) {
    BlockingCounter counter(num_iterations);
    for (int i = 0; i < num_iterations; ++i) {
      external_add_pool->Add(new SuicideFunctorForAdd(&counter));
    }
    counter.Wait();
  }

  // Teardown at the end of each test run.
  if (state.thread_index == 0) {
    state.SetItemsProcessed(state.range(0));
    delete external_add_pool;
  }
}
BENCHMARK(BM_ThreadPoolExternalAdd)
    // First pair is range for number of iterations (num_iterations).
    // Second pair is range for thread pool size (num_threads).
    ->RangePair(524288, 524288, 1, 1024)
    ->ThreadRange(1, 256);  // Concurrent external thread(s) up to 256

// Functor (closure) that adds itself into pool repeatedly. By adding self, the
// overhead would be low and can measure the time of add more accurately.
class AddSelfFunctor : public grpc_experimental_completion_queue_functor {
 public:
  AddSelfFunctor(grpc_core::ThreadPool* pool, BlockingCounter* counter,
                 int num_add)
      : pool_(pool), counter_(counter), num_add_(num_add) {
    functor_run = &AddSelfFunctor::Run;
    inlineable = false;
    internal_next = this;
    internal_success = 0;
  }
  // When the functor gets to run in thread pool, it will take itself as first
  // argument and internal_success as second one.
  static void Run(grpc_experimental_completion_queue_functor* cb, int /*ok*/) {
    auto* callback = static_cast<AddSelfFunctor*>(cb);
    if (--callback->num_add_ > 0) {
      callback->pool_->Add(cb);
    } else {
      callback->counter_->DecrementCount();
      // Suicides.
      delete callback;
    }
  }

 private:
  grpc_core::ThreadPool* pool_;
  BlockingCounter* counter_;
  int num_add_;
};

template <int kConcurrentFunctor>
static void ThreadPoolAddSelf(benchmark::State& state) {
  const int num_iterations = state.range(0);
  const int num_threads = state.range(1);
  // Number of adds done by each closure.
  const int num_add = num_iterations / kConcurrentFunctor;
  grpc_core::ThreadPool pool(num_threads);
  while (state.KeepRunningBatch(num_iterations)) {
    BlockingCounter counter(kConcurrentFunctor);
    for (int i = 0; i < kConcurrentFunctor; ++i) {
      pool.Add(new AddSelfFunctor(&pool, &counter, num_add));
    }
    counter.Wait();
  }
  state.SetItemsProcessed(state.iterations());
}

// First pair of arguments is range for number of iterations (num_iterations).
// Second pair of arguments is range for thread pool size (num_threads).
BENCHMARK_TEMPLATE(ThreadPoolAddSelf, 1)->RangePair(524288, 524288, 1, 1024);
BENCHMARK_TEMPLATE(ThreadPoolAddSelf, 4)->RangePair(524288, 524288, 1, 1024);
BENCHMARK_TEMPLATE(ThreadPoolAddSelf, 8)->RangePair(524288, 524288, 1, 1024);
BENCHMARK_TEMPLATE(ThreadPoolAddSelf, 16)->RangePair(524288, 524288, 1, 1024);
BENCHMARK_TEMPLATE(ThreadPoolAddSelf, 32)->RangePair(524288, 524288, 1, 1024);
BENCHMARK_TEMPLATE(ThreadPoolAddSelf, 64)->RangePair(524288, 524288, 1, 1024);
BENCHMARK_TEMPLATE(ThreadPoolAddSelf, 128)->RangePair(524288, 524288, 1, 1024);
BENCHMARK_TEMPLATE(ThreadPoolAddSelf, 512)->RangePair(524288, 524288, 1, 1024);
BENCHMARK_TEMPLATE(ThreadPoolAddSelf, 2048)->RangePair(524288, 524288, 1, 1024);

#if defined(__GNUC__) && !defined(SWIG)
#if defined(__i386__) || defined(__x86_64__)
#define CACHELINE_SIZE 64
#elif defined(__powerpc64__)
#define CACHELINE_SIZE 128
#elif defined(__aarch64__)
#define CACHELINE_SIZE 64
#elif defined(__arm__)
#if defined(__ARM_ARCH_5T__)
#define CACHELINE_SIZE 32
#elif defined(__ARM_ARCH_7A__)
#define CACHELINE_SIZE 64
#endif
#endif
#ifndef CACHELINE_SIZE
#define CACHELINE_SIZE 64
#endif
#endif

// A functor (closure) that simulates closures with small but non-trivial amount
// of work.
class ShortWorkFunctorForAdd
    : public grpc_experimental_completion_queue_functor {
 public:
  BlockingCounter* counter_;

  ShortWorkFunctorForAdd() {
    functor_run = &ShortWorkFunctorForAdd::Run;
    inlineable = false;
    internal_next = this;
    internal_success = 0;
    val_ = 0;
  }
  static void Run(grpc_experimental_completion_queue_functor* cb, int /*ok*/) {
    auto* callback = static_cast<ShortWorkFunctorForAdd*>(cb);
    // Uses pad to avoid compiler complaining unused variable error.
    callback->pad[0] = 0;
    for (int i = 0; i < 1000; ++i) {
      callback->val_++;
    }
    callback->counter_->DecrementCount();
  }

 private:
  char pad[CACHELINE_SIZE];
  volatile int val_;
};

// Simulates workloads where many short running callbacks are added to the
// threadpool. The callbacks are not enough to keep all the workers busy
// continuously so the number of workers running changes overtime.
//
// In effect this tests how well the threadpool avoids spurious wakeups.
static void BM_SpikyLoad(benchmark::State& state) {
  const int num_threads = state.range(0);

  const int kNumSpikes = 1000;
  const int batch_size = 3 * num_threads;
  std::vector<ShortWorkFunctorForAdd> work_vector(batch_size);
  grpc_core::ThreadPool pool(num_threads);
  while (state.KeepRunningBatch(kNumSpikes * batch_size)) {
    for (int i = 0; i != kNumSpikes; ++i) {
      BlockingCounter counter(batch_size);
      for (auto& w : work_vector) {
        w.counter_ = &counter;
        pool.Add(&w);
      }
      counter.Wait();
    }
  }
  state.SetItemsProcessed(state.iterations() * batch_size);
}
BENCHMARK(BM_SpikyLoad)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16);

}  // namespace testing
}  // namespace grpc

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char* argv[]) {
  grpc::testing::TestEnvironment env(argc, argv);
  LibraryInitializer libInit;
  ::benchmark::Initialize(&argc, argv);
  ::grpc::testing::InitTest(&argc, &argv, false);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
