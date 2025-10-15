// Copyright 2025 The gRPC Authors
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

#ifndef GRPC_SRC_CORE_CHANNELZ_ZVIZ_HTML_H
#define GRPC_SRC_CORE_CHANNELZ_ZVIZ_HTML_H

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/util/grpc_check.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"
#include "absl/strings/string_view.h"
namespace grpc_zviz::html {

std::string HtmlEscape(absl::string_view text);

class Item {
 public:
  virtual std::string Render() const = 0;
  virtual ~Item() = default;
};

class Text final : public Item {
 public:
  explicit Text(std::string text) : text_(std::move(text)) {}
  std::string Render() const override { return HtmlEscape(text_); }

 private:
  std::string text_;
};

class Table;

class Container final : public Item {
 public:
  explicit Container(std::string tag) : tag_(std::move(tag)) {}
  Container() = default;
  std::string Render() const override;

  Container& Attribute(std::string name, std::string value) {
    GRPC_CHECK(tag_.has_value())
        << "Attributes can only be set on containers with a tag.";
    attributes_.emplace_back(std::move(name), std::move(value));
    return *this;
  }

  template <typename T, typename... Args>
  T& NewItem(Args&&... args) {
    auto p = std::make_unique<T>(std::forward<Args>(args)...);
    auto* pp = p.get();
    items_.push_back(std::move(p));
    return *pp;
  }

  Container& Text(std::string text) {
    NewItem<html::Text>(std::move(text));
    return *this;
  }
  Container& Link(std::string text, std::string url);
  Container& Div(std::string clazz, absl::FunctionRef<void(Container&)> f);
  Container& NewDiv(std::string clazz);
  Container& TextDiv(std::string clazz, std::string text) {
    return Div(clazz, [&](Container& div) { div.Text(std::move(text)); });
  }
  Container& LinkDiv(std::string clazz, std::string text, std::string url) {
    return Div(clazz, [&](Container& div) { div.Link(std::move(text), url); });
  }
  Table& NewTable(std::string clazz);
  Container& AddStyle(absl::string_view style);

 private:
  std::optional<std::string> tag_;
  std::vector<std::pair<std::string, std::string>> attributes_;
  std::vector<std::unique_ptr<html::Item>> items_;
};

Container Div(std::string clazz, absl::FunctionRef<void(Container&)> f);

class Table final : public Item {
 public:
  explicit Table(std::string clazz) : clazz_(std::move(clazz)) {}

  Container& Cell(int column, int row);
  void set_num_header_columns(int n) { num_header_columns_ = n; }
  void set_num_header_rows(int n) { num_header_rows_ = n; }
  std::string Render() const override;

 private:
  using Address = std::tuple<int, int>;
  std::string clazz_;
  int num_header_columns_ = 0;
  int num_header_rows_ = 0;
  int num_columns_ = 0;
  int num_rows_ = 0;
  absl::flat_hash_map<Address, Container> cells_;
};

}  // namespace grpc_zviz::html

#endif  // GRPC_SRC_CORE_CHANNELZ_ZVIZ_HTML_H
