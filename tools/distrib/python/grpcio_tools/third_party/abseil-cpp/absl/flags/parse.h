//
// Copyright 2019 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// -----------------------------------------------------------------------------
// File: parse.h
// -----------------------------------------------------------------------------
//
// This file defines the main parsing function for Abseil flags:
// `absl::ParseCommandLine()`.

#ifndef ABSL_FLAGS_PARSE_H_
#define ABSL_FLAGS_PARSE_H_

#include <vector>

#include "absl/base/config.h"
#include "absl/flags/internal/parse.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// ParseCommandLine()
//
// Parses the set of command-line arguments passed in the `argc` (argument
// count) and `argv[]` (argument vector) parameters from `main()`, assigning
// values to any defined Abseil flags. (Any arguments passed after the
// flag-terminating delimiter (`--`) are treated as positional arguments and
// ignored.)
//
// Any command-line flags (and arguments to those flags) are parsed into Abseil
// Flag values, if those flags are defined. Any undefined flags will either
// return an error, or be ignored if that flag is designated using `undefok` to
// indicate "undefined is OK."
//
// Any command-line positional arguments not part of any command-line flag (or
// arguments to a flag) are returned in a vector, with the program invocation
// name at position 0 of that vector. (Note that this includes positional
// arguments after the flag-terminating delimiter `--`.)
//
// After all flags and flag arguments are parsed, this function looks for any
// built-in usage flags (e.g. `--help`), and if any were specified, it reports
// help messages and then exits the program.
std::vector<char*> ParseCommandLine(int argc, char* argv[]);

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_FLAGS_PARSE_H_
