/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/lib/uri/uri_parser.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include <gmock/gmock.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <gtest/gtest.h>

#include "test/core/util/test_config.h"

using ::testing::ContainerEq;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Pair;

static void TestSucceeds(
    absl::string_view uri_text, absl::string_view scheme,
    absl::string_view authority, absl::string_view path,
    const std::map<absl::string_view, absl::string_view>& query_param_map,
    const std::vector<grpc_core::URI::QueryParam>& query_param_pairs,
    absl::string_view fragment) {
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(uri_text);
  ASSERT_TRUE(uri.ok());
  EXPECT_EQ(scheme, uri->scheme());
  EXPECT_EQ(authority, uri->authority());
  EXPECT_EQ(path, uri->path());
  EXPECT_THAT(uri->query_parameter_map(), ContainerEq(query_param_map));
  EXPECT_THAT(uri->query_parameter_pairs(), ContainerEq(query_param_pairs));
  EXPECT_EQ(fragment, uri->fragment());
}

static void TestFails(absl::string_view uri_text) {
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(uri_text);
  ASSERT_FALSE(uri.ok());
}

TEST(URIParserTest, BasicExamplesAreParsedCorrectly) {
  TestSucceeds("http://www.google.com", "http", "www.google.com", "", {}, {},
               "");
  TestSucceeds("dns:///foo", "dns", "", "/foo", {}, {}, "");
  TestSucceeds("http://www.google.com:90", "http", "www.google.com:90", "", {},
               {}, "");
  TestSucceeds("a192.4-df:foo.coom", "a192.4-df", "", "foo.coom", {}, {}, "");
  TestSucceeds("a+b:foo.coom", "a+b", "", "foo.coom", {}, {}, "");
  TestSucceeds("zookeeper://127.0.0.1:2181/foo/bar", "zookeeper",
               "127.0.0.1:2181", "/foo/bar", {}, {}, "");
  TestSucceeds("dns:foo.com#fragment-all-the-things", "dns", "", "foo.com", {},
               {}, "fragment-all-the-things");
  TestSucceeds("http://localhost:8080/whatzit?mi_casa=su_casa", "http",
               "localhost:8080", "/whatzit", {{"mi_casa", "su_casa"}},
               {{"mi_casa", "su_casa"}}, "");
  TestSucceeds("http://localhost:8080/whatzit?1=2#buckle/my/shoe", "http",
               "localhost:8080", "/whatzit", {{"1", "2"}}, {{"1", "2"}},
               "buckle/my/shoe");
}

TEST(URIParserTest, UncommonValidExamplesAreParsedCorrectly) {
  TestSucceeds("scheme:path//is/ok", "scheme", "", "path//is/ok", {}, {}, "");
  TestSucceeds("http:?legit", "http", "", "", {{"legit", ""}}, {{"legit", ""}},
               "");
  TestSucceeds("unix:#this-is-ok-too", "unix", "", "", {}, {},
               "this-is-ok-too");
  TestSucceeds("http:?legit#twice", "http", "", "", {{"legit", ""}},
               {{"legit", ""}}, "twice");
  TestSucceeds("fake:///", "fake", "", "/", {}, {}, "");
}

TEST(URIParserTest, VariousKeyValueAndNonKVQueryParamsAreParsedCorrectly) {
  TestSucceeds("http://foo/path?a&b=B&c=&#frag", "http", "foo", "/path",
               {{"c", ""}, {"a", ""}, {"b", "B"}},
               {{"a", ""}, {"b", "B"}, {"c", ""}}, "frag");
}

TEST(URIParserTest, ParserTreatsFirstEqualSignAsKVDelimiterInQueryString) {
  TestSucceeds(
      "http://localhost:8080/?too=many=equals&are=present=here#fragged", "http",
      "localhost:8080", "/", {{"are", "present=here"}, {"too", "many=equals"}},
      {{"too", "many=equals"}, {"are", "present=here"}}, "fragged");
  TestSucceeds("http://auth/path?foo=bar=baz&foobar===", "http", "auth",
               "/path", {{"foo", "bar=baz"}, {"foobar", "=="}},
               {{"foo", "bar=baz"}, {"foobar", "=="}}, "");
}

TEST(URIParserTest,
     RepeatedQueryParamsAreSupportedInOrderedPairsButDeduplicatedInTheMap) {
  absl::StatusOr<grpc_core::URI> uri =
      grpc_core::URI::Parse("http://foo/path?a=2&a=1&a=3");
  ASSERT_TRUE(uri.ok());
  // The map stores the last found value.
  ASSERT_THAT(uri->query_parameter_map(), ElementsAre(Pair("a", "3")));
  // Order matters for query parameter pairs
  ASSERT_THAT(uri->query_parameter_pairs(),
              ElementsAre(grpc_core::URI::QueryParam{"a", "2"},
                          grpc_core::URI::QueryParam{"a", "1"},
                          grpc_core::URI::QueryParam{"a", "3"}));
}

