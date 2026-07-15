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

#ifndef GRPC_TOOLS_CODEGEN_GENERATE_TRACE_FLAGS_H
#define GRPC_TOOLS_CODEGEN_GENERATE_TRACE_FLAGS_H

#include <string>
#include <vector>

#include "absl/strings/string_view.h"

namespace grpc_generator {

std::string GenerateHeader(const std::vector<std::string>& trace_flags_yaml);
std::string GenerateCpp(const std::vector<std::string>& trace_flags_yaml,
                        const std::string& header_prefix);
std::string GenerateMarkdown(const std::vector<std::string>& trace_flags_yaml);

}  // namespace grpc_generator

#endif  // GRPC_TOOLS_CODEGEN_GENERATE_TRACE_FLAGS_H
