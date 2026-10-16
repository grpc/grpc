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

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_zviz {
namespace layout {
namespace {

TEST(LayoutTextTest, SimpleText) {
  TextElement root;
  EXPECT_EQ(root.Render(), "");
}

TEST(LayoutTextTest, OneLine) {
  TextElement root;
  root.AppendText(Intent::kNote, "Hello");
  EXPECT_EQ(root.Render(), "Hello\n");
}

TEST(LayoutTextTest, OneLineWithIndent) {
  TextElement root(1);
  root.AppendText(Intent::kNote, "Hello");
  EXPECT_EQ(root.Render(), "  Hello\n");
}

TEST(LayoutTextTest, TwoLines) {
  TextElement root;
  root.AppendText(Intent::kNote, "Hello");
  root.AppendText(Intent::kNote, " World");
  EXPECT_EQ(root.Render(), "Hello World\n");
}

TEST(LayoutTextTest, SimpleGroup) {
  TextElement root;
  root.AppendGroup(Intent::kNote).AppendText(Intent::kNote, "Hello");
  EXPECT_EQ(root.Render(), "\n  Hello\n");
}

TEST(LayoutTextTest, SimpleTable) {
  TextElement root;
  auto& table = root.AppendTable(TableIntent::kPropertyList);
  table.AppendColumn().AppendText(Intent::kKey, "Name");
  table.AppendColumn().AppendText(Intent::kValue, "Value");
  table.NewRow();
  table.AppendColumn().AppendText(Intent::kKey, "Another Name");
  table.AppendColumn().AppendText(Intent::kValue, "Another Value");
  table.NewRow();
  EXPECT_EQ(root.Render(),
            "\n"
            "  Name         | Value\n"
            "  Another Name | Another Value\n");
}

TEST(LayoutTextTest, TableWithHeader) {
  TextElement root;
  auto& table = root.AppendTable(TableIntent::kPropertyGrid);
  table.AppendColumn().AppendText(Intent::kKey, "Name");
  table.AppendColumn().AppendText(Intent::kValue, "Value");
  table.NewRow();
  table.AppendColumn().AppendText(Intent::kKey, "Another Name");
  table.AppendColumn().AppendText(Intent::kValue, "Another Value");
  table.NewRow();
  EXPECT_EQ(root.Render(),
            "\n"
            "  Name         | Value\n"
            "  ------------ + -------------\n"
            "  Another Name | Another Value\n");
}

TEST(LayoutTextTest, Banner) {
  TextElement root;
  root.AppendText(Intent::kBanner, "Hello");
  EXPECT_EQ(root.Render(),
            "\n"
            "------------------------------------------------------------------"
            "----\n"
            "📍 Hello\n");
}

}  // namespace
}  // namespace layout
}  // namespace grpc_zviz
