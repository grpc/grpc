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

/* With the addition of a libuv endpoint, sockaddr.h now includes uv.h when
   using that endpoint. Because of various transitive includes in uv.h,
   including windows.h on Windows, uv.h must be included before other system
   headers. Therefore, sockaddr.h must always be included first */
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"

#include <errno.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include "test/core/util/test_config.h"

static grpc_resolved_address make_addr4(const uint8_t *data, size_t data_len) {
  grpc_resolved_address resolved_addr4;
  struct sockaddr_in *addr4 = (struct sockaddr_in *)resolved_addr4.addr;
  memset(&resolved_addr4, 0, sizeof(resolved_addr4));
  addr4->sin_family = AF_INET;
  GPR_ASSERT(data_len == sizeof(addr4->sin_addr.s_addr));
  memcpy(&addr4->sin_addr.s_addr, data, data_len);
  addr4->sin_port = htons(12345);
  resolved_addr4.len = sizeof(struct sockaddr_in);
  return resolved_addr4;
}

static grpc_resolved_address make_addr6(const uint8_t *data, size_t data_len) {
  grpc_resolved_address resolved_addr6;
  struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)resolved_addr6.addr;
  memset(&resolved_addr6, 0, sizeof(resolved_addr6));
  addr6->sin6_family = AF_INET6;
  GPR_ASSERT(data_len == sizeof(addr6->sin6_addr.s6_addr));
  memcpy(&addr6->sin6_addr.s6_addr, data, data_len);
  addr6->sin6_port = htons(12345);
  resolved_addr6.len = sizeof(struct sockaddr_in6);
  return resolved_addr6;
}

static void set_addr6_scope_id(grpc_resolved_address *addr, uint32_t scope_id) {
  struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)addr->addr;
  GPR_ASSERT(addr6->sin6_family == AF_INET6);
  addr6->sin6_scope_id = scope_id;
}

static const uint8_t kMapped[] = {0, 0, 0,    0,    0,   0, 0, 0,
                                  0, 0, 0xff, 0xff, 192, 0, 2, 1};

static const uint8_t kNotQuiteMapped[] = {0, 0, 0,    0,    0,   0, 0, 0,
                                          0, 0, 0xff, 0xfe, 192, 0, 2, 99};
static const uint8_t kIPv4[] = {192, 0, 2, 1};

static const uint8_t kIPv6[] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0,
                                0,    0,    0,    0,    0, 0, 0, 1};

static void test_sockaddr_is_v4mapped(void) {
  grpc_resolved_address input4;
  grpc_resolved_address input6;
  grpc_resolved_address output4;
  grpc_resolved_address expect4;

  gpr_log(GPR_INFO, "%s", "test_sockaddr_is_v4mapped");

  /* v4mapped input should succeed. */
  input6 = make_addr6(kMapped, sizeof(kMapped));
  GPR_ASSERT(grpc_sockaddr_is_v4mapped(&input6, NULL));
  GPR_ASSERT(grpc_sockaddr_is_v4mapped(&input6, &output4));
  expect4 = make_addr4(kIPv4, sizeof(kIPv4));
  GPR_ASSERT(memcmp(&expect4, &output4, sizeof(expect4)) == 0);

  /* Non-v4mapped input should fail. */
  input6 = make_addr6(kNotQuiteMapped, sizeof(kNotQuiteMapped));
  GPR_ASSERT(!grpc_sockaddr_is_v4mapped(&input6, NULL));
  GPR_ASSERT(!grpc_sockaddr_is_v4mapped(&input6, &output4));
  /* Output is unchanged. */
  GPR_ASSERT(memcmp(&expect4, &output4, sizeof(expect4)) == 0);

  /* Plain IPv4 input should also fail. */
  input4 = make_addr4(kIPv4, sizeof(kIPv4));
  GPR_ASSERT(!grpc_sockaddr_is_v4mapped(&input4, NULL));
}

