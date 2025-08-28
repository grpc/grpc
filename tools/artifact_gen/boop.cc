#include <iostream>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "tools/artifact_gen/extract_metadata_from_bazel_mod.h"

int main(int argc, const char* argv[]) {
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverity::kInfo);
  if (argc < 2) {
    std::cerr << "Bazel output file was not specified\n";
    return 1;
  }
  auto archives = HttpArchive::ParseHttpArchives(argv[1]);
  if (!archives.ok()) {
    LOG(FATAL) << archives.status();
  }
  for (const HttpArchive& archive : *archives) {
    LOG(INFO) << archive;
  }
}