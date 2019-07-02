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

#include <condition_variable>

#include "src/core/lib/iomgr/executor/threadpool.h"
#include "test/cpp/microbenchmarks/helpers.h"
#include "test/cpp/util/test_config.h"

namespace grpc {
namespace testing {

// This helper class allows a thread to block for s pre-specified number of
// actions. BlockingCounter has a initial non-negative count on initialization
// Each call to DecrementCount will decrease the count by 1. When making a call
// to Wait, if the count is greater than 0, the thread will be block, until
// the count reaches 0, it will unblock.
class BlockingCounter {
 public:
  BlockingCounter(int count) : count_(count) {}
  void DecrementCount() {
    std::lock_guard<std::mutex> l(mu_);
    count_--;
    cv_.notify_one();
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
    internal_next = this;
    internal_success = 0;
  }
  ~AddAnotherFunctor() {}
  // When the functor gets to run in thread pool, it will take internal_next
  // as first argument and internal_success as second one. Therefore, the
  // first argument here would be the closure itself.
  static void Run(grpc_experimental_completion_queue_functor* cb, int ok) {
    auto* callback = static_cast<AddAnotherFunctor*>(cb);
    if (--callback->num_add_ > 0) {
      callback->pool_->Add(new AddAnotherFunctor(
          callback->pool_, callback->counter_, callback->num_add_));
    } else {
      callback->counter_->DecrementCount();
    }
    // Suicide
    delete callback;
  }