static void test_sockaddr_to_v4mapped(void) {
  grpc_resolved_address input4;
  grpc_resolved_address input6;
  grpc_resolved_address output6;
  grpc_resolved_address expect6;

  gpr_log(GPR_INFO, "%s", "test_sockaddr_to_v4mapped");

  /* IPv4 input should succeed. */
  input4 = make_addr4(kIPv4, sizeof(kIPv4));
  GPR_ASSERT(grpc_sockaddr_to_v4mapped(&input4, &output6));
  expect6 = make_addr6(kMapped, sizeof(kMapped));
  GPR_ASSERT(memcmp(&expect6, &output6, sizeof(output6)) == 0);

  /* IPv6 input should fail. */
  input6 = make_addr6(kIPv6, sizeof(kIPv6));
  GPR_ASSERT(!grpc_sockaddr_to_v4mapped(&input6, &output6));
  /* Output is unchanged. */
  GPR_ASSERT(memcmp(&expect6, &output6, sizeof(output6)) == 0);

  /* Already-v4mapped input should also fail. */
  input6 = make_addr6(kMapped, sizeof(kMapped));
  GPR_ASSERT(!grpc_sockaddr_to_v4mapped(&input6, &output6));
}

static void test_sockaddr_is_wildcard(void) {
  grpc_resolved_address wild4;
  grpc_resolved_address wild6;
  grpc_resolved_address wild_mapped;
  grpc_resolved_address dummy;
  struct sockaddr_in *wild4_addr;
  struct sockaddr_in6 *wild6_addr;
  struct sockaddr_in6 *wild_mapped_addr;
  int port;

  gpr_log(GPR_INFO, "%s", "test_sockaddr_is_wildcard");

  /* Generate wildcards. */
  grpc_sockaddr_make_wildcards(555, &wild4, &wild6);
  GPR_ASSERT(grpc_sockaddr_to_v4mapped(&wild4, &wild_mapped));

  /* Test 0.0.0.0:555 */
  port = -1;
  GPR_ASSERT(grpc_sockaddr_is_wildcard(&wild4, &port));
  GPR_ASSERT(port == 555);
  wild4_addr = (struct sockaddr_in *)&wild4.addr;
  memset(&wild4_addr->sin_addr.s_addr, 0xbd, 1);
  GPR_ASSERT(!grpc_sockaddr_is_wildcard(&wild4, &port));

  /* Test [::]:555 */
  port = -1;
  GPR_ASSERT(grpc_sockaddr_is_wildcard(&wild6, &port));
  GPR_ASSERT(port == 555);
  wild6_addr = (struct sockaddr_in6 *)&wild6.addr;
  memset(&wild6_addr->sin6_addr.s6_addr, 0xbd, 1);
  GPR_ASSERT(!grpc_sockaddr_is_wildcard(&wild6, &port));

  /* Test [::ffff:0.0.0.0]:555 */
  port = -1;
  GPR_ASSERT(grpc_sockaddr_is_wildcard(&wild_mapped, &port));
  GPR_ASSERT(port == 555);
  wild_mapped_addr = (struct sockaddr_in6 *)&wild_mapped.addr;
  memset(&wild_mapped_addr->sin6_addr.s6_addr, 0xbd, 1);
  GPR_ASSERT(!grpc_sockaddr_is_wildcard(&wild_mapped, &port));

  /* Test AF_UNSPEC. */
  port = -1;
  memset(&dummy, 0, sizeof(dummy));
  GPR_ASSERT(!grpc_sockaddr_is_wildcard(&dummy, &port));
  GPR_ASSERT(port == -1);
}

static void expect_sockaddr_str(const char *expected,
                                grpc_resolved_address *addr, int normalize) {
  int result;
  char *str;
  gpr_log(GPR_INFO, "  expect_sockaddr_str(%s)", expected);
  result = grpc_sockaddr_to_string(&str, addr, normalize);
  GPR_ASSERT(str != NULL);
  GPR_ASSERT(result >= 0);
  GPR_ASSERT((size_t)result == strlen(str));
  GPR_ASSERT(strcmp(expected, str) == 0);
  gpr_free(str);
}

static void expect_sockaddr_uri(const char *expected,
                                grpc_resolved_address *addr) {
  char *str;
  gpr_log(GPR_INFO, "  expect_sockaddr_uri(%s)", expected);
  str = grpc_sockaddr_to_uri(addr);
  GPR_ASSERT(str != NULL);
  GPR_ASSERT(strcmp(expected, str) == 0);
  gpr_free(str);
}

