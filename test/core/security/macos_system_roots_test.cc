/*
 *
 * Copyright 2018 gRPC authors.
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

#include <grpc/support/port_platform.h>
#include <stdio.h>

#if defined GPR_APPLE && !defined GPR_CPU_IPHONE
// TODO: see
// src/core/lib/security/security_connector/load_system_roots_macos.cc.
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <sys/param.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/security_connector/load_system_roots.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/tsi/transport_security.h"
#include "test/core/util/test_config.h"

#include "gtest/gtest.h"

#include "src/core/lib/security/security_connector/load_system_roots_macos.h"

#ifndef GRPC_USE_SYSTEM_SSL_ROOTS_ENV_VAR
#define GRPC_USE_SYSTEM_SSL_ROOTS_ENV_VAR "GRPC_USE_SYSTEM_SSL_ROOTS"
#endif  // GRPC_USE_SYSTEM_SSL_ROOTS_ENV_VAR

namespace grpc {
namespace {

int grpc_macos_system_roots_fail(CFDataRef* data, CFDataRef* untrusted) {
  return -1;
}

int grpc_macos_system_roots_dummy(CFDataRef* data, CFDataRef* untrusted) {
  const char* buf = "DUMMY DATA";
  CFDataRef dummy_data = CFDataCreate(kCFAllocatorDefault, (const UInt8*)buf,
                                      (CFIndex)strlen(buf));
  *data = dummy_data;
  CFRelease(CFTypeRef(dummy_data));
  return 0;
}

TEST(MacOSRootsTest, FailsCorrectly) {
  // Set roots getter to fail to test that GetMacOSRootCerts returns -1 when it
  // should.
  grpc_slice result = grpc_empty_slice();
  int err = grpc_core::GetMacOSRootCerts(&result, grpc_macos_system_roots_fail);
  EXPECT_EQ(err, -1);
  grpc_slice_unref(result);
}

TEST(MacOSRootsTest, ConvertsCorrectly) {
  // Test that GetMacOSRootCerts properly converts from CFDataRef to grpc_slice.
  grpc_slice result = grpc_empty_slice();
  int err =
      grpc_core::GetMacOSRootCerts(&result, grpc_macos_system_roots_dummy);
  char* test_slice_str = grpc_slice_to_c_string(result);
  EXPECT_EQ(err, 0);
  EXPECT_STREQ(test_slice_str, "DUMMY DATA");
  grpc_slice_unref(result);
  gpr_free(test_slice_str);
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
#else
int main() {
  printf("*** WARNING: this test is only supported on MacOS systems ***\n");
  return 0;
}
#endif  // GPR_APPLE
