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

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_set>

#include "absl/container/inlined_vector.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"
#include "absl/strings/substitute.h"
#include "re2/re2.h"

namespace {

class HttpArchive {
 public:
  explicit HttpArchive(std::string_view alias) : alias_(alias) {}

  std::string alias() const { return alias_; }

  std::string integrity() const { return integrity_; }

  std::string strip_prefix() const { return strip_prefix_; }

  void set_integrity(std::string_view integrity) { integrity_ = integrity; }

  void set_strip_prefix(std::string_view strip_prefix) {
    strip_prefix_ = strip_prefix;
  }

  void set_urls(absl::Span<const std::string> urls) {
    std::copy(urls.begin(), urls.end(), std::back_inserter(urls_));
  }

  template <typename Sink>
  friend void AbslStringify(Sink& s, const HttpArchive& archive) {
    s.Append(absl::StrFormat(
        "%s = { integrity = \"%s\", strip_prefix = \"%s\", urls = [%s] }",
        archive.alias_, archive.integrity_, archive.strip_prefix_,
        absl::StrJoin(archive.urls_, ", ",
                      [](std::string* dest, const std::string& url) {
                        absl::StrAppend(dest, "\"", url, "\"");
                      })));
  }

 private:
  // if match[1] in {"integrity", "name", "sha256", "strip_prefix", "urls"}:
  std::string alias_;
  std::string integrity_;
  std::string strip_prefix_;
  absl::InlinedVector<std::string, 3> urls_;
};

class HttpArchiveParser {
 public:
  absl::Status Parse(std::string_view line) {
    static const std::unordered_set<std::string_view> kIgnoredAttributes = {
        "name",
        "patch_strip",
        "patches",
        "remote_file_integrity",
        "remote_file_urls",
        "remote_patches",
        "remote_patch_strip"};
    std::array<std::string, 2> groups;
    // Drop comment lines
    if (RE2::FullMatch(line, "^# .*$") || line == "") {
      return absl::OkStatus();
    }
    // Module name
    if (RE2::FullMatch(line, "^## (.*):$", &groups[0])) {
      if (current_.has_value()) {
        return absl::FailedPreconditionError(
            absl::StrFormat("Rule %s started before rule %s was closed",
                            groups[0], current_->alias()));
      }
      current_.emplace(groups[0]);
      return absl::OkStatus();
    }
    if (!current_.has_value()) {
      return absl::FailedPreconditionError(
          absl::StrFormat("Line \"%s\" is outside any rule", line));
    }
    if (line == "http_archive(") {
      return absl::OkStatus();
    }
    if (line == ")") {
      archives_.emplace_back(std::move(current_).value());
      current_.reset();
      return absl::OkStatus();
    }
    if (RE2::FullMatch(line, "^  (\\w+) = (.+),$", &groups[0], &groups[1])) {
      if (kIgnoredAttributes.find(groups[0]) != kIgnoredAttributes.end()) {
        // Ignore
      } else if (groups[0] == "integrity") {
        current_->set_integrity(
            absl::StripSuffix(absl::StripPrefix(groups[1], "\""), "\""));
      } else if (groups[0] == "strip_prefix") {
        current_->set_strip_prefix(
            absl::StripSuffix(absl::StripPrefix(groups[1], "\""), "\""));
      } else if (groups[0] == "urls") {
        absl::InlinedVector<std::string, 5> urls = absl::StrSplit(
            absl::StripPrefix(absl::StripSuffix(groups[1], "]"), "["), ", ");
        std::transform(
            urls.begin(), urls.end(), urls.begin(), [](std::string_view url) {
              return absl::StripPrefix(absl::StripSuffix(url, "\""), "\"");
            });
        current_->set_urls(urls);
      } else {
        LOG(INFO) << groups[0] << " = " << groups[1];
      }
      return absl::OkStatus();
    }
    return absl::FailedPreconditionError(absl::StrFormat(
        "Unexpected line \"%s\" in rule %s", line, current_->alias()));
  }

  absl::Span<const HttpArchive> archives() const { return archives_; }

 private:
  std::optional<HttpArchive> current_;
  absl::InlinedVector<HttpArchive, 10> archives_;
};

}  // namespace

int main(int argc, const char* argv[]) {
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverity::kInfo);
  if (argc < 2) {
    std::cerr << "Bazel output file was not specified\n";
    return 1;
  }
  std::ifstream reader(argv[1]);
  if (!reader.is_open()) {
    std::cerr << absl::Substitute("Can't open $0\n", argv[1]);
  }
  std::string line;
  HttpArchiveParser parser;
  size_t line_no = 0;
  while (std::getline(reader, line)) {
    line_no += 1;
    absl::Status status = parser.Parse(line);
    LOG_IF(FATAL, !status.ok()) << "[line " << line_no << "] " << status;
  }
  for (const HttpArchive& archive : parser.archives()) {
    LOG(INFO) << archive;
  }
}