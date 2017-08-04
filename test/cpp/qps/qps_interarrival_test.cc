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

#include <chrono>
#include <iostream>

// Use the C histogram rather than C++ to avoid depending on proto
#include <grpc/support/histogram.h>

#include "test/cpp/qps/interarrival.h"
#include "test/cpp/util/test_config.h"

using grpc::testing::RandomDistInterface;
using grpc::testing::InterarrivalTimer;

static void RunTest(RandomDistInterface &&r, int threads, std::string title) {
  InterarrivalTimer timer;
  timer.init(r, threads);
  gpr_histogram *h(gpr_histogram_create(0.01, 60e9));

  for (int i = 0; i < 10000000; i++) {
    for (int j = 0; j < threads; j++) {
      gpr_histogram_add(h, timer.next(j));
    }
  }

  std::cout << title << " Distribution" << std::endl;
  std::cout << "Value, Percentile" << std::endl;
  for (double pct = 0.0; pct < 100.0; pct += 1.0) {
    std::cout << gpr_histogram_percentile(h, pct) << "," << pct << std::endl;
  }

  gpr_histogram_destroy(h);
}

using grpc::testing::ExpDist;

int main(int argc, char **argv) {
  grpc::testing::InitTest(&argc, &argv, true);

  RunTest(ExpDist(10.0), 5, std::string("Exponential(10)"));
  return 0;
}
