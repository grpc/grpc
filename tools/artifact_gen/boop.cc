#include <iostream>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "extract_bazelmod_repositories.h"

int main(int argc, const char* argv[]) {
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverity::kInfo);
  if (argc < 2) {
    std::cerr << "Bazel output file was not specified\n";
    return 1;
  }
  auto archives = BazelModRepository::ParseBazelOutput(argv[1]);
  if (!archives.ok()) {
    LOG(FATAL) << archives.status();
  }
  for (const BazelModRepository& archive : *archives) {
    LOG(INFO) << archive;
  }
}