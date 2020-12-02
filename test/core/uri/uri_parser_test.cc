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

// TODO(hork): rewrite with googletest

#include "src/core/lib/uri/uri_parser.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "test/core/util/test_config.h"

static void test_succeeds(
    absl::string_view uri_text, absl::string_view scheme,
    absl::string_view authority, absl::string_view path,
    const std::map<std::string, std::string>& query_param_map,
    const std::vector<grpc_core::URI::QueryParam> query_param_pairs,
    absl::string_view fragment) {
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(uri_text);
  GPR_ASSERT(uri.ok());
  GPR_ASSERT(scheme == uri->scheme());
  GPR_ASSERT(authority == uri->authority());
  GPR_ASSERT(path == uri->path());
  // query param map
  GPR_ASSERT(uri->query_parameter_map().size() == query_param_map.size());
  for (const auto& expected_kv : query_param_map) {
    const auto it = uri->query_parameter_map().find(expected_kv.first);
    GPR_ASSERT(it != uri->query_parameter_map().end());
    GPR_ASSERT(it->second == expected_kv.second);
  }
  // query param pairs
  GPR_ASSERT(uri->query_parameter_pairs() == query_param_pairs);
  GPR_ASSERT(fragment == uri->fragment());
}

static void test_fails(absl::string_view uri_text) {
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(uri_text);
  GPR_ASSERT(!uri.ok());
}

static void test_query_param_map() {
  test_succeeds("http://localhost:8080/whatzit?mi_casa=su_casa", "http",
                "localhost:8080", "/whatzit", {{"mi_casa", "su_casa"}},
                {{"mi_casa", "su_casa"}}, "");
  test_succeeds("http://localhost:8080/whatzit?1=2#buckle/my/shoe", "http",
                "localhost:8080", "/whatzit", {{"1", "2"}}, {{"1", "2"}},
                "buckle/my/shoe");
  test_succeeds(
      "http://localhost:8080/?too=many=equals&are=present=here#fragged", "http",
      "localhost:8080", "/", {{"are", "present=here"}, {"too", "many=equals"}},
      {{"too", "many=equals"}, {"are", "present=here"}}, "fragged");
  test_succeeds("http://foo/path?a&b=B&c=&#frag", "http", "foo", "/path",
                {{"c", ""}, {"a", ""}, {"b", "B"}},
                {{"a", ""}, {"b", "B"}, {"c", ""}}, "frag");
  test_succeeds("http://auth/path?foo=bar=baz&foobar===", "http", "auth",
                "/path", {{"foo", "bar=baz"}, {"foobar", "=="}},
                {{"foo", "bar=baz"}, {"foobar", "=="}}, "");
}

static void test_repeated_query_param_pairs() {
  absl::StatusOr<grpc_core::URI> uri =
      grpc_core::URI::Parse("http://foo/path?a=2&a=1&a=3");
  GPR_ASSERT(uri.ok());
  GPR_ASSERT(uri->query_parameter_map().size() == 1);
  GPR_ASSERT(uri->query_parameter_map().find("a")->second == "3");
  std::vector<grpc_core::URI::QueryParam> expected(
      {{"a", "2"}, {"a", "1"}, {"a", "3"}});
  GPR_ASSERT(uri->query_parameter_pairs() == expected);
}

static void test_query_param_validity_after_move() {
  grpc_core::URI uri_copy;
  {
    absl::StatusOr<grpc_core::URI> uri =
        grpc_core::URI::Parse("http://foo/path?a=2&b=1&c=3");
    GPR_ASSERT(uri.ok());
    uri_copy = std::move(*uri);
  }
  GPR_ASSERT(uri_copy.query_parameter_map().find("a")->second == "2");
}

