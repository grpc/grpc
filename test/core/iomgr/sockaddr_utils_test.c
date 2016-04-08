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

#include "src/core/lib/iomgr/sockaddr_utils.h"

#include <errno.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include "test/core/util/test_config.h"

static struct sockaddr_in make_addr4(const uint8_t *data, size_t data_len) {
  struct sockaddr_in addr4;
  memset(&addr4, 0, sizeof(addr4));
  addr4.sin_family = AF_INET;
  GPR_ASSERT(data_len == sizeof(addr4.sin_addr.s_addr));
  memcpy(&addr4.sin_addr.s_addr, data, data_len);
  addr4.sin_port = htons(12345);
  return addr4;
}

static struct sockaddr_in6 make_addr6(const uint8_t *data, size_t data_len) {
  struct sockaddr_in6 addr6;
  memset(&addr6, 0, sizeof(addr6));
  addr6.sin6_family = AF_INET6;
  GPR_ASSERT(data_len == sizeof(addr6.sin6_addr.s6_addr));
  memcpy(&addr6.sin6_addr.s6_addr, data, data_len);
  addr6.sin6_port = htons(12345);
  return addr6;
}

static const uint8_t kMapped[] = {0, 0, 0,    0,    0,   0, 0, 0,
                                  0, 0, 0xff, 0xff, 192, 0, 2, 1};

static const uint8_t kNotQuiteMapped[] = {0, 0, 0,    0,    0,   0, 0, 0,
                                          0, 0, 0xff, 0xfe, 192, 0, 2, 99};
static const uint8_t kIPv4[] = {192, 0, 2, 1};

static const uint8_t kIPv6[] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0,
                                0,    0,    0,    0,    0, 0, 0, 1};

static void test_sockaddr_is_v4mapped(void) {
  struct sockaddr_in input4;
  struct sockaddr_in6 input6;
  struct sockaddr_in output4;
  struct sockaddr_in expect4;

  gpr_log(GPR_INFO, "%s", "test_sockaddr_is_v4mapped");

  /* v4mapped input should succeed. */
  input6 = make_addr6(kMapped, sizeof(kMapped));
  GPR_ASSERT(grpc_sockaddr_is_v4mapped((const struct sockaddr *)&input6, NULL));
  GPR_ASSERT(
      grpc_sockaddr_is_v4mapped((const struct sockaddr *)&input6, &output4));
  expect4 = make_addr4(kIPv4, sizeof(kIPv4));
  GPR_ASSERT(memcmp(&expect4, &output4, sizeof(expect4)) == 0);

  /* Non-v4mapped input should fail. */
  input6 = make_addr6(kNotQuiteMapped, sizeof(kNotQuiteMapped));
  GPR_ASSERT(
      !grpc_sockaddr_is_v4mapped((const struct sockaddr *)&input6, NULL));
  GPR_ASSERT(
      !grpc_sockaddr_is_v4mapped((const struct sockaddr *)&input6, &output4));
  /* Output is unchanged. */
  GPR_ASSERT(memcmp(&expect4, &output4, sizeof(expect4)) == 0);

  /* Plain IPv4 input should also fail. */
  input4 = make_addr4(kIPv4, sizeof(kIPv4));
  GPR_ASSERT(
      !grpc_sockaddr_is_v4mapped((const struct sockaddr *)&input4, NULL));
}

static void test_sockaddr_to_v4mapped(void) {
  struct sockaddr_in input4;
  struct sockaddr_in6 input6;
  struct sockaddr_in6 output6;
  struct sockaddr_in6 expect6;

  gpr_log(GPR_INFO, "%s", "test_sockaddr_to_v4mapped");

  /* IPv4 input should succeed. */
  input4 = make_addr4(kIPv4, sizeof(kIPv4));
  GPR_ASSERT(
      grpc_sockaddr_to_v4mapped((const struct sockaddr *)&input4, &output6));
  expect6 = make_addr6(kMapped, sizeof(kMapped));
  GPR_ASSERT(memcmp(&expect6, &output6, sizeof(output6)) == 0);

  /* IPv6 input should fail. */
  input6 = make_addr6(kIPv6, sizeof(kIPv6));
  GPR_ASSERT(
      !grpc_sockaddr_to_v4mapped((const struct sockaddr *)&input6, &output6));
  /* Output is unchanged. */
  GPR_ASSERT(memcmp(&expect6, &output6, sizeof(output6)) == 0);

  /* Already-v4mapped input should also fail. */
  input6 = make_addr6(kMapped, sizeof(kMapped));
  GPR_ASSERT(
      !grpc_sockaddr_to_v4mapped((const struct sockaddr *)&input6, &output6));
}

