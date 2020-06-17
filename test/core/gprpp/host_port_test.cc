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

#include "src/core/lib/gprpp/host_port.h"
#include "test/core/util/test_config.h"

static void join_host_port_expect(const char* host, int port,
                                  const char* expected) {
  std::string actual = grpc_core::JoinHostPort(host, port);
  GPR_ASSERT(actual == expected);
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

static void split_host_port_expect(const char* name, const char* host,
                                   const char* port, bool ret) {
  std::string actual_host;
  std::string actual_port;
  const bool actual_ret =
      grpc_core::SplitHostPort(name, &actual_host, &actual_port);
  GPR_ASSERT(actual_ret == ret);
  GPR_ASSERT(actual_host == (host == nullptr ? "" : host));
  GPR_ASSERT(actual_port == (port == nullptr ? "" : port));
}

static void test_split_host_port() {
  split_host_port_expect("", "", nullptr, true);
  split_host_port_expect("[a:b]", "a:b", nullptr, true);
  split_host_port_expect("1.2.3.4", "1.2.3.4", nullptr, true);
  split_host_port_expect("0.0.0.0:", "0.0.0.0", "", true);
  split_host_port_expect("a:b:c::", "a:b:c::", nullptr, true);
  split_host_port_expect("[a:b:c::]:", "a:b:c::", "", true);
  split_host_port_expect("[a:b]:30", "a:b", "30", true);
  split_host_port_expect("1.2.3.4:30", "1.2.3.4", "30", true);
  split_host_port_expect(":30", "", "30", true);
}

static void test_split_host_port_invalid() {
  split_host_port_expect("[a:b", nullptr, nullptr, false);
  split_host_port_expect("[a:b]30", nullptr, nullptr, false);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);

  test_join_host_port();
  test_join_host_port_garbage();
  test_split_host_port();
  test_split_host_port_invalid();

  return 0;
}
