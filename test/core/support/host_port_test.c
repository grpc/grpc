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

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include "test/core/util/test_config.h"

static void join_host_port_expect(const char *host, int port,
                                  const char *expected) {
  char *buf;
  int len;
  len = gpr_join_host_port(&buf, host, port);
  GPR_ASSERT(len >= 0);
  GPR_ASSERT(strlen(expected) == (size_t)len);
  GPR_ASSERT(strcmp(expected, buf) == 0);
  gpr_free(buf);
}

static void test_join_host_port(void) {
  join_host_port_expect("foo", 101, "foo:101");
  join_host_port_expect("", 102, ":102");
  join_host_port_expect("1::2", 103, "[1::2]:103");
  join_host_port_expect("[::1]", 104, "[::1]:104");
}

/* Garbage in, garbage out. */
static void test_join_host_port_garbage(void) {
  join_host_port_expect("[foo]", 105, "[foo]:105");
  join_host_port_expect("[::", 106, "[:::106");
  join_host_port_expect("::]", 107, "[::]]:107");
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);

  test_join_host_port();
  test_join_host_port_garbage();

  return 0;
}
