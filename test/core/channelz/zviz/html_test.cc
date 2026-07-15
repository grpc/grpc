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

#include "src/core/channelz/zviz/html.h"

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

namespace grpc_zviz::html {
namespace {

TEST(HtmlEscapeTest, HtmlEscape) {
  EXPECT_EQ(HtmlEscape("foo"), "foo");
  EXPECT_EQ(HtmlEscape("foo&bar"), "foo&amp;bar");
  EXPECT_EQ(HtmlEscape("'<>'"), "&apos;&lt;&gt;&apos;");
  EXPECT_EQ(HtmlEscape("foo\nbar"), "foo\nbar");
  EXPECT_EQ(HtmlEscape("\"foo\""), "&quot;foo&quot;");
}

void HtmlEscapeNeverEmpty(std::string text) { EXPECT_NE(HtmlEscape(text), ""); }
FUZZ_TEST(HtmlEscapeTest, HtmlEscapeNeverEmpty)
    .WithDomains(fuzztest::NonEmpty(fuzztest::Arbitrary<std::string>()));

TEST(TextTest, Text) {
  Text t("foo");
  EXPECT_EQ(t.Render(), "foo");
}

TEST(TextTest, TextEscapes) {
  Text t("foo&bar");
  EXPECT_EQ(t.Render(), "foo&amp;bar");
}

TEST(ContainerTest, EmptyDiv) {
  Container c{"div"};
  EXPECT_EQ(c.Render(), "<div/>");
}

TEST(ContainerTest, SimpleDivWithContent) {
  Container c{"div"};
  c.Text("foo");
  EXPECT_EQ(c.Render(), "<div>foo</div>");
}

TEST(ContainerTest, DivWithStyle) {
  Container c{"div"};
  c.Attribute("style", "slartibartfast");
  c.Text("bar");
  EXPECT_EQ(c.Render(), "<div style=\"slartibartfast\">bar</div>");
}

TEST(ContainerTest, Link) {
  Container c{"p"};
  c.Link("click here", "http://example.com");
  EXPECT_EQ(c.Render(), "<p><a href=\"http://example.com\">click here</a></p>");
}

TEST(ContainerTest, LinkEscapesUrlAndText) {
  Container c{"p"};
  c.Link("click & me", "http://example.com?q=\"value\"");
  EXPECT_EQ(c.Render(),
            "<p><a href=\"http://example.com?q=&quot;value&quot;\">click "
            "&amp; me</a></p>");
}

TEST(ContainerTest, DivMemberFunction) {
  Container c{"body"};
  c.Div("my-style", [](Container& div) { div.Text("hello"); });
  EXPECT_EQ(c.Render(), "<body><div class=\"my-style\">hello</div></body>");
}

TEST(ContainerTest, NewDivSimple) {
  Container c{"body"};
  c.NewDiv("child-style");
  EXPECT_EQ(c.Render(), "<body><div class=\"child-style\"/></body>");
}

TEST(ContainerTest, NewDivWithContent) {
  Container c{"body"};
  c.NewDiv("child-style").Text("content");
  EXPECT_EQ(c.Render(),
            "<body><div class=\"child-style\">content</div></body>");
}

TEST(ContainerTest, TextDiv) {
  Container c{"body"};
  c.TextDiv("text-div-style", "some text");
  EXPECT_EQ(c.Render(),
            "<body><div class=\"text-div-style\">some text</div></body>");
}

TEST(ContainerTest, TextDivEscapesText) {
  Container c{"body"};
  c.TextDiv("text-div-style", "some & text");
  EXPECT_EQ(c.Render(),
            "<body><div class=\"text-div-style\">some &amp; text</div></body>");
}

TEST(ContainerTest, LinkDiv) {
  Container c{"body"};
  c.LinkDiv("link-div-style", "my link", "http://foo.bar");
  EXPECT_EQ(c.Render(),
            "<body><div class=\"link-div-style\"><a href=\"http://foo.bar\">my "
            "link</a></div></body>");
}

TEST(ContainerTest, LinkDivEscapes) {
  Container c{"body"};
  c.LinkDiv("link-div-style", "my & link", "http://foo.bar?q=\"baz\"");
  EXPECT_EQ(c.Render(),
            "<body><div class=\"link-div-style\"><a "
            "href=\"http://foo.bar?q=&quot;baz&quot;\">my &amp; "
            "link</a></div></body>");
}

TEST(ContainerTest, EmptyTable) {
  Container c{"body"};
  c.NewTable("");
  // This is a golden file test.
  // The extra tbody is expected.
  EXPECT_EQ(c.Render(),
            "<body><table class=\"\"><tbody></tbody></table></body>");
}

TEST(ContainerTest, TableWithContent) {
  Container c{"body"};
  auto& table = c.NewTable("");
  table.Cell(0, 0).Text("foo");
  table.Cell(1, 0).Text("bar");
  table.Cell(0, 2).Text("baz");
  // This is a golden file test.
  // The extra divs in the cells are expected.
  EXPECT_EQ(c.Render(),
            "<body><table class=\"\">"
            "<tbody>"
            "<tr><td><div>foo</div></td><td><div>bar</div></td></tr>"
            "<tr><td/><td/></tr>"
            "<tr><td><div>baz</div></td><td/></tr>"
            "</tbody>"
            "</table></body>");
}

TEST(ContainerTest, TableWithMissingCell) {
  Container c{"body"};
  auto& table = c.NewTable("my-table");
  table.Cell(0, 0).Text("A");
  table.Cell(1, 0).Text("B");
  table.Cell(0, 1).Text("C");
  // This is a golden file test.
  // The extra divs in the cells are expected.
  EXPECT_EQ(c.Render(),
            "<body><table class=\"my-table\">"
            "<tbody>"
            "<tr><td><div>A</div></td><td><div>B</div></td></tr>"
            "<tr><td><div>C</div></td><td/></tr>"
            "</tbody>"
            "</table></body>");
}

TEST(ContainerTest, NestedTable) {
  Container c{"body"};
  auto& table = c.NewTable("my-table");
  table.Cell(0, 0).Text("A");
  table.Cell(1, 0).Text("B");
  auto& nested_table = table.Cell(0, 1).NewTable("nested-table");
  nested_table.Cell(0, 0).Text("C");
  nested_table.Cell(1, 0).Text("D");
  // This is a golden file test.
  // The extra divs in the cells are expected.
  EXPECT_EQ(c.Render(),
            "<body><table class=\"my-table\">"
            "<tbody>"
            "<tr><td><div>A</div></td><td><div>B</div></td></tr>"
            "<tr><td><div><table class=\"nested-table\">"
            "<tbody>"
            "<tr><td><div>C</div></td><td><div>D</div></td></tr>"
            "</tbody>"
            "</table></div></td><td/></tr>"
            "</tbody>"
            "</table></body>");
}

TEST(ContainerTest, PropertyGrid) {
  Container c{"body"};
  auto& table = c.NewTable("property-grid");
  table.set_num_header_rows(1);
  table.set_num_header_columns(1);
  table.Cell(1, 0).Text("local");
  table.Cell(2, 0).Text("sent");
  table.Cell(3, 0).Text("peer");
  table.Cell(4, 0).Text("acked");
  table.Cell(0, 1).Text("ENABLE_PUSH");
  table.Cell(1, 1).Text("true");
  table.Cell(2, 1).Text("true");
  table.Cell(3, 1).Text("false");
  table.Cell(4, 1).Text("true");
  // This is a golden file test.
  // The extra divs in the cells are expected.
  EXPECT_EQ(c.Render(),
            "<body><table class=\"property-grid\">"
            "<thead>"
            "<tr><th/><th><div>local</div></th><th><div>sent</div></"
            "th><th><div>peer</div></th><th><div>acked</div></th></tr>"
            "</thead>"
            "<tbody>"
            "<tr><th><div>ENABLE_PUSH</div></th><td><div>true</div></"
            "td><td><div>true</div></td><td><div>false</div></"
            "td><td><div>true</div></td></tr>"
            "</tbody>"
            "</table></body>");
}

TEST(ContainerTest, NestedPropertyList) {
  Container c{"body"};
  auto& table = c.NewTable("property-list");
  table.Cell(0, 0).Text("ping_callbacks");
  auto& nested_table = table.Cell(1, 0).NewTable("property-list");
  nested_table.Cell(0, 0).Text("inflight");
  nested_table.Cell(1, 0).Text("...");
  table.Cell(0, 1).Text("ping_on_rst_stream_percent");
  table.Cell(1, 1).Text("1");
  // This is a golden file test.
  // The extra divs in the cells are expected.
  EXPECT_EQ(c.Render(),
            "<body><table class=\"property-list\">"
            "<tbody>"
            "<tr><td><div>ping_callbacks</div></td><td><div>"
            "<table class=\"property-list\">"
            "<tbody>"
            "<tr><td><div>inflight</div></td><td><div>...</div></td></tr>"
            "</tbody>"
            "</table>"
            "</div></td></tr>"
            "<tr><td><div>ping_on_rst_stream_percent</div></td><td><div>1</"
            "div></td></tr>"
            "</tbody>"
            "</table></body>");
}

}  // namespace
}  // namespace grpc_zviz::html
