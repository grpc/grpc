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

#ifndef GRPC_SRC_CORE_CHANNELZ_ZVIZ_LAYOUT_HTML_H
#define GRPC_SRC_CORE_CHANNELZ_ZVIZ_LAYOUT_HTML_H

#include <vector>

#include "src/core/channelz/zviz/html.h"
#include "src/core/channelz/zviz/layout.h"

namespace grpc_zviz::layout {

class HtmlTable;

class HtmlElement : public Element {
 public:
  explicit HtmlElement(html::Container& container) : container_(container) {}

  Element& AppendText(Intent intent, absl::string_view text) override;
  Element& AppendLink(Intent intent, absl::string_view text,
                      absl::string_view href) override;
  Element& AppendGroup(Intent intent) override;
  Element& AppendData(absl::string_view name, absl::string_view type) override;
  Table& AppendTable(TableIntent intent) override;

 private:
  html::Container& container_;
  std::vector<std::unique_ptr<HtmlElement>> children_;
  std::vector<std::unique_ptr<HtmlTable>> tables_;
};

class HtmlTable : public Table {
 public:
  HtmlTable(html::Table& table, TableIntent intent);

  Element& AppendColumn() override;
  void NewRow() override;

 private:
  html::Table& table_;
  int column_ = 0;
  int row_ = 0;
  bool in_header_;
  std::vector<std::unique_ptr<Element>> elements_;
};

}  // namespace grpc_zviz::layout

#endif  // GRPC_SRC_CORE_CHANNELZ_ZVIZ_LAYOUT_HTML_H
