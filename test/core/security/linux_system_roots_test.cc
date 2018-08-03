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

#ifdef GPR_LINUX
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <string.h>
#include <sys/param.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/security_connector/load_system_roots.h"
#include "src/core/lib/security/security_connector/load_system_roots_linux.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/tsi/transport_security.h"
#include "test/core/util/test_config.h"

#include "gtest/gtest.h"

#ifndef GRPC_USE_SYSTEM_SSL_ROOTS_ENV_VAR
#define GRPC_USE_SYSTEM_SSL_ROOTS_ENV_VAR "GRPC_USE_SYSTEM_SSL_ROOTS"
#endif  // GRPC_USE_SYSTEM_SSL_ROOTS_ENV_VAR

namespace grpc {
namespace {

TEST(AbsoluteFilePathTest, ConcatenatesCorrectly) {
  const char* directory = "nonexistent/test/directory";
  const char* filename = "doesnotexist.txt";
  char result_path[MAXPATHLEN];
  grpc_core::GetAbsoluteFilePath(directory, filename, result_path);
  EXPECT_STREQ(result_path, "nonexistent/test/directory/doesnotexist.txt");
}

TEST(CreateRootCertsBundleTest, ReturnsEmpty) {
  // Test that CreateRootCertsBundle returns an empty slice for null or
  // nonexistent cert directories.
  grpc_slice result_slice = grpc_core::CreateRootCertsBundle(nullptr);
  EXPECT_TRUE(GRPC_SLICE_IS_EMPTY(result_slice));
  grpc_slice_unref(result_slice);
  result_slice = grpc_core::CreateRootCertsBundle("does/not/exist");
  EXPECT_TRUE(GRPC_SLICE_IS_EMPTY(result_slice));
  grpc_slice_unref(result_slice);
}

TEST(CreateRootCertsBundleTest, BundlesCorrectly) {
  gpr_setenv(GRPC_USE_SYSTEM_SSL_ROOTS_ENV_VAR, "true");
  // Test that CreateRootCertsBundle returns a correct slice.
  grpc_slice roots_bundle = grpc_empty_slice();
  GRPC_LOG_IF_ERROR(
      "load_file",
      grpc_load_file("test/core/security/etc/bundle.pem", 1, &roots_bundle));
  // result_slice should have the same content as roots_bundle.
  grpc_slice result_slice =
      grpc_core::CreateRootCertsBundle("test/core/security/etc/test_roots");
  char* result_str = grpc_slice_to_c_string(result_slice);
  char* bundle_str = grpc_slice_to_c_string(roots_bundle);
  EXPECT_STREQ(result_str, bundle_str);
  // Clean up.
  unsetenv(GRPC_USE_SYSTEM_SSL_ROOTS_ENV_VAR);
  gpr_free(result_str);
  gpr_free(bundle_str);
  grpc_slice_unref(roots_bundle);
  grpc_slice_unref(result_slice);
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
  printf("*** WARNING: this test is only supported on Linux systems ***\n");
  return 0;
}
#endif  // GPR_LINUX
