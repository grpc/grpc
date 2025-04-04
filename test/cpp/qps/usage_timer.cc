//
//
// Copyright 2015 gRPC authors.
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
//
//

#include "test/cpp/qps/usage_timer.h"

#include <grpc/support/time.h>

#include <fstream>
#include <sstream>
#include <string>

#include "absl/log/log.h"
#include "src/core/util/crash.h"
#ifdef __linux__
#include <sys/resource.h>
#include <sys/time.h>

static double time_double(struct timeval* tv) {
  return tv->tv_sec + (1e-6 * tv->tv_usec);
}
#endif

UsageTimer::UsageTimer() : start_(Sample()) {}

double UsageTimer::Now() {
  auto ts = gpr_now(GPR_CLOCK_REALTIME);
  return ts.tv_sec + (1e-9 * ts.tv_nsec);
}

static void get_resource_usage(double* utime, double* stime) {
#ifdef __linux__
  struct rusage usage;
  getrusage(RUSAGE_SELF, &usage);
  *utime = time_double(&usage.ru_utime);
  *stime = time_double(&usage.ru_stime);
#else
  *utime = 0;
  *stime = 0;
#endif
}

static void get_cpu_usage(unsigned long long* total_cpu_time,
                          unsigned long long* idle_cpu_time) {
#ifdef __linux__
  std::ifstream proc_stat("/proc/stat");
  proc_stat.ignore(5);
  std::string cpu_time_str;
  std::string first_line;
  std::getline(proc_stat, first_line);
  std::stringstream first_line_s(first_line);
  for (int i = 0; i < 10; ++i) {
    std::getline(first_line_s, cpu_time_str, ' ');
    *total_cpu_time += std::stol(cpu_time_str);
    if (i == 3) {
      *idle_cpu_time = std::stol(cpu_time_str);
    }
  }
#else
  // Use the parameters to avoid unused-parameter warning
  (void)total_cpu_time;
  (void)idle_cpu_time;
  LOG(INFO) << "get_cpu_usage(): Non-linux platform is not supported.";
#endif
}

UsageTimer::Result UsageTimer::Sample() {
  Result r;
  r.wall = Now();
  get_resource_usage(&r.user, &r.system);
  r.total_cpu_time = 0;
  r.idle_cpu_time = 0;
  get_cpu_usage(&r.total_cpu_time, &r.idle_cpu_time);
  return r;
}

UsageTimer::Result UsageTimer::Mark() const {
  Result s = Sample();
  Result r;
  r.wall = s.wall - start_.wall;
  r.user = s.user - start_.user;
  r.system = s.system - start_.system;
  r.total_cpu_time = s.total_cpu_time - start_.total_cpu_time;
  r.idle_cpu_time = s.idle_cpu_time - start_.idle_cpu_time;

  return r;
}