static void test_sockaddr_to_string(void) {
  grpc_resolved_address input4;
  grpc_resolved_address input6;
  grpc_resolved_address dummy;
  struct sockaddr *dummy_addr;

  gpr_log(GPR_INFO, "%s", "test_sockaddr_to_string");

  errno = 0x7EADBEEF;

  input4 = make_addr4(kIPv4, sizeof(kIPv4));
  expect_sockaddr_str("192.0.2.1:12345", &input4, 0);
  expect_sockaddr_str("192.0.2.1:12345", &input4, 1);
  expect_sockaddr_uri("ipv4:192.0.2.1:12345", &input4);

  input6 = make_addr6(kIPv6, sizeof(kIPv6));
  expect_sockaddr_str("[2001:db8::1]:12345", &input6, 0);
  expect_sockaddr_str("[2001:db8::1]:12345", &input6, 1);
  expect_sockaddr_uri("ipv6:[2001:db8::1]:12345", &input6);

  set_addr6_scope_id(&input6, 2);
  expect_sockaddr_str("[2001:db8::1%252]:12345", &input6, 0);
  expect_sockaddr_str("[2001:db8::1%252]:12345", &input6, 1);
  expect_sockaddr_uri("ipv6:[2001:db8::1%252]:12345", &input6);

  set_addr6_scope_id(&input6, 101);
  expect_sockaddr_str("[2001:db8::1%25101]:12345", &input6, 0);
  expect_sockaddr_str("[2001:db8::1%25101]:12345", &input6, 1);
  expect_sockaddr_uri("ipv6:[2001:db8::1%25101]:12345", &input6);

  input6 = make_addr6(kMapped, sizeof(kMapped));
  expect_sockaddr_str("[::ffff:192.0.2.1]:12345", &input6, 0);
  expect_sockaddr_str("192.0.2.1:12345", &input6, 1);
  expect_sockaddr_uri("ipv4:192.0.2.1:12345", &input6);

  input6 = make_addr6(kNotQuiteMapped, sizeof(kNotQuiteMapped));
  expect_sockaddr_str("[::fffe:c000:263]:12345", &input6, 0);
  expect_sockaddr_str("[::fffe:c000:263]:12345", &input6, 1);
  expect_sockaddr_uri("ipv6:[::fffe:c000:263]:12345", &input6);

  memset(&dummy, 0, sizeof(dummy));
  dummy_addr = (struct sockaddr *)dummy.addr;
  dummy_addr->sa_family = 123;
  expect_sockaddr_str("(sockaddr family=123)", &dummy, 0);
  expect_sockaddr_str("(sockaddr family=123)", &dummy, 1);
  GPR_ASSERT(grpc_sockaddr_to_uri(&dummy) == NULL);
}

static void test_sockaddr_set_get_port(void) {
  grpc_resolved_address input4;
  grpc_resolved_address input6;
  grpc_resolved_address dummy;
  struct sockaddr *dummy_addr;

  gpr_log(GPR_DEBUG, "test_sockaddr_set_get_port");

  input4 = make_addr4(kIPv4, sizeof(kIPv4));
  GPR_ASSERT(grpc_sockaddr_get_port(&input4) == 12345);
  GPR_ASSERT(grpc_sockaddr_set_port(&input4, 54321));
  GPR_ASSERT(grpc_sockaddr_get_port(&input4) == 54321);

  input6 = make_addr6(kIPv6, sizeof(kIPv6));
  GPR_ASSERT(grpc_sockaddr_get_port(&input6) == 12345);
  GPR_ASSERT(grpc_sockaddr_set_port(&input6, 54321));
  GPR_ASSERT(grpc_sockaddr_get_port(&input6) == 54321);

  memset(&dummy, 0, sizeof(dummy));
  dummy_addr = (struct sockaddr *)dummy.addr;
  dummy_addr->sa_family = 123;
  GPR_ASSERT(grpc_sockaddr_get_port(&dummy) == 0);
  GPR_ASSERT(grpc_sockaddr_set_port(&dummy, 1234) == 0);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);

  test_sockaddr_is_v4mapped();
  test_sockaddr_to_v4mapped();
  test_sockaddr_is_wildcard();
  test_sockaddr_to_string();
  test_sockaddr_set_get_port();

  return 0;
}
