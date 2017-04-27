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

#include "src/core/ext/filters/client_channel/uri_parser.h"

#include <string.h>

#include <grpc/support/log.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/util/test_config.h"

static void test_succeeds(const char *uri_text, const char *scheme,
                          const char *authority, const char *path,
                          const char *query, const char *fragment) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_uri *uri = grpc_uri_parse(&exec_ctx, uri_text, 0);
  GPR_ASSERT(uri);
  GPR_ASSERT(0 == strcmp(scheme, uri->scheme));
  GPR_ASSERT(0 == strcmp(authority, uri->authority));
  GPR_ASSERT(0 == strcmp(path, uri->path));
  GPR_ASSERT(0 == strcmp(query, uri->query));
  GPR_ASSERT(0 == strcmp(fragment, uri->fragment));
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_uri_destroy(uri);
}

static void test_fails(const char *uri_text) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  GPR_ASSERT(NULL == grpc_uri_parse(&exec_ctx, uri_text, 0));
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_query_parts() {
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    const char *uri_text = "http://foo/path?a&b=B&c=&#frag";
    grpc_uri *uri = grpc_uri_parse(&exec_ctx, uri_text, 0);
    GPR_ASSERT(uri);

    GPR_ASSERT(0 == strcmp("http", uri->scheme));
    GPR_ASSERT(0 == strcmp("foo", uri->authority));
    GPR_ASSERT(0 == strcmp("/path", uri->path));
    GPR_ASSERT(0 == strcmp("a&b=B&c=&", uri->query));
    GPR_ASSERT(4 == uri->num_query_parts);

    GPR_ASSERT(0 == strcmp("a", uri->query_parts[0]));
    GPR_ASSERT(NULL == uri->query_parts_values[0]);

    GPR_ASSERT(0 == strcmp("b", uri->query_parts[1]));
    GPR_ASSERT(0 == strcmp("B", uri->query_parts_values[1]));

    GPR_ASSERT(0 == strcmp("c", uri->query_parts[2]));
    GPR_ASSERT(0 == strcmp("", uri->query_parts_values[2]));

    GPR_ASSERT(0 == strcmp("", uri->query_parts[3]));
    GPR_ASSERT(NULL == uri->query_parts_values[3]);

    GPR_ASSERT(NULL == grpc_uri_get_query_arg(uri, "a"));
    GPR_ASSERT(0 == strcmp("B", grpc_uri_get_query_arg(uri, "b")));
    GPR_ASSERT(0 == strcmp("", grpc_uri_get_query_arg(uri, "c")));
    GPR_ASSERT(NULL == grpc_uri_get_query_arg(uri, ""));

    GPR_ASSERT(0 == strcmp("frag", uri->fragment));
    grpc_exec_ctx_finish(&exec_ctx);
    grpc_uri_destroy(uri);
  }
  {
    /* test the current behavior of multiple query part values */
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    const char *uri_text = "http://auth/path?foo=bar=baz&foobar==";
    grpc_uri *uri = grpc_uri_parse(&exec_ctx, uri_text, 0);
    GPR_ASSERT(uri);

    GPR_ASSERT(0 == strcmp("http", uri->scheme));
    GPR_ASSERT(0 == strcmp("auth", uri->authority));
    GPR_ASSERT(0 == strcmp("/path", uri->path));
    GPR_ASSERT(0 == strcmp("foo=bar=baz&foobar==", uri->query));
    GPR_ASSERT(2 == uri->num_query_parts);

    GPR_ASSERT(0 == strcmp("bar", grpc_uri_get_query_arg(uri, "foo")));
    GPR_ASSERT(0 == strcmp("", grpc_uri_get_query_arg(uri, "foobar")));

    grpc_exec_ctx_finish(&exec_ctx);
    grpc_uri_destroy(uri);
  }
  {
    /* empty query */
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    const char *uri_text = "http://foo/path";
    grpc_uri *uri = grpc_uri_parse(&exec_ctx, uri_text, 0);
    GPR_ASSERT(uri);

    GPR_ASSERT(0 == strcmp("http", uri->scheme));
    GPR_ASSERT(0 == strcmp("foo", uri->authority));
    GPR_ASSERT(0 == strcmp("/path", uri->path));
    GPR_ASSERT(0 == strcmp("", uri->query));
    GPR_ASSERT(0 == uri->num_query_parts);
    GPR_ASSERT(NULL == uri->query_parts);
    GPR_ASSERT(NULL == uri->query_parts_values);
    GPR_ASSERT(0 == strcmp("", uri->fragment));
    grpc_exec_ctx_finish(&exec_ctx);
    grpc_uri_destroy(uri);
  }
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
  test_succeeds("ipv6:[2001:db8::1%252]:12345", "ipv6", "",
                "[2001:db8::1%2]:12345", "", "");

  test_fails("xyz");
  test_fails("http:?dangling-pct-%0");
  test_fails("http://foo?[bar]");
  test_fails("http://foo?x[bar]");
  test_fails("http://foo?bar#lol#");

  test_query_parts();
  return 0;
}
