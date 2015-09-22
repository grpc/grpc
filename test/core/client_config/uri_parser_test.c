/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/core/client_config/uri_parser.h"

#include <string.h>

#include <grpc/support/log.h>

#include "test/core/util/test_config.h"

static void test_succeeds(const char *uri_text, const char *scheme,
                          const char *authority, const char *path,
                          const char *query, const char *fragment) {
  grpc_uri *uri = grpc_uri_parse(uri_text, 0);
  GPR_ASSERT(uri);
  GPR_ASSERT(0 == strcmp(scheme, uri->scheme));
  GPR_ASSERT(0 == strcmp(authority, uri->authority));
  GPR_ASSERT(0 == strcmp(path, uri->path));
  GPR_ASSERT(0 == strcmp(query, uri->query));
  GPR_ASSERT(0 == strcmp(fragment, uri->fragment));
  grpc_uri_destroy(uri);
}

static void test_fails(const char *uri_text) {
  GPR_ASSERT(NULL == grpc_uri_parse(uri_text, 0));
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_succeeds("http://www.google.com", "http", "www.google.com", "", "", "");
  test_succeeds("dns:///foo", "dns", "", "/foo", "", "");
  test_succeeds("http://www.google.com:90", "http", "www.google.com:90", "", "",
                "");
  test_succeeds("a192.4-df:foo.coom", "a192.4-df", "", "foo.coom", "", "");
  test_succeeds("a+b:foo.coom", "a+b", "", "foo.coom", "", "");
  test_succeeds("zookeeper://127.0.0.1:2181/foo/bar", "zookeeper",
                "127.0.0.1:2181", "/foo/bar", "", "");
  test_succeeds("http://www.google.com?yay-i'm-using-queries", "http",
                "www.google.com", "", "yay-i'm-using-queries", "");
  test_succeeds("dns:foo.com#fragment-all-the-things", "dns", "", "foo.com", "",
                "fragment-all-the-things");
  test_succeeds("http:?legit", "http", "", "", "legit", "");
  test_succeeds("unix:#this-is-ok-too", "unix", "", "", "", "this-is-ok-too");
  test_succeeds("http:?legit#twice", "http", "", "", "legit", "twice");
  test_succeeds("http://foo?bar#lol?", "http", "foo", "", "bar", "lol?");
  test_succeeds("http://foo?bar#lol?/", "http", "foo", "", "bar", "lol?/");

  test_fails("xyz");
  test_fails("http:?dangling-pct-%0");
  test_fails("http://foo?[bar]");
  test_fails("http://foo?x[bar]");
  test_fails("http://foo?bar#lol#");

  return 0;
}
