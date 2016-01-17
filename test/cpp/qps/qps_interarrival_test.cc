/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <chrono>
#include <iostream>

// Use the C histogram rather than C++ to avoid depending on proto
#include <grpc/support/histogram.h>

#include "test/cpp/qps/interarrival.h"

using grpc::testing::RandomDist;
using grpc::testing::InterarrivalTimer;

static void RunTest(RandomDist &&r, int threads, std::string title) {
  InterarrivalTimer timer;
  timer.init(r, threads);
  gpr_histogram *h(gpr_histogram_create(0.01, 60e9));

  for (int i = 0; i < 10000000; i++) {
    for (int j = 0; j < threads; j++) {
      gpr_histogram_add(h, timer(j).count());
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
using grpc::testing::DetDist;
using grpc::testing::UniformDist;
using grpc::testing::ParetoDist;

int main(int argc, char **argv) {
  RunTest(ExpDist(10.0), 5, std::string("Exponential(10)"));
  RunTest(DetDist(5.0), 5, std::string("Det(5)"));
  RunTest(UniformDist(0.0, 10.0), 5, std::string("Uniform(1,10)"));
  RunTest(ParetoDist(1.0, 1.0), 5, std::string("Pareto(1,1)"));
  return 0;
}
