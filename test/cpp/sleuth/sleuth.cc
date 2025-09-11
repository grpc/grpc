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

#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "test/cpp/sleuth/tool.h"
#include "test/cpp/util/test_config.h"

void UsageThenDie() {
  std::cerr << "Usage: sleuth <tool> [args...]\n";
  for (const auto& tool : grpc_sleuth::ToolRegistry::Get()->GetMetadata()) {
    std::cerr << "  " << tool.name << " " << tool.args << ": "
              << tool.description << "\n";
  }
  std::cerr << "Run 'sleuth --help' for more information.\n";
  exit(1);
}

int main(int argc, char** argv) {
  CHECK_GT(grpc_sleuth::ToolRegistry::Get()->GetMetadata().size(), 0u);
  grpc::testing::InitTest(&argc, &argv, /*remove_flags=*/true);
  if (argc < 2) UsageThenDie();
  auto tool_name = argv[1];
  auto tool = grpc_sleuth::ToolRegistry::Get()->GetTool(tool_name);
  if (tool == nullptr) {
    std::cerr << "Unknown tool: " << tool_name << "\n";
    UsageThenDie();
  }
  auto status = tool(std::vector<std::string>(argv + 2, argv + argc));
  if (!status.ok()) {
    std::cerr << "Tool failed: " << status << "\n";
    exit(1);
  }
  return 0;
}
