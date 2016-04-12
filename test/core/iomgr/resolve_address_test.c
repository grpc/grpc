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
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "test/core/util/test_config.h"

static gpr_timespec test_deadline(void) {
  return GRPC_TIMEOUT_SECONDS_TO_DEADLINE(100);
}

static void must_succeed(grpc_exec_ctx *exec_ctx, void *evp,
                         grpc_resolved_addresses *p) {
  GPR_ASSERT(p);
  GPR_ASSERT(p->naddrs >= 1);
  grpc_resolved_addresses_destroy(p);
  gpr_event_set(evp, (void *)1);
}

static void must_fail(grpc_exec_ctx *exec_ctx, void *evp,
                      grpc_resolved_addresses *p) {
  GPR_ASSERT(!p);
  gpr_event_set(evp, (void *)1);
}

static void test_localhost(void) {
  gpr_event ev;
  gpr_event_init(&ev);
  grpc_resolve_address("localhost:1", NULL, must_succeed, &ev);
  GPR_ASSERT(gpr_event_wait(&ev, test_deadline()));
}

static void test_default_port(void) {
  gpr_event ev;
  gpr_event_init(&ev);
  grpc_resolve_address("localhost", "1", must_succeed, &ev);
  GPR_ASSERT(gpr_event_wait(&ev, test_deadline()));
}

static void test_missing_default_port(void) {
  gpr_event ev;
  gpr_event_init(&ev);
  grpc_resolve_address("localhost", NULL, must_fail, &ev);
  GPR_ASSERT(gpr_event_wait(&ev, test_deadline()));
}

static void test_ipv6_with_port(void) {
  gpr_event ev;
  gpr_event_init(&ev);
  grpc_resolve_address("[2001:db8::1]:1", NULL, must_succeed, &ev);
  GPR_ASSERT(gpr_event_wait(&ev, test_deadline()));
}

static void test_ipv6_without_port(void) {
  const char *const kCases[] = {
      "2001:db8::1", "2001:db8::1.2.3.4", "[2001:db8::1]",
  };
  unsigned i;
  for (i = 0; i < sizeof(kCases) / sizeof(*kCases); i++) {
    gpr_event ev;
    gpr_event_init(&ev);
    grpc_resolve_address(kCases[i], "80", must_succeed, &ev);
    GPR_ASSERT(gpr_event_wait(&ev, test_deadline()));
  }
}

static void test_invalid_ip_addresses(void) {
  const char *const kCases[] = {
      "293.283.1238.3:1", "[2001:db8::11111]:1",
  };
  unsigned i;
  for (i = 0; i < sizeof(kCases) / sizeof(*kCases); i++) {
    gpr_event ev;
    gpr_event_init(&ev);
    grpc_resolve_address(kCases[i], NULL, must_fail, &ev);
    GPR_ASSERT(gpr_event_wait(&ev, test_deadline()));
  }
}

static void test_unparseable_hostports(void) {
  const char *const kCases[] = {
      "[", "[::1", "[::1]bad", "[1.2.3.4]", "[localhost]", "[localhost]:1",
  };
  unsigned i;
  for (i = 0; i < sizeof(kCases) / sizeof(*kCases); i++) {
    gpr_event ev;
    gpr_event_init(&ev);
    grpc_resolve_address(kCases[i], "1", must_fail, &ev);
    GPR_ASSERT(gpr_event_wait(&ev, test_deadline()));
  }
}

static void test_to_string() {
  {
    const char *const kCases[] = {
        "2001:db8::1", "2001:db8::1.2.3.4", "[2001:db8::1]:31415",
        "192.168.1.2:1234",
    };
    const char *const kExpected[] = {"[2001:db8::1]:80",
                                     "[2001:db8::102:304]:80",
                                     "[2001:db8::1]:31415", "192.168.1.2:1234"};

    for (size_t i = 0; i < GPR_ARRAY_SIZE(kCases); i++) {
      grpc_resolved_addresses *resolved =
          grpc_blocking_resolve_address(kCases[i], "80");
      GPR_ASSERT(resolved->naddrs == 1);
      char *addr_str = grpc_resolved_address_to_string(&resolved->addrs[0]);
      GPR_ASSERT(addr_str != NULL);
      GPR_ASSERT(strcmp(addr_str, kExpected[i]) == 0);
      gpr_free(addr_str);
      grpc_resolved_addresses_destroy(resolved);
    }
  }
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_executor_init();
  grpc_iomgr_init();
  test_localhost();
  test_default_port();
  test_missing_default_port();
  test_ipv6_with_port();
  test_ipv6_without_port();
  test_invalid_ip_addresses();
  test_unparseable_hostports();
  test_to_string();
  grpc_iomgr_shutdown();
  grpc_executor_shutdown();
  return 0;
}
