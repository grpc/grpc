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

#ifndef TEST_QPS_INTERARRIVAL_H
#define TEST_QPS_INTERARRIVAL_H

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <vector>

#include <grpc++/support/config.h>

namespace grpc {
namespace testing {

// First create classes that define a random distribution
// Note that this code does not include C++-specific random distribution
// features supported in std::random. Although this would make this code easier,
// this code is required to serve as the template code for other language
// stacks. Thus, this code only uses a uniform distribution of doubles [0,1)
// and then provides the distribution functions itself.

class RandomDistInterface {
 public:
  RandomDistInterface() {}
  virtual ~RandomDistInterface() = 0;
  // Argument to transform is a uniform double in the range [0,1)
  virtual double transform(double uni) const = 0;
};

inline RandomDistInterface::~RandomDistInterface() {}

// ExpDist implements an exponential distribution, which is the
// interarrival distribution for a Poisson process. The parameter
// lambda is the mean rate of arrivals. This is the
// most useful distribution since it is actually additive and
// memoryless. It is a good representation of activity coming in from
// independent identical stationary sources. For more information,
// see http://en.wikipedia.org/wiki/Exponential_distribution

class ExpDist final : public RandomDistInterface {
 public:
  explicit ExpDist(double lambda) : lambda_recip_(1.0 / lambda) {}
  ~ExpDist() override {}
  double transform(double uni) const override {
    // Note: Use 1.0-uni above to avoid NaN if uni is 0
    return lambda_recip_ * (-log(1.0 - uni));
  }

 private:
  double lambda_recip_;
};

// A class library for generating pseudo-random interarrival times
// in an efficient re-entrant way. The random table is built at construction
// time, and each call must include the thread id of the invoker

class InterarrivalTimer {
 public:
  InterarrivalTimer() {}
  void init(const RandomDistInterface& r, int threads, int entries = 1000000) {
    for (int i = 0; i < entries; i++) {
      // rand is the only choice that is portable across POSIX and Windows
      // and that supports new and old compilers
      const double uniform_0_1 =
          static_cast<double>(rand()) / static_cast<double>(RAND_MAX);
      random_table_.push_back(
          static_cast<int64_t>(1e9 * r.transform(uniform_0_1)));
    }
    // Now set up the thread positions
    for (int i = 0; i < threads; i++) {
      thread_posns_.push_back(random_table_.begin() + (entries * i) / threads);
    }
  }
  virtual ~InterarrivalTimer(){};

  int64_t next(int thread_num) {
    auto ret = *(thread_posns_[thread_num]++);
    if (thread_posns_[thread_num] == random_table_.end())
      thread_posns_[thread_num] = random_table_.begin();
    return ret;
  }

 private:
  typedef std::vector<int64_t> time_table;
  std::vector<time_table::const_iterator> thread_posns_;
  time_table random_table_;
};
}
}

#endif