static void test_sockaddr_is_wildcard(void) {
  struct sockaddr_in wild4;
  struct sockaddr_in6 wild6;
  struct sockaddr_in6 wild_mapped;
  struct sockaddr dummy;
  int port;

  gpr_log(GPR_INFO, "%s", "test_sockaddr_is_wildcard");

  /* Generate wildcards. */
  grpc_sockaddr_make_wildcards(555, &wild4, &wild6);
  GPR_ASSERT(
      grpc_sockaddr_to_v4mapped((const struct sockaddr *)&wild4, &wild_mapped));

  /* Test 0.0.0.0:555 */
  port = -1;
  GPR_ASSERT(grpc_sockaddr_is_wildcard((const struct sockaddr *)&wild4, &port));
  GPR_ASSERT(port == 555);
  memset(&wild4.sin_addr.s_addr, 0xbd, 1);
  GPR_ASSERT(
      !grpc_sockaddr_is_wildcard((const struct sockaddr *)&wild4, &port));

  /* Test [::]:555 */
  port = -1;
  GPR_ASSERT(grpc_sockaddr_is_wildcard((const struct sockaddr *)&wild6, &port));
  GPR_ASSERT(port == 555);
  memset(&wild6.sin6_addr.s6_addr, 0xbd, 1);
  GPR_ASSERT(
      !grpc_sockaddr_is_wildcard((const struct sockaddr *)&wild6, &port));

  /* Test [::ffff:0.0.0.0]:555 */
  port = -1;
  GPR_ASSERT(
      grpc_sockaddr_is_wildcard((const struct sockaddr *)&wild_mapped, &port));
  GPR_ASSERT(port == 555);
  memset(&wild_mapped.sin6_addr.s6_addr, 0xbd, 1);
  GPR_ASSERT(
      !grpc_sockaddr_is_wildcard((const struct sockaddr *)&wild_mapped, &port));

  /* Test AF_UNSPEC. */
  port = -1;
  memset(&dummy, 0, sizeof(dummy));
  GPR_ASSERT(!grpc_sockaddr_is_wildcard(&dummy, &port));
  GPR_ASSERT(port == -1);
}

static void expect_sockaddr_str(const char *expected, void *addr,
                                int normalize) {
  int result;
  char *str;
  gpr_log(GPR_INFO, "  expect_sockaddr_str(%s)", expected);
  result = grpc_sockaddr_to_string(&str, (struct sockaddr *)addr, normalize);
  GPR_ASSERT(str != NULL);
  GPR_ASSERT(result >= 0);
  GPR_ASSERT((size_t)result == strlen(str));
  GPR_ASSERT(strcmp(expected, str) == 0);
  gpr_free(str);
}

static void expect_sockaddr_uri(const char *expected, void *addr) {
  char *str;
  gpr_log(GPR_INFO, "  expect_sockaddr_uri(%s)", expected);
  str = grpc_sockaddr_to_uri((struct sockaddr *)addr);
  GPR_ASSERT(str != NULL);
  GPR_ASSERT(strcmp(expected, str) == 0);
  gpr_free(str);
}

static void test_sockaddr_to_string(void) {
  struct sockaddr_in input4;
  struct sockaddr_in6 input6;
  struct sockaddr dummy;

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

  input6 = make_addr6(kMapped, sizeof(kMapped));
  expect_sockaddr_str("[::ffff:192.0.2.1]:12345", &input6, 0);
  expect_sockaddr_str("192.0.2.1:12345", &input6, 1);
  expect_sockaddr_uri("ipv4:192.0.2.1:12345", &input6);

  input6 = make_addr6(kNotQuiteMapped, sizeof(kNotQuiteMapped));
  expect_sockaddr_str("[::fffe:c000:263]:12345", &input6, 0);
  expect_sockaddr_str("[::fffe:c000:263]:12345", &input6, 1);
  expect_sockaddr_uri("ipv6:[::fffe:c000:263]:12345", &input6);

  memset(&dummy, 0, sizeof(dummy));
  dummy.sa_family = 123;
  expect_sockaddr_str("(sockaddr family=123)", &dummy, 0);
  expect_sockaddr_str("(sockaddr family=123)", &dummy, 1);
  GPR_ASSERT(grpc_sockaddr_to_uri(&dummy) == NULL);

  GPR_ASSERT(errno == 0x7EADBEEF);
}

static void test_sockaddr_set_get_port(void) {
  struct sockaddr_in input4;
  struct sockaddr_in6 input6;
  struct sockaddr dummy;

  gpr_log(GPR_DEBUG, "test_sockaddr_set_get_port");

  input4 = make_addr4(kIPv4, sizeof(kIPv4));
  GPR_ASSERT(grpc_sockaddr_get_port((struct sockaddr *)&input4) == 12345);
  GPR_ASSERT(grpc_sockaddr_set_port((struct sockaddr *)&input4, 54321));
  GPR_ASSERT(grpc_sockaddr_get_port((struct sockaddr *)&input4) == 54321);

  input6 = make_addr6(kIPv6, sizeof(kIPv6));
  GPR_ASSERT(grpc_sockaddr_get_port((struct sockaddr *)&input6) == 12345);
  GPR_ASSERT(grpc_sockaddr_set_port((struct sockaddr *)&input6, 54321));
  GPR_ASSERT(grpc_sockaddr_get_port((struct sockaddr *)&input6) == 54321);

  memset(&dummy, 0, sizeof(dummy));
  dummy.sa_family = 123;
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
