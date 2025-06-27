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

TEST(ContainerTest, SimpleDiv) {
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
  c.NewTable();
  EXPECT_EQ(c.Render(), "<body><table/></body>");
}

TEST(ContainerTest, TableWithContent) {
  Container c{"body"};
  auto& table = c.NewTable();
  table.Cell(0, 0).Text("foo");
  table.Cell(1, 0).Text("bar");
  table.Cell(0, 2).Text("baz");
  EXPECT_EQ(c.Render(),
            "<body><table>"
            "<tr><td>foo</td><td>bar</td></tr>"
            "<tr><td/><td/></tr>"
            "<tr><td>baz</td><td/></tr>"
            "</table></body>");
}

}  // namespace
}  // namespace grpc_zviz::html
