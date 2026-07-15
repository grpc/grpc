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

#include "src/core/channelz/zviz/layout_html.h"

#include "absl/strings/str_cat.h"

namespace grpc_zviz::layout {

namespace {

template <typename T>
std::string Stringify(T V) {
  return absl::StrCat("zviz-", V);
}

}  // namespace

Element& HtmlElement::AppendText(Intent intent, absl::string_view text) {
  container_.TextDiv(Stringify(intent), std::string(text));
  return *this;
}

Element& HtmlElement::AppendLink(Intent intent, absl::string_view text,
                                 absl::string_view href) {
  container_.LinkDiv(Stringify(intent), std::string(text), std::string(href));
  return *this;
}

Element& HtmlElement::AppendGroup(Intent intent) {
  return *children_.emplace_back(
      std::make_unique<HtmlElement>(container_.NewDiv(Stringify(intent))));
}

Element& HtmlElement::AppendData(absl::string_view name, absl::string_view) {
  auto& grp = AppendGroup(Intent::kData);
  grp.AppendText(Intent::kHeading, name);
  return grp;
}

Table& HtmlElement::AppendTable(TableIntent intent) {
  return *tables_.emplace_back(std::make_unique<HtmlTable>(
      container_.NewTable(Stringify(intent)), intent));
}

HtmlTable::HtmlTable(html::Table& table, TableIntent intent) : table_(table) {
  switch (intent) {
    case TableIntent::kPropertyList:
      in_header_ = false;
      break;
    case TableIntent::kPropertyGrid:
      table_.set_num_header_columns(1);
      in_header_ = true;
      break;
    case TableIntent::kPropertyTable:
    case TableIntent::kTrace:
      in_header_ = true;
      break;
  }
  if (in_header_) {
    table_.set_num_header_rows(1);
  }
}

Element& HtmlTable::AppendColumn() {
  if (in_header_) {
    table_.set_num_header_rows(1);
  }
  return *elements_.emplace_back(
      std::make_unique<HtmlElement>(table_.Cell(column_++, row_)));
}

void HtmlTable::NewRow() {
  if (in_header_) {
    in_header_ = false;
  }
  row_++;
  column_ = 0;
}

}  // namespace grpc_zviz::layout
