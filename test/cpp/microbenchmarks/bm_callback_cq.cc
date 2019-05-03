/*
 *
 * Copyright 2015 gRPC authors.
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

/* This benchmark exists to ensure that immediately-firing alarms are fast */

#include <benchmark/benchmark.h>
#include <grpc/grpc.h>
#include <grpcpp/alarm.h>
#include <grpcpp/completion_queue.h>
#include <grpcpp/impl/grpc_library.h>
#include "test/core/util/test_config.h"
#include "test/cpp/microbenchmarks/helpers.h"
#include "test/cpp/util/test_config.h"

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/surface/completion_queue.h"

namespace grpc {
namespace testing {

auto& force_library_initialization = Library::get();

class TagCallback : public grpc_experimental_completion_queue_functor {
 public:
  TagCallback(int* counter, int tag) : counter_(counter), tag_(tag) {
    functor_run = &TagCallback::Run;
  }
  ~TagCallback() {}
  static void Run(grpc_experimental_completion_queue_functor* cb, int ok) {
    GPR_ASSERT(static_cast<bool>(ok));
    auto* callback = static_cast<TagCallback*>(cb);
    *callback->counter_ += callback->tag_;
    grpc_core::Delete(callback);
  };

 private:
  int* counter_;
  int tag_;
};

class ShutdownCallback : public grpc_experimental_completion_queue_functor {
 public:
  ShutdownCallback(bool* done) : done_(done) {
    functor_run = &ShutdownCallback::Run;
  }
  ~ShutdownCallback() {}
  static void Run(grpc_experimental_completion_queue_functor* cb, int ok) {
    *static_cast<ShutdownCallback*>(cb)->done_ = static_cast<bool>(ok);
  }

 private:
  bool* done_;
};

/* helper for tests to shutdown correctly and tersely */
static void shutdown_and_destroy(grpc_completion_queue* cc) {
  grpc_completion_queue_shutdown(cc);
  grpc_completion_queue_destroy(cc);
}

static void do_nothing_end_completion(void* arg, grpc_cq_completion* c) {}

static void BM_Callback_CQ_Default_Polling(benchmark::State& state) {
  int tag_size = state.range(0);
  grpc_completion_queue* cc;
  void* tags[tag_size];
  grpc_cq_completion completions[GPR_ARRAY_SIZE(tags)];

  grpc_completion_queue_attributes attr;
  unsigned i;
  bool got_shutdown = false;
  ShutdownCallback shutdown_cb(&got_shutdown);

  attr.version = 2;
  attr.cq_completion_type = GRPC_CQ_CALLBACK;
  attr.cq_shutdown_cb = &shutdown_cb;
  while (state.KeepRunning()) {
    int sumtags = 0;
    int counter = 0;
    {
      // reset exec_ctx types
      grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
      grpc_core::ExecCtx exec_ctx;
      attr.cq_polling_type = GRPC_CQ_DEFAULT_POLLING;
      cc = grpc_completion_queue_create(
          grpc_completion_queue_factory_lookup(&attr), &attr, nullptr);

      for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
        tags[i] = static_cast<void*>(grpc_core::New<TagCallback>(&counter, i));
        sumtags += i;
      }

      for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
        GPR_ASSERT(grpc_cq_begin_op(cc, tags[i]));
        grpc_cq_end_op(cc, tags[i], GRPC_ERROR_NONE, do_nothing_end_completion,
                       nullptr, &completions[i]);
      }

      shutdown_and_destroy(cc);
    }
    GPR_ASSERT(sumtags == counter);
    GPR_ASSERT(got_shutdown);
    got_shutdown = false;
  }
}
BENCHMARK(BM_Callback_CQ_Default_Polling)->Range(1, 128 * 1024);

static void BM_Callback_CQ_Non_Listening(benchmark::State& state) {
  int tag_size = state.range(0);
  grpc_completion_queue* cc;
  void* tags[tag_size];
  grpc_cq_completion completions[GPR_ARRAY_SIZE(tags)];
  grpc_completion_queue_attributes attr;
  unsigned i;
  bool got_shutdown = false;
  ShutdownCallback shutdown_cb(&got_shutdown);

  attr.version = 2;
  attr.cq_completion_type = GRPC_CQ_CALLBACK;
  attr.cq_shutdown_cb = &shutdown_cb;
  while (state.KeepRunning()) {
    int sumtags = 0;
    int counter = 0;
    {
      // reset exec_ctx types
      grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
      grpc_core::ExecCtx exec_ctx;
      attr.cq_polling_type = GRPC_CQ_NON_LISTENING;
      cc = grpc_completion_queue_create(
          grpc_completion_queue_factory_lookup(&attr), &attr, nullptr);

      for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
        tags[i] = static_cast<void*>(grpc_core::New<TagCallback>(&counter, i));
        sumtags += i;
      }

      for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
        GPR_ASSERT(grpc_cq_begin_op(cc, tags[i]));
        grpc_cq_end_op(cc, tags[i], GRPC_ERROR_NONE, do_nothing_end_completion,
                       nullptr, &completions[i]);
      }

      shutdown_and_destroy(cc);
    }
    GPR_ASSERT(sumtags == counter);
    GPR_ASSERT(got_shutdown);
    got_shutdown = false;
  }
}
BENCHMARK(BM_Callback_CQ_Non_Listening)->Range(1, 128 * 1024);

static void BM_Callback_CQ_Non_Polling(benchmark::State& state) {
  int tag_size = state.range(0);
  grpc_completion_queue* cc;
  void* tags[tag_size];
  grpc_cq_completion completions[GPR_ARRAY_SIZE(tags)];
  grpc_completion_queue_attributes attr;
  unsigned i;
  bool got_shutdown = false;
  ShutdownCallback shutdown_cb(&got_shutdown);

  attr.version = 2;
  attr.cq_completion_type = GRPC_CQ_CALLBACK;
  attr.cq_shutdown_cb = &shutdown_cb;
  while (state.KeepRunning()) {
    int sumtags = 0;
    int counter = 0;
    {
      // reset exec_ctx types
      grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
      grpc_core::ExecCtx exec_ctx;
      attr.cq_polling_type = GRPC_CQ_DEFAULT_POLLING;
      cc = grpc_completion_queue_create(
          grpc_completion_queue_factory_lookup(&attr), &attr, nullptr);

      for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
        tags[i] = static_cast<void*>(grpc_core::New<TagCallback>(&counter, i));
        sumtags += i;
      }

      for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
        GPR_ASSERT(grpc_cq_begin_op(cc, tags[i]));
        grpc_cq_end_op(cc, tags[i], GRPC_ERROR_NONE, do_nothing_end_completion,
                       nullptr, &completions[i]);
      }

      shutdown_and_destroy(cc);
    }
    GPR_ASSERT(sumtags == counter);
    GPR_ASSERT(got_shutdown);
    got_shutdown = false;
  }
}
BENCHMARK(BM_Callback_CQ_Non_Polling)->Range(1, 128 * 1024);

}  // namespace testing
}  // namespace grpc

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  ::benchmark::Initialize(&argc, argv);
  ::grpc::testing::InitTest(&argc, &argv, false);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
