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

#include <grpc/support/port_platform.h>

#include "src/core/lib/debug/trace.h"

#include <string>
#include <type_traits>
#include <utility>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/lib/config/config_vars.h"

int grpc_tracer_set_enabled(const char* name, int enabled);

namespace grpc_core {

TraceFlag* TraceFlagList::root_tracer_ = nullptr;

bool TraceFlagList::Set(absl::string_view name, bool enabled) {
  TraceFlag* t;
  if (name == "all") {
    for (t = root_tracer_; t; t = t->next_tracer_) {
      t->set_enabled(enabled);
    }
  } else if (name == "list_tracers") {
    LogAllTracers();
  } else if (name == "refcount") {
    for (t = root_tracer_; t; t = t->next_tracer_) {
      if (absl::StrContains(t->name_, "refcount")) {
        t->set_enabled(enabled);
      }
    }
  } else {
    bool found = false;
    for (t = root_tracer_; t; t = t->next_tracer_) {
      if (name == t->name_) {
        t->set_enabled(enabled);
        found = true;
      }
    }
    // check for unknowns, but ignore "", to allow to GRPC_TRACE=
    if (!found && !name.empty()) {
      gpr_log(GPR_ERROR, "Unknown trace var: '%s'", std::string(name).c_str());
      return false;  // early return
    }
  }
  return true;
}

void TraceFlagList::Add(TraceFlag* flag) {
  flag->next_tracer_ = root_tracer_;
  root_tracer_ = flag;
}

void TraceFlagList::LogAllTracers() {
  gpr_log(GPR_DEBUG, "available tracers:");
  for (TraceFlag* t = root_tracer_; t != nullptr; t = t->next_tracer_) {
    gpr_log(GPR_DEBUG, "\t%s", t->name_);
  }
}

void TraceFlagList::SaveTo(std::map<std::string, bool>& values) {
  for (TraceFlag* t = root_tracer_; t != nullptr; t = t->next_tracer_) {
    values[t->name_] = t->enabled();
  }
}

// Flags register themselves on the list during construction
TraceFlag::TraceFlag(bool default_enabled, const char* name) : name_(name) {
  static_assert(std::is_trivially_destructible<TraceFlag>::value,
                "TraceFlag needs to be trivially destructible.");
  set_enabled(default_enabled);
  TraceFlagList::Add(this);
}

SavedTraceFlags::SavedTraceFlags() { TraceFlagList::SaveTo(values_); }

void SavedTraceFlags::Restore() {
  for (const auto& flag : values_) {
    TraceFlagList::Set(flag.first, flag.second);
  }
}

namespace {
void ParseTracers(absl::string_view tracers) {
  for (auto s : absl::StrSplit(tracers, ',')) {
    s = absl::StripAsciiWhitespace(s);
    if (s.empty()) continue;
    if (s[0] == '-') {
      TraceFlagList::Set(s.substr(1), false);
    } else {
      TraceFlagList::Set(s, true);
    }
  }
}
}  // namespace

}  // namespace grpc_core

void grpc_tracer_init() {
  grpc_core::ParseTracers(grpc_core::ConfigVars::Get().Trace());
}

int grpc_tracer_set_enabled(const char* name, int enabled) {
  return grpc_core::TraceFlagList::Set(name, enabled != 0);
}
