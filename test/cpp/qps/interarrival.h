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
#include <random>

#include <grpc++/config.h>

namespace grpc {
namespace testing {

// First create classes that define a random distribution
// Note that this code does not include C++-specific random distribution
// features supported in std::random. Although this would make this code easier,
// this code is required to serve as the template code for other language
// stacks. Thus, this code only uses a uniform distribution of doubles [0,1)
// and then provides the distribution functions itself.

class RandomDist {
 public:
  RandomDist() {}
  virtual ~RandomDist() = 0;
  // Argument to operator() is a uniform double in the range [0,1)
  virtual double operator()(double uni) const = 0;
};

inline RandomDist::~RandomDist() {}

// ExpDist implements an exponential distribution, which is the
// interarrival distribution for a Poisson process. The parameter
// lambda is the mean rate of arrivals. This is the
// most useful distribution since it is actually additive and
// memoryless. It is a good representation of activity coming in from
// independent identical stationary sources. For more information,
// see http://en.wikipedia.org/wiki/Exponential_distribution

class ExpDist GRPC_FINAL : public RandomDist {
 public:
  explicit ExpDist(double lambda) : lambda_recip_(1.0 / lambda) {}
  ~ExpDist() GRPC_OVERRIDE {}
  double operator()(double uni) const GRPC_OVERRIDE {
    // Note: Use 1.0-uni above to avoid NaN if uni is 0
    return lambda_recip_ * (-log(1.0 - uni));
  }

 private:
  double lambda_recip_;
};

// UniformDist implements a random distribution that has
// interarrival time uniformly spread between [lo,hi). The
// mean interarrival time is (lo+hi)/2. For more information,
// see http://en.wikipedia.org/wiki/Uniform_distribution_%28continuous%29

class UniformDist GRPC_FINAL : public RandomDist {
 public:
  UniformDist(double lo, double hi) : lo_(lo), range_(hi - lo) {}
  ~UniformDist() GRPC_OVERRIDE {}
  double operator()(double uni) const GRPC_OVERRIDE {
    return uni * range_ + lo_;
  }

 private:
  double lo_;
  double range_;
};

// DetDist provides a random distribution with interarrival time
// of val. Note that this is not additive, so using this on multiple
// flows of control (threads within the same client or separate
// clients) will not preserve any deterministic interarrival gap across
// requests.

class DetDist GRPC_FINAL : public RandomDist {
 public:
  explicit DetDist(double val) : val_(val) {}
  ~DetDist() GRPC_OVERRIDE {}
  double operator()(double uni) const GRPC_OVERRIDE { return val_; }

 private:
  double val_;
};

// ParetoDist provides a random distribution with interarrival time
// spread according to a Pareto (heavy-tailed) distribution. In this
// model, many interarrival times are close to the base, but a sufficient
// number will be high (up to infinity) as to disturb the mean. It is a
// good representation of the response times of data center jobs. See
// http://en.wikipedia.org/wiki/Pareto_distribution

class ParetoDist GRPC_FINAL : public RandomDist {
 public:
  ParetoDist(double base, double alpha)
      : base_(base), alpha_recip_(1.0 / alpha) {}
  ~ParetoDist() GRPC_OVERRIDE {}
  double operator()(double uni) const GRPC_OVERRIDE {
    // Note: Use 1.0-uni above to avoid div by zero if uni is 0
    return base_ / pow(1.0 - uni, alpha_recip_);
  }

 private:
  double base_;
  double alpha_recip_;
};

// A class library for generating pseudo-random interarrival times
// in an efficient re-entrant way. The random table is built at construction
// time, and each call must include the thread id of the invoker

typedef std::default_random_engine qps_random_engine;

class InterarrivalTimer {
 public:
  InterarrivalTimer() {}
  void init(const RandomDist& r, int threads, int entries = 1000000) {
    qps_random_engine gen;
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    for (int i = 0; i < entries; i++) {
      random_table_.push_back(std::chrono::nanoseconds(
          static_cast<int64_t>(1e9 * r(uniform(gen)))));
    }
    // Now set up the thread positions
    for (int i = 0; i < threads; i++) {
      thread_posns_.push_back(random_table_.begin() + (entries * i) / threads);
    }
  }
  virtual ~InterarrivalTimer(){};

  std::chrono::nanoseconds operator()(int thread_num) {
    auto ret = *(thread_posns_[thread_num]++);
    if (thread_posns_[thread_num] == random_table_.end())
      thread_posns_[thread_num] = random_table_.begin();
    return ret;
  }

 private:
  typedef std::vector<std::chrono::nanoseconds> time_table;
  std::vector<time_table::const_iterator> thread_posns_;
  time_table random_table_;
};
}
}

#endif
