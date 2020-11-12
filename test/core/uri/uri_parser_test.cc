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

#include <string.h>

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/util/test_config.h"

static void test_succeeds(
    absl::string_view uri_text, absl::string_view scheme,
    absl::string_view authority, absl::string_view path,
    const absl::flat_hash_map<std::string, std::string>& query_params,
    absl::string_view fragment) {
  grpc_core::ExecCtx exec_ctx;
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(uri_text);
  GPR_ASSERT(uri.ok());
  GPR_ASSERT(scheme == uri->scheme());
  GPR_ASSERT(authority == uri->authority());
  GPR_ASSERT(path == uri->path());
  for (const auto& expected_kv : query_params) {
    const auto it = uri->query_parameters().find(expected_kv.first);
    GPR_ASSERT(it != uri->query_parameters().end());
    if (it->second != expected_kv.second) {
      gpr_log(GPR_ERROR, "%s!=%s", it->second.c_str(),
              expected_kv.second.c_str());
    }
    GPR_ASSERT(it->second == expected_kv.second);
  }
  GPR_ASSERT(query_params.size() == uri->query_parameters().size());
  GPR_ASSERT(fragment == uri->fragment());
}

static void test_fails(absl::string_view uri_text) {
  grpc_core::ExecCtx exec_ctx;

  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(uri_text);
  GPR_ASSERT(!uri.ok());
}

static void test_query_parts() {
  test_succeeds("http://localhost:8080/whatzit?mi_casa=su_casa", "http",
                "localhost:8080", "/whatzit", {{"mi_casa", "su_casa"}}, "");
  test_succeeds("http://localhost:8080/whatzit?1=2#buckle/my/shoe", "http",
                "localhost:8080", "/whatzit", {{"1", "2"}}, "buckle/my/shoe");
  test_succeeds(
      "http://localhost:8080/?too=many=equals&are=present=here#fragged", "http",
      "localhost:8080", "/", {{"too", "many=equals"}, {"are", "present=here"}},
      "fragged");
  test_succeeds("http://foo/path?a&b=B&c=&#frag", "http", "foo", "/path",
                {{"a", ""}, {"b", "B"}, {"c", ""}}, "frag");
  test_succeeds("http://auth/path?foo=bar=baz&foobar===", "http", "auth",
                "/path", {{"foo", "bar=baz"}, {"foobar", "=="}}, "");
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  test_succeeds("http://www.google.com", "http", "www.google.com", "", {}, "");
  test_succeeds("dns:///foo", "dns", "", "/foo", {}, "");
  test_succeeds("http://www.google.com:90", "http", "www.google.com:90", "", {},
                "");
  test_succeeds("a192.4-df:foo.coom", "a192.4-df", "", "foo.coom", {}, "");
  test_succeeds("a+b:foo.coom", "a+b", "", "foo.coom", {}, "");
  test_succeeds("zookeeper://127.0.0.1:2181/foo/bar", "zookeeper",
                "127.0.0.1:2181", "/foo/bar", {}, "");
  test_succeeds("http://www.google.com?yay-i'm-using-queries", "http",
                "www.google.com", "", {{"yay-i'm-using-queries", ""}}, "");
  test_succeeds("dns:foo.com#fragment-all-the-things", "dns", "", "foo.com", {},
                "fragment-all-the-things");
  test_succeeds("http:?legit", "http", "", "", {{"legit", ""}}, "");
  test_succeeds("unix:#this-is-ok-too", "unix", "", "", {}, "this-is-ok-too");
  test_succeeds("http:?legit#twice", "http", "", "", {{"legit", ""}}, "twice");
  test_succeeds("http://foo?bar#lol?", "http", "foo", "", {{"bar", ""}},
                "lol?");
  test_succeeds("http://foo?bar#lol?/", "http", "foo", "", {{"bar", ""}},
                "lol?/");
  test_succeeds("ipv6:[2001:db8::1%252]:12345", "ipv6", "",
                "[2001:db8::1%2]:12345", {}, "");
  test_succeeds("https://www.google.com/?a=1%26b%3D2&c=3", "https",
                "www.google.com", "/", {{"a", "1&b=2"}, {"c", "3"}}, "");
  // Artificial examples to show that embedded nulls are supported.
  test_succeeds(std::string("unix-abstract:\0should-be-ok", 27),
                "unix-abstract", "", std::string("\0should-be-ok", 13), {}, "");
  test_succeeds(
      "https://foo.com:5555/v1/"
      "token-exchange?subject_token=eyJhbGciO&subject_token_type=urn:ietf:"
      "params:oauth:token-type:id_token",
      "https", "foo.com:5555", "/v1/token-exchange",
      {{"subject_token", "eyJhbGciO"},
       {"subject_token_type", "urn:ietf:params:oauth:token-type:id_token"}},
      "");
  test_succeeds("http:?dangling-pct-%0", "http", "", "",
                {{"dangling-pct-%0", ""}}, "");
  test_succeeds("x:y?%xx", "x", "", "y", {{"%xx", ""}}, "");

  test_fails("xyz");
  test_fails("http://foo?[bar]");
  test_fails("http://foo?x[bar]");
  test_fails("http://foo?bar#lol#");
  test_fails("");

  test_query_parts();
  grpc_shutdown();
  return 0;
}
