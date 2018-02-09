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

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/host_port.h"
#include "test/core/util/test_config.h"

static void join_host_port_expect(const char* host, int port,
                                  const char* expected) {
  char* buf;
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

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);

  test_join_host_port();
  test_join_host_port_garbage();

  return 0;
}
