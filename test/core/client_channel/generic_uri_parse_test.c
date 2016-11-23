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

#include "src/core/ext/client_channel/generic_uri_parse.h"

#include <string.h>

#include <grpc/support/log.h>

#include "test/core/util/test_config.h"

static void fake_host_port_parser_ref(grpc_host_port_parser *parser);
static void fake_host_port_parser_unref(grpc_host_port_parser *parser);
static int fake_host_port_parser_join_host_port(grpc_host_port_parser *parser, char **joined_host_port, const char *host, const char *port);
static int fake_host_port_parser_split_host_port(grpc_host_port_parser *parser, const char *joined_host_port, char **host, char **port);

static grpc_host_port_parser_vtable fake_host_port_parser_vtable = {
  fake_host_port_parser_ref,
  fake_host_port_parser_unref,
  fake_host_port_parser_join_host_port,
  fake_host_port_parser_split_host_port,
  "fake",
};

static grpc_host_port_parser fake_host_port_parser = {
  &fake_host_port_parser_vtable,
};

static void fake_host_port_parser_ref(grpc_host_port_parser *parser) {}

static void fake_host_port_parser_unref(grpc_host_port_parser *parser) {}

static int fake_host_port_parser_join_host_port(grpc_host_port_parser *parser, char **joined_host_port, const char *host, const char *port) {
  GPR_ASSERT(parser == &fake_host_port_parser);
  *joined_host_port = "fake:fake_host_port";
  return 0;
}

static int fake_host_port_parser_split_host_port(grpc_host_port_parser *parser, const char *joined_host_port, char **host, char **port) {
  GPR_ASSERT(parser == &fake_host_port_parser);
  *host = "fake_host";
  *port = "fake_port";
  return 0;
}

static void init_fake_host_port_parser() {
  grpc_register_host_port_parser(&fake_host_port_parser);
}

static void test_split_host_port_succeeds(char *joined_host_port, char *expected_host, char *expected_port) {
  char *actual_host, *actual_port;
  gpr_log(GPR_INFO, "testing grpc_generic_split_host_port(%s, *expected_host, *expected_port)", joined_host_port);
  gpr_log(GPR_INFO, "expected_host: %s. expected_port: %s", expected_host, expected_port);
  grpc_generic_split_host_port(joined_host_port, &actual_host, &actual_port);
  gpr_log(GPR_INFO, "actual host: %s", actual_host);
  GPR_ASSERT(0 == strcmp(expected_host, actual_host));
  gpr_log(GPR_INFO, "test succeeds");
}

static void test_join_host_port_succeeds(const char *expected_host_port, const char *host, const char *port) {
  char *joined_host_port;
  gpr_log(GPR_INFO, "testing grpc_generic_join_host_port(*expected_host_port, %s, %s)", host, port);
  gpr_log(GPR_INFO, "expected_host_port: %s.", expected_host_port);
  grpc_generic_join_host_port(&joined_host_port, host, port);
  gpr_log(GPR_INFO, "actual host_port: %s", joined_host_port);
  GPR_ASSERT(0 == strcmp(expected_host_port, joined_host_port));
  gpr_log(GPR_INFO, "test succeeds");
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_init();

  init_fake_host_port_parser();

  test_split_host_port_succeeds("foo:2181", "foo", "2181");
  test_split_host_port_succeeds("127.0.0.1:2181", "127.0.0.1", "2181");
  test_split_host_port_succeeds("[::]:1234", "::", "1234");

  test_split_host_port_succeeds("fake:foo:2181", "fake_host", "fake_port");
  test_split_host_port_succeeds("fake:foo:2181", "fake_host", "fake_port");

  test_join_host_port_succeeds("foo:2181", "foo", "2181");
  test_join_host_port_succeeds("127.0.0.1:2181", "127.0.0.1", "2181");
  test_join_host_port_succeeds("[::]:1234", "::", "1234");

  test_join_host_port_succeeds("fake:fake_host_port", "fake:foo", "2181");
  test_join_host_port_succeeds("fake:fake_host_port", "fake:127.0.0.1", "1234");

  grpc_shutdown();
}