static void test_query_param_validity_after_copy() {
  // Since the query parameter map points to objects stored in the param pair
  // vector, this test checks that the param map pointers remain valid after
  // a copy. Ideally {a,m}san will catch this if there's a problem.
  // testing copy operator=:
  grpc_core::URI uri_copy;
  {
    absl::StatusOr<grpc_core::URI> del_uri =
        grpc_core::URI::Parse("http://foo/path?a=2&b=1&c=3");
    GPR_ASSERT(del_uri.ok());
    uri_copy = *del_uri;
  }
  GPR_ASSERT(uri_copy.query_parameter_map().find("a")->second == "2");
  // testing copy constructor:
  grpc_core::URI* del_uri2 = new grpc_core::URI(uri_copy);
  grpc_core::URI uri_copy2(*del_uri2);
  delete del_uri2;
  GPR_ASSERT(uri_copy2.query_parameter_map().find("a")->second == "2");
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  test_succeeds("http://www.google.com", "http", "www.google.com", "", {}, {},
                "");
  test_succeeds("dns:///foo", "dns", "", "/foo", {}, {}, "");
  test_succeeds("http://www.google.com:90", "http", "www.google.com:90", "", {},
                {}, "");
  test_succeeds("a192.4-df:foo.coom", "a192.4-df", "", "foo.coom", {}, {}, "");
  test_succeeds("a+b:foo.coom", "a+b", "", "foo.coom", {}, {}, "");
  test_succeeds("zookeeper://127.0.0.1:2181/foo/bar", "zookeeper",
                "127.0.0.1:2181", "/foo/bar", {}, {}, "");
  test_succeeds("http://www.google.com?yay-i'm-using-queries", "http",
                "www.google.com", "", {{"yay-i'm-using-queries", ""}},
                {{"yay-i'm-using-queries", ""}}, "");
  test_succeeds("dns:foo.com#fragment-all-the-things", "dns", "", "foo.com", {},
                {}, "fragment-all-the-things");
  test_succeeds("http:?legit", "http", "", "", {{"legit", ""}}, {{"legit", ""}},
                "");
  test_succeeds("unix:#this-is-ok-too", "unix", "", "", {}, {},
                "this-is-ok-too");
  test_succeeds("http:?legit#twice", "http", "", "", {{"legit", ""}},
                {{"legit", ""}}, "twice");
  test_succeeds("http://foo?bar#lol?", "http", "foo", "", {{"bar", ""}},
                {{"bar", ""}}, "lol?");
  test_succeeds("http://foo?bar#lol?/", "http", "foo", "", {{"bar", ""}},
                {{"bar", ""}}, "lol?/");
  test_succeeds("ipv6:[2001:db8::1%252]:12345", "ipv6", "",
                "[2001:db8::1%2]:12345", {}, {}, "");
  test_succeeds("https://www.google.com/?a=1%26b%3D2&c=3", "https",
                "www.google.com", "/", {{"c", "3"}, {"a", "1&b=2"}},
                {{"a", "1&b=2"}, {"c", "3"}}, "");
  // Artificial examples to show that embedded nulls are supported.
  test_succeeds(std::string("unix-abstract:\0should-be-ok", 27),
                "unix-abstract", "", std::string("\0should-be-ok", 13), {}, {},
                "");
  test_succeeds(
      "https://foo.com:5555/v1/"
      "token-exchange?subject_token=eyJhbGciO&subject_token_type=urn:ietf:"
      "params:oauth:token-type:id_token",
      "https", "foo.com:5555", "/v1/token-exchange",
      {{"subject_token", "eyJhbGciO"},
       {"subject_token_type", "urn:ietf:params:oauth:token-type:id_token"}},
      {{"subject_token", "eyJhbGciO"},
       {"subject_token_type", "urn:ietf:params:oauth:token-type:id_token"}},
      "");
  test_succeeds("http:?dangling-pct-%0", "http", "", "",
                {{"dangling-pct-%0", ""}}, {{"dangling-pct-%0", ""}}, "");
  test_succeeds("unix-abstract:%00x", "unix-abstract", "",
                std::string("\0x", 2), {}, {}, "");
  test_succeeds("x:y?%xx", "x", "", "y", {{"%xx", ""}}, {{"%xx", ""}}, "");
  test_succeeds("scheme:path//is/ok", "scheme", "", "path//is/ok", {}, {}, "");
  test_succeeds("fake:///", "fake", "", "/", {}, {}, "");
  test_fails("xyz");
  test_fails("http://foo?[bar]");
  test_fails("http://foo?x[bar]");
  test_fails("http://foo?bar#lol#");
  test_fails("");
  test_fails(":no_scheme");
  test_fails("0invalid_scheme:must_start/with?alpha");
  test_query_param_map();
  test_repeated_query_param_pairs();
  test_query_param_validity_after_move();
  test_query_param_validity_after_copy();
  grpc_shutdown();
  return 0;
}
