// Copyright 2025 gRPC authors.
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

#ifndef GRPC_TEST_CPP_SLEUTH_SLEUTH_H
#define GRPC_TEST_CPP_SLEUTH_SLEUTH_H

#include <string>
#include <vector>

#include "absl/functional/any_invocable.h"

namespace grpc_sleuth {

// Expects `argc` and `argv` to be the same as what's passed to main.
// Prints the result to print_fn when not null, or defaults to stdout.
int RunSleuth(int argc, char** argv,
              absl::AnyInvocable<void(std::string) const> print_fn = nullptr);

// Does NOT expect `args` to include the binary name.
// This is a wrapper for a cython API.
// - python_print: Pointer to a Python Callable[[str], None].
// - python_cb: Python bridge funciton to call python_print.
int RunSleuth_Wrapper(std::vector<std::string> args, void* python_print,
                      void (*python_cb)(void*, const std::string&));

}  // namespace grpc_sleuth

#endif  // GRPC_TEST_CPP_SLEUTH_SLEUTH_H
