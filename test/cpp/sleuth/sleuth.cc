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

#include "test/cpp/sleuth/sleuth.h"

#include <string>
#include <utility>
#include <vector>

#include "test/cpp/sleuth/tool.h"
#include "test/cpp/sleuth/version.h"
#include "test/cpp/util/test_config.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/status/status.h"

namespace {
void Usage(const absl::AnyInvocable<void(std::string) const>& print_fn) {
  print_fn(std::string("Sleuth version ") +
           std::to_string(grpc_sleuth::kSleuthVersion) + "\n");
  print_fn("Usage: sleuth <tool> [args...]\n");
  for (const auto& tool : grpc_sleuth::ToolRegistry::Get()->GetMetadata()) {
    print_fn(std::string("  ") + std::string(tool.name) + " " +
             std::string(tool.args) + ": " + std::string(tool.description) +
             "\n");
  }
  print_fn("Run 'sleuth --help' for more information.\n");
}
}  // namespace

namespace grpc_sleuth {

int RunSleuth(int argc, char** argv,
              absl::AnyInvocable<void(std::string) const> print_fn) {
  static bool init_done = [&]() {
    grpc::testing::InitTest(&argc, &argv, /*remove_flags=*/true);
    return true;
  }();
  (void)init_done;  // Unused.
  if (print_fn == nullptr) {
    print_fn = PrintStdout;
  }
  CHECK_GT(grpc_sleuth::ToolRegistry::Get()->GetMetadata().size(), 0u);
  if (argc < 2) {
    Usage(print_fn);
    return 1;
  }
  auto tool_name = argv[1];
  auto tool = grpc_sleuth::ToolRegistry::Get()->GetTool(tool_name);
  if (tool == nullptr) {
    print_fn(std::string("Unknown tool: ") + tool_name + "\n");
    Usage(print_fn);
    return 1;
  }
  auto tool_args = grpc_sleuth::ToolArgs::TryCreate(
      std::vector<std::string>(argv + 2, argv + argc));
  if (!tool_args.ok()) {
    print_fn(std::string("Argument parsing failed: ") +
             tool_args.status().ToString() + "\n");
    return 1;
  }
  auto status = tool(*tool_args.value(), print_fn);
  if (!status.ok()) {
    print_fn(std::string("Tool failed: ") + status.ToString() + "\n");
    return 1;
  }
  return 0;
}

int RunSleuth_Wrapper(std::vector<std::string> args, void* python_print,
                      void (*python_cb)(void*, const std::string&)) {
  std::vector<char*> argv_vec;
  argv_vec.reserve(args.size() + 1);
  std::string binary_name = "sleuth";
  argv_vec.push_back(const_cast<char*>(binary_name.c_str()));
  for (auto& arg : args) {
    argv_vec.push_back(const_cast<char*>(arg.c_str()));
  }
  absl::AnyInvocable<void(std::string) const> print_fn = nullptr;
  if (python_print != nullptr) {
    print_fn = [python_print, python_cb](const std::string& message) {
      python_cb(python_print, message);
    };
  }
  return RunSleuth(argv_vec.size(), argv_vec.data(), std::move(print_fn));
}

}  // namespace grpc_sleuth
