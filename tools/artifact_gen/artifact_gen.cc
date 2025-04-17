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

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "extract_metadata_from_bazel_xml.h"
#include "metadata_for_wrapped_languages.h"
#include "render.h"
#include "utils.h"

ABSL_FLAG(std::vector<std::string>, extra_build_yaml, {},
          "Extra build.yaml files to merge");
ABSL_FLAG(bool, save_json, false, "Save the generated build.yaml to a file");

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  auto build_yaml = ExtractMetadataFromBazelXml();
  for (const auto& filename : absl::GetFlag(FLAGS_extra_build_yaml)) {
    build_yaml.update(LoadYaml(filename), true);
  }
  // TODO(ctiller): all the special yaml updates
  AddMetadataForWrappedLanguages(build_yaml);
  if (absl::GetFlag(FLAGS_save_json)) {
    std::ofstream ofs("build.json");
    ofs << build_yaml.dump(4);
  }
  RenderAllTemplates(build_yaml);
  return 0;
}
