// Copyright 2022 gRPC authors.
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

#include "test/core/memory_usage/memstats.h"

#include <unistd.h>

#include <fstream>
#include <string>

#include "absl/strings/str_cat.h"

#include <grpc/support/log.h>

long GetMemUsage(absl::optional<int> pid) {
  // Default is getting memory usage for self (calling process)
  std::string path = "/proc/self/stat";
  if (pid != absl::nullopt) {
    path = absl::StrCat("/proc/", pid.value(), "/stat");
  }
  std::ifstream stat_stream(path, std::ios_base::in);

  double resident_set = 0.0;
  // Temporary variables for irrelevant leading entries in stats
  std::string temp_pid, comm, state, ppid, pgrp, session, tty_nr;
  std::string tpgid, flags, minflt, cminflt, majflt, cmajflt;
  std::string utime, stime, cutime, cstime, priority, nice;
  std::string O, itrealvalue, starttime, vsize;

  // Get rss to find memory usage
  long rss;
  stat_stream >> temp_pid >> comm >> state >> ppid >> pgrp >> session >>
      tty_nr >> tpgid >> flags >> minflt >> cminflt >> majflt >> cmajflt >>
      utime >> stime >> cutime >> cstime >> priority >> nice >> O >>
      itrealvalue >> starttime >> vsize >> rss;
  stat_stream.close();

  // pid does not connect to an existing process
  GPR_ASSERT(!state.empty());

  // Calculations in case x86-64 is configured to use 2MB pages
  long page_size_kb = sysconf(_SC_PAGE_SIZE) / 1024;
  resident_set = rss * page_size_kb;
  // Memory in KB
  return resident_set;
}