 private:
  grpc_core::ThreadPool* pool_;
  BlockingCounter* counter_;
  int num_add_;
};

void ThreadPoolAddAnotherHelper(benchmark::State& state,
                                int concurrent_functor) {
  const int num_threads = state.range(0);
  const int num_iterations = state.range(1);
  // number of adds done by each closure
  const int num_add = num_iterations / concurrent_functor;
  grpc_core::ThreadPool pool(num_threads);
  while (state.KeepRunningBatch(num_iterations)) {
    BlockingCounter* counter = new BlockingCounter(concurrent_functor);
    for (int i = 0; i < concurrent_functor; ++i) {
      pool.Add(new AddAnotherFunctor(&pool, counter, num_add));
    }
    counter->Wait();
    delete counter;
  }
  state.SetItemsProcessed(state.iterations());
}

// This benchmark will let a closure add a new closure into pool. Concurrent
// closures range from 1 to 2048
static void BM_ThreadPool1AddAnother(benchmark::State& state) {
  ThreadPoolAddAnotherHelper(state, 1);
}
BENCHMARK(BM_ThreadPool1AddAnother)
    ->UseRealTime()
    // First pair is range for number of threads in pool, second pair is range
    // for number of iterations
    ->Ranges({{1, 1024}, {524288, 2097152}});  // 512K ~ 2M

static void BM_ThreadPool4AddAnother(benchmark::State& state) {
  ThreadPoolAddAnotherHelper(state, 4);
}
BENCHMARK(BM_ThreadPool4AddAnother)
    ->UseRealTime()
    ->Ranges({{1, 1024}, {524288, 2097152}});

static void BM_ThreadPool8AddAnother(benchmark::State& state) {
  ThreadPoolAddAnotherHelper(state, 8);
}
BENCHMARK(BM_ThreadPool8AddAnother)
    ->UseRealTime()
    ->Ranges({{1, 1024}, {524288, 1048576}});  // 512K ~ 1M

static void BM_ThreadPool16AddAnother(benchmark::State& state) {
  ThreadPoolAddAnotherHelper(state, 16);
}
BENCHMARK(BM_ThreadPool16AddAnother)
    ->UseRealTime()
    ->Ranges({{1, 1024}, {524288, 1048576}});

static void BM_ThreadPool32AddAnother(benchmark::State& state) {
  ThreadPoolAddAnotherHelper(state, 32);
}
BENCHMARK(BM_ThreadPool32AddAnother)
    ->UseRealTime()
    ->Ranges({{1, 1024}, {524288, 1048576}});

static void BM_ThreadPool64AddAnother(benchmark::State& state) {
  ThreadPoolAddAnotherHelper(state, 64);
}
BENCHMARK(BM_ThreadPool64AddAnother)
    ->UseRealTime()
    ->Ranges({{1, 1024}, {524288, 1048576}});

static void BM_ThreadPool128AddAnother(benchmark::State& state) {
  ThreadPoolAddAnotherHelper(state, 128);
}
BENCHMARK(BM_ThreadPool128AddAnother)
    ->UseRealTime()
    ->Ranges({{1, 1024}, {524288, 1048576}});

static void BM_ThreadPool512AddAnother(benchmark::State& state) {
  ThreadPoolAddAnotherHelper(state, 512);
}
BENCHMARK(BM_ThreadPool512AddAnother)
    ->UseRealTime()
    ->Ranges({{1, 1024}, {524288, 1048576}});

static void BM_ThreadPool2048AddAnother(benchmark::State& state) {
  ThreadPoolAddAnotherHelper(state, 2048);
}
BENCHMARK(BM_ThreadPool2048AddAnother)
    ->UseRealTime()
    ->Ranges({{1, 1024}, {524288, 1048576}});

// A functor class that will delete self on end of running.
class SuicideFunctorForAdd : public grpc_experimental_completion_queue_functor {
 public:
  SuicideFunctorForAdd() {
    functor_run = &SuicideFunctorForAdd::Run;
    internal_next = this;
    internal_success = 0;
  }
  ~SuicideFunctorForAdd() {}
  static void Run(grpc_experimental_completion_queue_functor* cb, int ok) {
    // On running, the first argument would be internal_next, which is itself.
    delete cb;
  }
};

// Performs the scenario of external thread(s) adding closures into pool.
static void BM_ThreadPoolExternalAdd(benchmark::State& state) {
  const int num_threads = state.range(0);
  static grpc_core::ThreadPool* pool =
      grpc_core::New<grpc_core::ThreadPool>(num_threads);
  for (auto _ : state) {
    pool->Add(new SuicideFunctorForAdd());
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ThreadPoolExternalAdd)
    ->Range(1, 1024)
    ->ThreadRange(1, 1024)  // concurrent external thread(s) up to 1024
    ->UseRealTime();

// Functor (closure) that adds itself into pool repeatedly. By adding self, the
// overhead would be low and can measure the time of add more accurately.
class AddSelfFunctor : public grpc_experimental_completion_queue_functor {
 public:
  AddSelfFunctor(grpc_core::ThreadPool* pool, BlockingCounter* counter,
                 int num_add)
      : pool_(pool), counter_(counter), num_add_(num_add) {
    functor_run = &AddSelfFunctor::Run;
    internal_next = this;
    internal_success = 0;
  }
  ~AddSelfFunctor() {}
  // When the functor gets to run in thread pool, it will take internal_next
  // as first argument and internal_success as second one. Therefore, the
  // first argument here would be the closure itself.
  static void Run(grpc_experimental_completion_queue_functor* cb, int ok) {
    auto* callback = static_cast<AddSelfFunctor*>(cb);
    if (--callback->num_add_ > 0) {
      callback->pool_->Add(cb);
    } else {
      callback->counter_->DecrementCount();
      // Suicide
      delete callback;
    }
  }

 private:
  grpc_core::ThreadPool* pool_;
  BlockingCounter* counter_;
  int num_add_;
};

static void BM_ThreadPoolAddSelf(benchmark::State& state) {
  const int num_threads = state.range(0);
  const int kNumIteration = 524288;
  int concurrent_functor = num_threads;
  int num_add = kNumIteration / concurrent_functor;
  grpc_core::ThreadPool pool(num_threads);
  while (state.KeepRunningBatch(kNumIteration)) {
    BlockingCounter* counter = new BlockingCounter(concurrent_functor);
    for (int i = 0; i < concurrent_functor; ++i) {
      pool.Add(new AddSelfFunctor(&pool, counter, num_add));
    }
    counter->Wait();
    delete counter;
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_ThreadPoolAddSelf)->UseRealTime()->Range(1, 1024);

// A functor (closure) that simulates closures with small but non-trivial amount
// of work.
class ShortWorkFunctorForAdd
    : public grpc_experimental_completion_queue_functor {
 public:
  BlockingCounter* counter_;

  ShortWorkFunctorForAdd() {
    functor_run = &ShortWorkFunctorForAdd::Run;
    internal_next = this;
    internal_success = 0;
    val_ = 0;
  }
  ~ShortWorkFunctorForAdd() {}
  static void Run(grpc_experimental_completion_queue_functor* cb, int ok) {
    auto* callback = static_cast<ShortWorkFunctorForAdd*>(cb);
    for (int i = 0; i < 1000; ++i) {
      callback->val_++;
    }
    callback->counter_->DecrementCount();
  }

 private:
  int val_;
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
  while (state.KeepRunningBatch(kNumSpikes * batch_size)) {
    grpc_core::ThreadPool pool(num_threads);
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

int main(int argc, char** argv) {
  LibraryInitializer libInit;
  ::benchmark::Initialize(&argc, argv);
  ::grpc::testing::InitTest(&argc, &argv, false);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
