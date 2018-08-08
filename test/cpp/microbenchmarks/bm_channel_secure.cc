/*
 *
 * Copyright 2018 gRPC authors.
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

/* Benchmark channel */

#include <benchmark/benchmark.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <chrono>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/security/credentials/ssl/ssl_credentials.h"
#include "test/cpp/microbenchmarks/helpers.h"
#include "test/cpp/util/test_config.h"

auto& force_library_initialization = Library::get();

// Total number of initial channels used for benchmarking.
const size_t kNumberOfChannels = 512;

class ChannelDestroyerFixture {
 public:
  ChannelDestroyerFixture() {}
  virtual ~ChannelDestroyerFixture() {
    if (channel_) {
      grpc_channel_destroy(channel_);
    }
  }
  virtual void Init() = 0;

 protected:
  grpc_channel* channel_ = nullptr;
};

namespace grpc_core {
class TestDefaultSslRootStore : public DefaultSslRootStore {
 public:
  static grpc_slice ComputePemRootCertsForTesting() {
    return ComputePemRootCerts();
  }
};
}  // namespace grpc_core

class SecureChannelFixture : public ChannelDestroyerFixture {
 public:
  SecureChannelFixture() {}
  void Init() override {
    grpc_channel_credentials* channel_creds = grpc_ssl_credentials_create(
        /*pem root certs*/ nullptr,
        /*pem key cert pair*/ nullptr,
        /*verify peer options*/ nullptr,
        /*reserved*/ nullptr);

    channel_ = grpc_secure_channel_create(channel_creds, "localhost:1234",
                                          /*grpc channel args*/ nullptr,
                                          /*reserved*/ nullptr);
  }
};

template <class Fixture>
static void BM_SecureChannelCreateDestroy(benchmark::State& state) {
  // In order to test if channel creation time is affected by the number of
  // already existing channels, we create some initial channels here.
  Fixture initial_channels[kNumberOfChannels];
  for (int i = 0; i < state.range(0); i++) {
    initial_channels[i].Init();
  }
  while (state.KeepRunning()) {
    Fixture channel;
    channel.Init();
  }
}
BENCHMARK_TEMPLATE(BM_SecureChannelCreateDestroy, SecureChannelFixture)
    ->Range(0, kNumberOfChannels);

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  ::benchmark::Initialize(&argc, argv);
  ::grpc::testing::InitTest(&argc, &argv, false);

  // kTotalIterations is the number of times we compute the system roots.
  // This value is then used to calculate the average runtime.
  const int kTotalIterations = 1000;
  grpc_slice result_slice = grpc_empty_slice();

  // System roots feature disabled.
  auto start = std::chrono::high_resolution_clock::now();
  if (gpr_getenv("GRPC_USE_SYSTEM_SSL_ROOTS") != nullptr) {
    unsetenv("GRPC_USE_SYSTEM_SSL_ROOTS");
  }
  if (gpr_getenv("GRPC_SYSTEM_SSL_ROOTS_DIR") != nullptr) {
    unsetenv("GRPC_SYSTEM_SSL_ROOTS_DIR");
  }
  for (int i = 0; i < kTotalIterations; i++) {
    result_slice = grpc_core::TestDefaultSslRootStore::ComputePemRootCertsForTesting();
    grpc_slice_unref(result_slice);
  }
  auto finish = std::chrono::high_resolution_clock::now();
  auto time_elapsed0 =
      std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start)
          .count();

  // System roots feature using roots.pem.
  start = std::chrono::high_resolution_clock::now();
  gpr_setenv("GRPC_USE_SYSTEM_SSL_ROOTS", "1");
  gpr_setenv("GRPC_SYSTEM_SSL_ROOTS_DIR", "./etc");
  for (int i = 0; i < kTotalIterations; i++) {
    result_slice = grpc_core::TestDefaultSslRootStore::ComputePemRootCertsForTesting();
    grpc_slice_unref(result_slice);
  }
  finish = std::chrono::high_resolution_clock::now();
  auto time_elapsed1 =
      std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start)
          .count();

  // System feature enabled.
  start = std::chrono::high_resolution_clock::now();
  unsetenv("GRPC_SYSTEM_SSL_ROOTS_DIR");
  for (int i = 0; i < kTotalIterations; i++) {
    result_slice = grpc_core::TestDefaultSslRootStore::ComputePemRootCertsForTesting();
    grpc_slice_unref(result_slice);
  }
  finish = std::chrono::high_resolution_clock::now();
  auto time_elapsed2 =
      std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start)
          .count();

  benchmark::RunTheBenchmarksNamespaced();

  std::cout << "\nRoot certs computation took: "
            << time_elapsed0 / kTotalIterations
            << " nanoseconds, with the feature disabled"
            << "\n";

  std::cout << "Root certs computation took: "
            << time_elapsed1 / kTotalIterations
            << " nanoseconds, with the feature using roots.pem"
            << "\n";

  std::cout << "Root certs computation took: "
            << time_elapsed2 / kTotalIterations
            << " nanoseconds, with the feature enabled (uses system roots)"
            << "\n\n";
  return 0;
}
