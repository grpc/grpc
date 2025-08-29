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

#ifndef GRPC_TOOLS_ARTIFACT_GEN_EXTRACT_BAZELMOD_REPOSITORIES_H
#define GRPC_TOOLS_ARTIFACT_GEN_EXTRACT_BAZELMOD_REPOSITORIES_H

#include <string>

#include "absl/container/inlined_vector.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

class BazelModRepository {
 public:
  static absl::StatusOr<std::vector<BazelModRepository>> ParseBazelOutput(
      const std::string& archives_query_path);

  explicit BazelModRepository(absl::string_view alias) : alias_(alias) {}

  std::string alias() const { return alias_; }

  std::string integrity() const { return integrity_; }

  std::string strip_prefix() const { return strip_prefix_; }

  absl::Span<const std::string> urls() const { return urls_; }

  void set_integrity(absl::string_view integrity) { integrity_ = integrity; }

  void set_strip_prefix(absl::string_view strip_prefix) {
    strip_prefix_ = strip_prefix;
  }

  void set_urls(absl::Span<const std::string> urls) {
    std::copy(urls.begin(), urls.end(), std::back_inserter(urls_));
  }

  template <typename Sink>
  friend void AbslStringify(Sink& s, const BazelModRepository& archive) {
    s.Append(archive.Stringify());
  }

 private:
  std::string Stringify() const;

  std::string alias_;
  std::string integrity_;
  std::string strip_prefix_;
  absl::InlinedVector<std::string, 3> urls_;
};

#endif  // GRPC_TOOLS_ARTIFACT_GEN_EXTRACT_BAZELMOD_REPOSITORIES_H
