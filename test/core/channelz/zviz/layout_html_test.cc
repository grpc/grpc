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

#include "src/core/channelz/zviz/layout_html.h"

#include "gtest/gtest.h"
#include "src/core/channelz/zviz/html.h"

namespace grpc_zviz::layout {
namespace {

TEST(HtmlLayoutTest, SimpleText) {
  html::Container container("body");
  HtmlElement element(container);
  element.AppendText(Intent::kBanner, "Hello World");
  EXPECT_EQ(container.Render(),
            "<body><div class=\"zviz-banner\">Hello World</div></body>");
}

TEST(HtmlLayoutTest, AppendLink) {
  html::Container container("body");
  HtmlElement element(container);
  element.AppendLink(Intent::kEntityRef, "Click Me", "http://example.com");
  EXPECT_EQ(container.Render(),
            "<body><div class=\"zviz-entity-ref\"><a "
            "href=\"http://example.com\">Click Me</a></div></body>");
}

TEST(HtmlLayoutTest, AppendGroup) {
  html::Container container("body");
  HtmlElement element(container);
  Element& group = element.AppendGroup(Intent::kHeading);
  group.AppendText(Intent::kNote, "Grouped Text");
  EXPECT_EQ(container.Render(),
            "<body><div class=\"zviz-heading\">"
            "<div class=\"zviz-note\">Grouped Text</div></div></body>");
}

TEST(HtmlLayoutTest, AppendData) {
  html::Container container("body");
  HtmlElement element(container);
  element.AppendData("data_name", "data_type");
  EXPECT_EQ(container.Render(),
            "<body>"
            "<div class=\"zviz-data\">"
            "<div class=\"zviz-heading\">data_name</div>"
            "</div>"
            "</body>");
}

TEST(HtmlLayoutTest, AppendTable) {
  html::Container container("body");
  HtmlElement element(container);
  Table& table = element.AppendTable(TableIntent::kTrace);
  table.AppendColumn().AppendText(Intent::kKey, "Key");
  table.AppendColumn().AppendText(Intent::kValue, "Value");
  table.NewRow();
  table.AppendColumn().AppendText(Intent::kKey, "Key2");
  table.AppendColumn().AppendText(Intent::kValue, "Value2");
  EXPECT_EQ(container.Render(),
            "<body><table>"
            "<tr>"
            "<td><div class=\"zviz-key\">Key</div></td>"
            "<td><div class=\"zviz-value\">Value</div></td>"
            "</tr>"
            "<tr>"
            "<td><div class=\"zviz-key\">Key2</div></td>"
            "<td><div class=\"zviz-value\">Value2</div></td>"
            "</tr>"
            "</table></body>");
}

}  // namespace
}  // namespace grpc_zviz::layout
