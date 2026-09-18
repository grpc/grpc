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

#include "src/core/channelz/zviz/layout_text.h"

#include <algorithm>
#include <sstream>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace grpc_zviz::layout {

namespace {
std::string Indent(int indent) { return std::string(indent * 2, ' '); }
}  // namespace

//
// TextElement
//

TextElement::TextElement(int indent) : indent_(indent) {}

TextElement::~TextElement() = default;

std::string TextElement::Render() const {
  std::string out;
  Render(&out);
  return out;
}

void TextElement::Render(std::string* out) const {
  std::string buffer;
  for (const auto& content : contents_) {
    std::visit(
        [&](auto& c) {
          using T = std::decay_t<decltype(c)>;
          if constexpr (std::is_same_v<T, TextContent>) {
            if (c.intent == Intent::kBanner) {
              if (!buffer.empty()) {
                absl::StrAppend(out, Indent(indent_), buffer, "\n");
                buffer.clear();
              }
              absl::StrAppend(out, "\n", std::string(70, '-'), "\n");
              absl::StrAppend(out, Indent(indent_), "📍 ", c.text, "\n");
            } else {
              buffer.append(c.text);
            }
          } else {
            if (!buffer.empty()) {
              absl::StrAppend(out, Indent(indent_), buffer, "\n");
              buffer.clear();
            }
            if constexpr (std::is_same_v<T, GroupContent>) {
              out->append("\n");
              c.element->Render(out);
            } else {  // Table
              out->append("\n");
              c.table->Render(out);
            }
          }
        },
        content);
  }
  if (!buffer.empty()) {
    absl::StrAppend(out, Indent(indent_), buffer, "\n");
  }
}

Element& TextElement::AppendText(Intent intent, absl::string_view text) {
  if (!contents_.empty()) {
    if (auto* t = std::get_if<TextContent>(&contents_.back())) {
      if (t->intent == intent) {
        absl::StrAppend(&t->text, text);
        return *this;
      }
    }
  }
  contents_.emplace_back(TextContent{intent, std::string(text)});
  return *this;
}

Element& TextElement::AppendLink(Intent, absl::string_view text,
                                 absl::string_view href) {
  return AppendText(Intent::kNote, absl::StrCat(text, " (", href, ")"));
}

Element& TextElement::AppendGroup(Intent) {
  auto element = std::make_unique<TextElement>(indent_ + 1);
  auto* ptr = element.get();
  contents_.emplace_back(GroupContent{std::move(element)});
  return *ptr;
}

Element& TextElement::AppendData(absl::string_view name, absl::string_view) {
  auto& group = AppendGroup(Intent::kData);
  group.AppendText(Intent::kHeading, absl::StrCat(name, ":"));
  return group;
}

Table& TextElement::AppendTable(TableIntent intent) {
  auto table = std::make_unique<TextTable>(intent, indent_ + 1);
  auto* ptr = table.get();
  contents_.emplace_back(TableContent{std::move(table)});
  return *ptr;
}

//
// TextTable
//

TextTable::TextTable(TableIntent intent, int indent)
    : intent_(intent), indent_(indent) {
  rows_.emplace_back();
}

TextTable::~TextTable() = default;

Element& TextTable::AppendColumn() {
  auto element = std::make_unique<TextElement>(0);
  auto* ptr = element.get();
  rows_.back().emplace_back(Cell{std::move(element)});
  return *ptr;
}

void TextTable::NewRow() { rows_.emplace_back(); }

std::string TextTable::Render() const {
  std::string out;
  Render(&out);
  return out;
}

void TextTable::Render(std::string* out) const {
  std::vector<std::vector<std::string>> rendered_cells;
  for (const auto& row : rows_) {
    if (row.empty()) continue;
    rendered_cells.emplace_back();
    for (const auto& cell : row) {
      std::string cell_out;
      cell.element->Render(&cell_out);
      while (!cell_out.empty() && cell_out.back() == '\n') {
        cell_out.pop_back();
      }
      if (!cell_out.empty() && cell_out.front() == '\n') {
        cell_out.erase(0, 1);
      }
      rendered_cells.back().push_back(std::move(cell_out));
    }
  }

  if (rendered_cells.empty()) return;

  std::vector<size_t> widths;
  for (const auto& row : rendered_cells) {
    if (widths.size() < row.size()) {
      widths.resize(row.size(), 0);
    }
    for (size_t i = 0; i < row.size(); ++i) {
      size_t max_line_len = 0;
      for (absl::string_view line : absl::StrSplit(row[i], '\n')) {
        max_line_len = std::max(max_line_len, line.length());
      }
      widths[i] = std::max(widths[i], max_line_len);
    }
  }

  std::string indent_str = Indent(indent_);

  auto format_row = [&](const std::vector<std::string>& row, char pad) {
    std::vector<std::vector<absl::string_view>> split_cells;
    split_cells.reserve(row.size());
    size_t max_lines = 0;
    for (const auto& cell_text : row) {
      std::vector<absl::string_view> lines = absl::StrSplit(cell_text, '\n');
      max_lines = std::max(max_lines, lines.size());
      split_cells.push_back(std::move(lines));
    }
    if (max_lines == 0) return;
    for (size_t line_idx = 0; line_idx < max_lines; ++line_idx) {
      std::string line;
      for (size_t i = 0; i < widths.size(); ++i) {
        if (i > 0) absl::StrAppend(&line, " | ");
        absl::string_view text;
        if (i < split_cells.size() && line_idx < split_cells[i].size()) {
          text = split_cells[i][line_idx];
        }
        absl::StrAppend(&line, text);
        if (i < widths.size() - 1) {
          line.append(widths[i] - text.length(), pad);
        }
      }
      absl::StrAppend(out, indent_str, line, "\n");
    }
  };

  auto format_separator = [&]() {
    std::string line;
    for (size_t i = 0; i < widths.size(); ++i) {
      if (i > 0) absl::StrAppend(&line, " + ");
      absl::StrAppend(&line, std::string(widths[i], '-'));
    }
    absl::StrAppend(out, indent_str, line, "\n");
  };

  bool has_header = false;
  switch (intent_) {
    case TableIntent::kPropertyList:
      for (const auto& row : rendered_cells) {
        if (row.empty()) continue;
        format_row(row, ' ');
      }
      return;
    case TableIntent::kPropertyGrid:
    case TableIntent::kPropertyTable:
    case TableIntent::kTrace:
      has_header = true;
      break;
  }

  if (has_header && !rendered_cells.empty()) {
    format_row(rendered_cells[0], ' ');
    format_separator();
    for (size_t i = 1; i < rendered_cells.size(); ++i) {
      if (rendered_cells[i].empty()) continue;
      format_row(rendered_cells[i], ' ');
    }
  } else {
    for (const auto& row : rendered_cells) {
      if (row.empty()) continue;
      format_row(row, ' ');
    }
  }
}

}  // namespace grpc_zviz::layout
