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

#ifndef GRPC_TEST_CORE_CHANNELZ_ZVIZ_LAYOUT_LOG_H
#define GRPC_TEST_CORE_CHANNELZ_ZVIZ_LAYOUT_LOG_H

#include "src/core/channelz/zviz/layout.h"

namespace grpc_zviz::layout {

class LogElement final : public Element {
 public:
  LogElement(std::string prefix, std::vector<std::string>& lines)
      : prefix_(prefix), lines_(lines) {}

  Element& AppendText(Intent intent, absl::string_view text) override {
    if (intent == Intent::kTimestamp) {
      // We elide the text for timestamps to avoid timezone issues in text
      // comparisons.
      lines_.emplace_back(
          absl::StrCat(prefix_, "APPEND_TEXT ", intent, " [value elided]"));
      return *this;
    }
    lines_.emplace_back(
        absl::StrCat(prefix_, "APPEND_TEXT ", intent, " ", text));
    return *this;
  }

  Element& AppendLink(Intent intent, absl::string_view text,
                      absl::string_view href) override {
    lines_.emplace_back(
        absl::StrCat(prefix_, "APPEND_LINK ", intent, " ", text, " ", href));
    return *this;
  }

  Element& AppendGroup(Intent intent) override {
    int id = next_id_++;
    std::string new_prefix = absl::StrCat(prefix_, "[", id, "] ");
    lines_.emplace_back(absl::StrCat(new_prefix, "GROUP ", intent));
    return *children_.emplace_back(
        std::make_unique<LogElement>(new_prefix, lines_));
  }

  Element& AppendData(absl::string_view name, absl::string_view type) override {
    int id = next_id_++;
    std::string new_prefix = absl::StrCat(prefix_, "[", id, "] ");
    lines_.emplace_back(absl::StrCat(new_prefix, "DATA ", name, " ", type));
    return *children_.emplace_back(
        std::make_unique<LogElement>(new_prefix, lines_));
  }

  Table& AppendTable(TableIntent intent) override {
    int id = next_id_++;
    std::string new_prefix = absl::StrCat(prefix_, "[", id, "] ");
    lines_.emplace_back(absl::StrCat(new_prefix, "APPEND_TABLE ", intent));
    return *tables_.emplace_back(
        std::make_unique<LogTable>(new_prefix, lines_));
  }

 private:
  class LogTable final : public Table {
   public:
    LogTable(std::string prefix, std::vector<std::string>& lines)
        : prefix_(prefix), lines_(lines) {}

    Element& AppendColumn() override {
      int col = column_++;
      std::string new_prefix = absl::StrCat(prefix_, "[", col, ",", row_, "] ");
      lines_.emplace_back(absl::StrCat(new_prefix, "APPEND_COLUMN"));
      return *children_.emplace_back(
          std::make_unique<LogElement>(new_prefix, lines_));
    }
    void NewRow() override {
      lines_.emplace_back(absl::StrCat(prefix_, "NEW_ROW"));
      row_++;
      column_ = 0;
    }

   private:
    int column_ = 0;
    int row_ = 0;
    std::string prefix_;
    std::vector<std::string>& lines_;
    std::vector<std::unique_ptr<LogElement>> children_;
  };

  std::string prefix_;
  int next_id_ = 0;
  std::vector<std::string>& lines_;
  std::vector<std::unique_ptr<LogElement>> children_;
  std::vector<std::unique_ptr<LogTable>> tables_;
};

}  // namespace grpc_zviz::layout

#endif  // GRPC_TEST_CORE_CHANNELZ_ZVIZ_LAYOUT_LOG_H
