//
// Copyright 2026 gRPC authors.
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
//

#ifndef GRPC_TEST_CORE_XDS_BM_XDS_MATCHER_COMMON_H
#define GRPC_TEST_CORE_XDS_BM_XDS_MATCHER_COMMON_H

#include <memory>
#include <optional>
#include <string>

#include "benchmark/benchmark.h"
#include "src/core/util/down_cast.h"
#include "src/core/xds/grpc/xds_matcher.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

inline constexpr int kSizeLow = 1;
inline constexpr int kSizeHigh = 512;
inline constexpr int kRangeMultiplier = 4;

// A concrete implementation of MatchContext for testing purposes.
class TestMatchContext : public XdsMatcher::MatchContext {
 public:
  explicit TestMatchContext(std::string path) : path_(std::move(path)) {}
  static UniqueTypeName Type() {
    return GRPC_UNIQUE_TYPE_NAME_HERE("TestMatchContext");
  }
  UniqueTypeName type() const override { return Type(); }
  absl::string_view path() const { return path_; }

 private:
  std::string path_;
};

// A concrete implementation of InputValue for testing.
class TestPathInput : public XdsMatcher::InputValue<absl::string_view> {
 public:
  std::optional<absl::string_view> GetValue(
      const XdsMatcher::MatchContext& context) const override {
    const auto& test_context = DownCast<const TestMatchContext&>(context);
    return test_context.path();
  }
  static UniqueTypeName Type() {
    return GRPC_UNIQUE_TYPE_NAME_HERE("TestPathInput");
  }
  UniqueTypeName type() const override { return Type(); }
  // Not used
  bool Equals(
      const XdsMatcher::InputValue<absl::string_view>& other) const override {
    return true;
  }
  std::string ToString() const override { return "TestPathInput"; }
};

// A concrete implementation of Action for testing.
class TestAction : public XdsMatcher::Action {
 public:
  explicit TestAction(absl::string_view name) : name_(name) {}
  static UniqueTypeName Type() {
    return GRPC_UNIQUE_TYPE_NAME_HERE("test.testAction");
  }
  UniqueTypeName type() const override { return Type(); }
  absl::string_view name() const { return name_; }
  bool Equals(const XdsMatcher::Action& other) const override {
    if (other.type() != type()) return false;
    return name_ == DownCast<const TestAction&>(other).name_;
  }
  std::string ToString() const override {
    return absl::StrCat("TestAction{name=", name(), "}");
  }

 private:
  std::string name_;
};

}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_XDS_BM_XDS_MATCHER_COMMON_H
