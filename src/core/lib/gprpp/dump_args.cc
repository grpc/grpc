// Copyright 2024 gRPC authors.
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

#include "src/core/lib/gprpp/dump_args.h"

#include "absl/log/check.h"
#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace dump_args_detail {

void DumpArgs::Stringify(CustomSink& sink) const {
  // Parse the argument string into a vector of keys.
  // #__VA_ARGS__ produces a stringified version of the arguments passed to the
  // macro. It's comma separated, and we can use that to split the string into
  // keys. Those keys might include parenthesis for e.g. argument lists, and so
  // we need to skip commas that are inside parenthesis.
  std::vector<absl::string_view> keys;
  int depth = 0;
  const char* start = arg_string_;
  for (const char* p = arg_string_; *p; ++p) {
    if (*p == '(') {
      ++depth;
    } else if (*p == ')') {
      --depth;
    } else if (*p == ',' && depth == 0) {
      keys.push_back(absl::string_view(start, p - start));
      start = p + 1;
    }
  }
  keys.push_back(start);
  CHECK_EQ(keys.size(), arg_dumpers_.size());
  for (size_t i = 0; i < keys.size(); i++) {
    if (i != 0) sink.Append(", ");
    sink.Append(absl::StripAsciiWhitespace(keys[i]));
    sink.Append(" = ");
    arg_dumpers_[i](sink);
  }
}

}  // namespace dump_args_detail
}  // namespace grpc_core
