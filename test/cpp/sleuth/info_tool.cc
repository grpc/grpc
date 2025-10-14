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

#include <iostream>

#include "absl/status/status.h"
#include "test/cpp/sleuth/tool.h"
#include "test/cpp/sleuth/version.h"

namespace grpc_sleuth {

SLEUTH_TOOL(info, "", "Print version information.") {
  std::cout << "Sleuth version " << kSleuthVersion << std::endl;
  return absl::OkStatus();
}

}  // namespace grpc_sleuth
