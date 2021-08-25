// Copyright 2021 gRPC authors.
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

#include "test/cpp/microbenchmarks/bm_callback_ping_pong_binder.h"

#ifdef GPR_ANDROID

#include <android/log.h>

namespace grpc {
namespace testing {

static void SweepSizesArgs(benchmark::internal::Benchmark* b) {
  b->Args({0, 0});
  for (int i = 1; i <= 128 * 1024 * 1024; i *= 8) {
    b->Args({i, 0});
    b->Args({0, i});
    b->Args({i, i});
  }
}

static void StreamingPingPongMsgSizeArgs(benchmark::internal::Benchmark* b) {
  // base case: 0 byte ping-pong msgs
  b->Args({0, 1});
  b->Args({0, 2});

  for (int msg_size = 1; msg_size <= 128 * 1024 * 1024; msg_size *= 8) {
    b->Args({msg_size, 1});
    b->Args({msg_size, 2});
  }
}

static void StreamingPingPongMsgsNumberArgs(benchmark::internal::Benchmark* b) {
  for (int msg_number = 1; msg_number <= 256 * 1024; msg_number *= 8) {
    b->Args({0, msg_number});
    b->Args({1024, msg_number});
  }
}

namespace {

class AndroidReporter : public benchmark::BenchmarkReporter {
 public:
  bool ReportContext(const Context& /*context*/) override { return true; }
  void ReportRuns(const std::vector<Run>& report) override {
    for (Run run : report) {
      const std::string time_label =
          benchmark::GetTimeUnitString(run.time_unit);
      __android_log_print(
          ANDROID_LOG_INFO, "Benchmark",
          "name = %-30s\treal-time = %10.0f %-4s\tcpu-time = %10.0f "
          "%-4s\titerations = %10lld",
          run.benchmark_name().c_str(), run.GetAdjustedRealTime(),
          time_label.c_str(), run.GetAdjustedCPUTime(), time_label.c_str(),
          run.iterations);
    }
  }
};

}  // namespace

void RunCallbackPingPongBinderBenchmarks(JNIEnv* env, jobject application) {
  benchmark::RegisterBenchmark("binder-unary", BM_CallbackUnaryPingPongBinder,
                               env, application)
      ->Apply(SweepSizesArgs);
  benchmark::RegisterBenchmark("binder-streaming",
                               BM_CallbackBidiStreamingBinder, env, application)
      ->Apply(StreamingPingPongMsgSizeArgs);
  benchmark::RegisterBenchmark("binder-streaming",
                               BM_CallbackBidiStreamingBinder, env, application)
      ->Apply(StreamingPingPongMsgsNumberArgs);
  int argc = 1;
  char* argv[] = {const_cast<char*>("benchmark")};
  std::unique_ptr<benchmark::BenchmarkReporter> reporter =
      absl::make_unique<AndroidReporter>();
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks(reporter.get());
}

}  // namespace testing
}  // namespace grpc

#endif  // GPR_ANDROID
