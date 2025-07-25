
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

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"

namespace grpc_core {

// Data structure representing a histogram's configuration.
struct Shape {
  int max;
  int buckets;
  int bits;

  bool operator<(const Shape& other) const {
    return std::tie(max, buckets, bits) <
           std::tie(other.max, other.buckets, other.bits);
  }

  bool operator==(const Shape& other) const {
    return max == other.max && buckets == other.buckets && bits == other.bits;
  }

  template <typename H>
  friend H AbslHashValue(H h, const Shape& key) {
    return H::combine(std::move(h), key.max, key.buckets, key.bits);
  }
};

enum class StatType : uint8_t { kCounter = 0, kHistogram = 1 };

// Definitions for statistics to be generated.
struct StatDef {
  StatType type;
  std::string name;
  std::string doc;
  std::string scope;
  std::string linked_global_scope;
  std::optional<int> max;
  std::optional<int> buckets;
  std::optional<int> scope_counter_bits;
  std::optional<int> scope_buckets;
  static void Validate(const StatDef& stat) {
    if (stat.type == StatType::kHistogram) {
      CHECK(stat.max.has_value());
      CHECK(stat.buckets.has_value());
    }
  }
};

class LocalScopedStatsCollectorGeneratorInterface {
 public:
  virtual void GenerateStatsCollector(
      const std::string& class_name,
      absl::flat_hash_map<StatType, std::vector<StatDef>> inst_map,
      std::string& output) = 0;
  virtual ~LocalScopedStatsCollectorGeneratorInterface() = default;
};

std::string SnakeToPascal(const std::string& name);
absl::Status WriteToFile(const std::string& output_file,
                         const std::string& contents);
absl::StatusOr<std::vector<StatDef>> ParseStatsFromFile(
    const std::string& filename);
absl::StatusOr<std::vector<StatDef>> ParseStatsFromYamlString(
    const std::string& contents);

class StatsDataGenerator {
 public:
  explicit StatsDataGenerator(const std::vector<StatDef>& attrs) {
    ProcessAttrs(attrs);
  }

  void GenStatsDataHdr(const std::string& prefix, std::string& output);
  void GenStatsDataSrc(std::string& output);
  void RegisterLocalScopedStatsGenerator(
      std::string scope, std::string linked_global_scope,
      std::unique_ptr<LocalScopedStatsCollectorGeneratorInterface> generator);

 private:
  void ProcessAttrs(const std::vector<StatDef>& attrs);
  int DeclStaticTable(const std::vector<int>& values, const std::string& type);

  absl::flat_hash_map<StatType, std::vector<StatDef>> inst_map_;
  std::vector<std::pair<std::string, std::vector<int>>> static_tables_;
  absl::flat_hash_map<Shape, int> hist_bucket_boundaries_;
  std::set<Shape> shapes_;
  std::vector<std::string> scopes_;
  absl::flat_hash_map<std::string, std::string> linked_global_scopes_;
  absl::flat_hash_map<
      std::string, std::unique_ptr<LocalScopedStatsCollectorGeneratorInterface>>
      local_scoped_stats_generators_;

  std::pair<std::string, int> GenBucketCode(const Shape& shape);
};

}  // namespace grpc_core
