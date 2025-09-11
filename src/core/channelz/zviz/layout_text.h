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

#ifndef GRPC_SRC_CORE_CHANNELZ_ZVIZ_LAYOUT_TEXT_H
#define GRPC_SRC_CORE_CHANNELZ_ZVIZ_LAYOUT_TEXT_H

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "src/core/channelz/zviz/layout.h"

namespace grpc_zviz::layout {

class TextTable;

class TextElement : public Element {
 public:
  explicit TextElement(int indent = 0);
  ~TextElement() override;

  Element& AppendText(Intent intent, absl::string_view text) override;
  Element& AppendLink(Intent intent, absl::string_view text,
                      absl::string_view href) override;
  Element& AppendGroup(Intent intent) override;
  Element& AppendData(absl::string_view name, absl::string_view type) override;
  Table& AppendTable(TableIntent intent) override;

  std::string Render() const;

 private:
  friend class TextTable;

  void Render(std::string* out) const;

  struct TextContent {
    Intent intent;
    std::string text;
  };
  struct GroupContent {
    std::unique_ptr<TextElement> element;
  };
  struct TableContent {
    std::unique_ptr<TextTable> table;
  };

  using Content = std::variant<TextContent, GroupContent, TableContent>;

  const int indent_;
  std::vector<Content> contents_;
};

class TextTable : public Table {
 public:
  friend class TextElement;
  TextTable(TableIntent intent, int indent);
  ~TextTable() override;

  Element& AppendColumn() override;
  void NewRow() override;

  std::string Render() const;

 private:
  void Render(std::string* out) const;
  struct Cell {
    std::unique_ptr<TextElement> element;
  };

  const TableIntent intent_;
  const int indent_;
  std::vector<std::vector<Cell>> rows_;
};

}  // namespace grpc_zviz::layout

#endif  // GRPC_SRC_CORE_CHANNELZ_ZVIZ_LAYOUT_TEXT_H
