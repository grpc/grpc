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

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "tools/codegen/core/generate_trace_flags.h"

ABSL_FLAG(std::vector<std::string>, trace_flags_yaml, {},
          "Path to the trace flags yaml file(s).");
ABSL_FLAG(std::string, header_path, "", "Path to the header file.");
ABSL_FLAG(std::string, header_prefix, "", "Prefix to the header file.");
ABSL_FLAG(std::string, cpp_path, "", "Path to the cpp file.");
ABSL_FLAG(std::string, markdown_path, "", "Path to the markdown file.");

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  const std::vector<std::string> trace_flags_yaml =
      absl::GetFlag(FLAGS_trace_flags_yaml);
  const std::string header_path = absl::GetFlag(FLAGS_header_path);
  const std::string cpp_path = absl::GetFlag(FLAGS_cpp_path);
  const std::string markdown_path = absl::GetFlag(FLAGS_markdown_path);
  const std::string header_prefix = absl::GetFlag(FLAGS_header_prefix);
  if (!header_path.empty()) {
    std::string header = grpc_generator::GenerateHeader(trace_flags_yaml);
    if (!header.empty()) {
      std::ofstream header_file(header_path);
      header_file << header;
    }
  }
  if (!cpp_path.empty()) {
    std::string cpp =
        grpc_generator::GenerateCpp(trace_flags_yaml, header_prefix);
    if (!cpp.empty()) {
      std::ofstream cpp_file(cpp_path);
      cpp_file << cpp;
    }
  }
  if (!markdown_path.empty()) {
    std::string markdown = grpc_generator::GenerateMarkdown(trace_flags_yaml);
    if (!markdown.empty()) {
      std::ofstream markdown_file(markdown_path);
      markdown_file << markdown;
    }
  }
  return 0;
}