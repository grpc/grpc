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

#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "tools/codegen/core/gen_stats/gen_stats_utils.h"

ABSL_FLAG(std::string, stats_data_yaml, "",
          "Path to the stats data yaml file.");
ABSL_FLAG(std::string, header_path, "", "Path to the stats data header file.");
ABSL_FLAG(std::string, cpp_path, "", "Path to the stats data cpp file.");

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  const std::string stats_data_file = absl::GetFlag(FLAGS_stats_data_yaml);
  const std::string header_path = absl::GetFlag(FLAGS_header_path);
  const std::string cpp_path = absl::GetFlag(FLAGS_cpp_path);
  if (stats_data_file.empty()) {
    LOG(ERROR) << "Stats data not specified. Skipping code generation ";
    return 1;
  }

  auto attrs_or = grpc_core::ParseStatsFromFile(stats_data_file);
  CHECK_OK(attrs_or);

  grpc_core::StatsDataGenerator generator(*attrs_or);
  std::string hdr_output;
  std::string src_output;
  if (!header_path.empty()) {
    generator.GenStatsDataHdr("", hdr_output);
    CHECK_OK(grpc_core::WriteToFile(header_path, hdr_output));
  }
  if (!cpp_path.empty()) {
    generator.GenStatsDataSrc(src_output);
    CHECK_OK(grpc_core::WriteToFile(cpp_path, src_output));
  }
  return 0;
}