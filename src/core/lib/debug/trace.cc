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

#include "src/core/lib/debug/trace.h"

#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>

#include <string>
#include <type_traits>
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "src/core/config/config_vars.h"
#include "src/core/util/glob.h"

int grpc_tracer_set_enabled(const char* name, int enabled);

namespace grpc_core {
namespace {
void LogAllTracers() {
  VLOG(2) << "available tracers:";
  for (const auto& name : GetAllTraceFlags()) {
    LOG(INFO) << "  " << name.first;
  }
}

}  // namespace

// Flags register themselves on the list during construction
TraceFlag::TraceFlag(bool default_enabled, const char* name) : name_(name) {
  static_assert(std::is_trivially_destructible<TraceFlag>::value,
                "TraceFlag needs to be trivially destructible.");
  set_enabled(default_enabled);
}

SavedTraceFlags::SavedTraceFlags() {
  for (const auto& flag : GetAllTraceFlags()) {
    values_[flag.first] = {flag.second->enabled(), flag.second};
  }
}

void SavedTraceFlags::Restore() {
  for (const auto& flag : values_) {
    flag.second.second->set_enabled(flag.second.first);
  }
}

bool ParseTracers(absl::string_view tracers) {
  std::string enabled_tracers;
  bool some_trace_was_found = false;
  for (auto trace_glob : absl::StrSplit(tracers, ',', absl::SkipWhitespace())) {
    if (trace_glob == "list_tracers") {
      LogAllTracers();
      continue;
    }
    bool enabled = !absl::ConsumePrefix(&trace_glob, "-");
    if (trace_glob == "all") trace_glob = "*";
    if (trace_glob == "refcount") trace_glob = "*refcount*";
    bool found = false;
    for (const auto& flag : GetAllTraceFlags()) {
      if (GlobMatch(flag.first, trace_glob)) {
        flag.second->set_enabled(enabled);
        if (enabled) absl::StrAppend(&enabled_tracers, flag.first, ", ");
        found = true;
        some_trace_was_found = true;
      }
    }
    if (!found) LOG(ERROR) << "Unknown tracer: " << trace_glob;
  }
  if (!enabled_tracers.empty()) {
    absl::string_view enabled_tracers_view(enabled_tracers);
    absl::ConsumeSuffix(&enabled_tracers_view, ", ");
    LOG(INFO) << "gRPC Tracers: " << enabled_tracers_view;
  }
  return some_trace_was_found;
}

}  // namespace grpc_core

void grpc_tracer_init() {
  grpc_core::ParseTracers(grpc_core::ConfigVars::Get().Trace());
}

int grpc_tracer_set_enabled(const char* name, int enabled) {
  if (enabled != 0) return grpc_core::ParseTracers(name);
  return grpc_core::ParseTracers(absl::StrCat("-", name));
}
