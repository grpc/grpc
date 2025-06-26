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

namespace grpc_zviz::layout {

namespace {

std::string IntentToStyle(Intent intent) {
  switch (intent) {
    case Intent::kBanner:
      return "zviz-banner";
    case Intent::kHeading:
      return "zviz-heading";
    case Intent::kEntityRef:
      return "zviz-entity-ref";
    case Intent::kTrace:
      return "zviz-trace";
    case Intent::kTraceDescription:
      return "zviz-trace-description";
    case Intent::kData:
      return "zviz-data";
    case Intent::kTimestamp:
      return "zviz-timestamp";
    case Intent::kNote:
      return "zviz-note";
    case Intent::kKey:
      return "zviz-key";
    case Intent::kValue:
      return "zviz-value";
    case Intent::kDuration:
      return "zviz-duration";
  }
}

}  // namespace

Element& HtmlElement::AppendText(Intent intent, absl::string_view text) {
  container_.TextDiv(IntentToStyle(intent), std::string(text));
  return *this;
}

Element& HtmlElement::AppendLink(Intent intent, absl::string_view text,
                                 absl::string_view href) {
  container_.LinkDiv(IntentToStyle(intent), std::string(text),
                     std::string(href));
  return *this;
}

Element& HtmlElement::AppendGroup(Intent intent) {
  return *children_.emplace_back(
      std::make_unique<HtmlElement>(container_.NewDiv(IntentToStyle(intent))));
}

Element& HtmlElement::AppendData(absl::string_view name, absl::string_view) {
  auto& grp = AppendGroup(Intent::kData);
  grp.AppendText(Intent::kHeading, name);
  return grp;
}

Table& HtmlElement::AppendTable(TableIntent) {
  return *tables_.emplace_back(
      std::make_unique<HtmlTable>(container_.NewTable()));
}

Element& HtmlTable::AppendColumn() {
  return *elements_.emplace_back(
      std::make_unique<HtmlElement>(table_.Cell(column_++, row_)));
}
}  // namespace grpc_zviz::layout