TEST(URIParserTest, QueryParamMapRemainsValiditAfterMovingTheURI) {
  grpc_core::URI uri_copy;
  {
    absl::StatusOr<grpc_core::URI> uri =
        grpc_core::URI::Parse("http://foo/path?a=2&b=1&c=3");
    ASSERT_TRUE(uri.ok());
    uri_copy = std::move(*uri);
  }
  // ASSERT_EQ(uri_copy.query_parameter_map().find("a")->second, "2");
  ASSERT_THAT(uri_copy.query_parameter_map(), Contains(Pair("a", "2")));
}

TEST(URIParserTest, QueryParamMapRemainsValidAfterCopyingTheURI) {
  // Since the query parameter map points to objects stored in the param pair
  // vector, this test checks that the param map pointers remain valid after
  // a copy. Ideally {a,m}san will catch this if there's a problem.
  // testing copy operator=:
  grpc_core::URI uri_copy;
  {
    absl::StatusOr<grpc_core::URI> del_uri =
        grpc_core::URI::Parse("http://foo/path?a=2&b=1&c=3");
    ASSERT_TRUE(del_uri.ok());
    uri_copy = *del_uri;
  }
  ASSERT_THAT(uri_copy.query_parameter_map(), Contains(Pair("a", "2")));
  grpc_core::URI* del_uri2 = new grpc_core::URI(uri_copy);
  grpc_core::URI uri_copy2(*del_uri2);
  delete del_uri2;
  ASSERT_THAT(uri_copy2.query_parameter_map(), Contains(Pair("a", "2")));
}

TEST(URIParserTest, AWSExternalAccountRegressionTest) {
  TestSucceeds(
      "https://foo.com:5555/v1/"
      "token-exchange?subject_token=eyJhbGciO&subject_token_type=urn:ietf:"
      "params:oauth:token-type:id_token",
      "https", "foo.com:5555", "/v1/token-exchange",
      {{"subject_token", "eyJhbGciO"},
       {"subject_token_type", "urn:ietf:params:oauth:token-type:id_token"}},
      {{"subject_token", "eyJhbGciO"},
       {"subject_token_type", "urn:ietf:params:oauth:token-type:id_token"}},
      "");
}

TEST(URIParserTest, NonKeyValueQueryStringsWork) {
  TestSucceeds("http://www.google.com?yay-i'm-using-queries", "http",
               "www.google.com", "", {{"yay-i'm-using-queries", ""}},
               {{"yay-i'm-using-queries", ""}}, "");
}

TEST(URIParserTest, IPV6StringsAreParsedCorrectly) {
  TestSucceeds("ipv6:[2001:db8::1%252]:12345", "ipv6", "",
               "[2001:db8::1%2]:12345", {}, {}, "");
  TestSucceeds("ipv6:[fe80::90%eth1.sky1]:6010", "ipv6", "",
               "[fe80::90%eth1.sky1]:6010", {}, {}, "");
}

TEST(URIParserTest, PreviouslyReservedCharactersInUnrelatedURIPartsAreIgnored) {
  // The '?' and '/' characters are not reserved delimiter characters in the
  // fragment. See http://go/rfc/3986#section-3.5
  TestSucceeds("http://foo?bar#lol?", "http", "foo", "", {{"bar", ""}},
               {{"bar", ""}}, "lol?");
  TestSucceeds("http://foo?bar#lol?/", "http", "foo", "", {{"bar", ""}},
               {{"bar", ""}}, "lol?/");
}

TEST(URIParserTest, EncodedCharactersInQueryStringAreParsedCorrectly) {
  TestSucceeds("https://www.google.com/?a=1%26b%3D2&c=3", "https",
               "www.google.com", "/", {{"c", "3"}, {"a", "1&b=2"}},
               {{"a", "1&b=2"}, {"c", "3"}}, "");
}

TEST(URIParserTest, InvalidPercentEncodingsArePassedThrough) {
  TestSucceeds("x:y?%xx", "x", "", "y", {{"%xx", ""}}, {{"%xx", ""}}, "");
  TestSucceeds("http:?dangling-pct-%0", "http", "", "",
               {{"dangling-pct-%0", ""}}, {{"dangling-pct-%0", ""}}, "");
}

TEST(URIParserTest, NullCharactersInURIStringAreSupported) {
  // Artificial examples to show that embedded nulls are supported.
  TestSucceeds(std::string("unix-abstract:\0should-be-ok", 27), "unix-abstract",
               "", std::string("\0should-be-ok", 13), {}, {}, "");
}

TEST(URIParserTest, EncodedNullsInURIStringAreSupported) {
  TestSucceeds("unix-abstract:%00x", "unix-abstract", "", std::string("\0x", 2),
               {}, {}, "");
}

TEST(URIParserTest, InvalidURIsResultInFailureStatuses) {
  TestFails("xyz");
  TestFails("http://foo?[bar]");
  TestFails("http://foo?x[bar]");
  TestFails("http://foo?bar#lol#");
  TestFails("");
  TestFails(":no_scheme");
  TestFails("0invalid_scheme:must_start/with?alpha");
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
