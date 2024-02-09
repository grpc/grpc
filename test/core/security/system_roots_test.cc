//
//
// Copyright 2018 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include <stdio.h>

#if defined(GPR_LINUX) || defined(GPR_FREEBSD) || defined(GPR_APPLE)
#include <string.h>
#include <sys/param.h>

#include "gtest/gtest.h"

#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/gprpp/load_file.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/security_connector/load_system_roots.h"
#include "src/core/lib/security/security_connector/load_system_roots_supported.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/tsi/transport_security.h"
#include "test/core/util/test_config.h"

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
  // Test that CreateRootCertsBundle returns a correct slice.
  absl::string_view roots_bundle_str;
  auto roots_bundle = grpc_core::LoadFile("test/core/security/etc/bundle.pem",
                                          /*add_null_terminator=*/false);
  if (roots_bundle.ok()) roots_bundle_str = roots_bundle->as_string_view();
  // result_slice should have the same content as roots_bundle.
  grpc_core::Slice result_slice(
      grpc_core::CreateRootCertsBundle("test/core/security/etc/test_roots"));
  EXPECT_EQ(result_slice.as_string_view(), roots_bundle_str)
      << "Expected: \"" << result_slice.as_string_view() << "\"\n"
      << "Actual:   \"" << roots_bundle_str << "\"";
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
#else
int main() {
  printf(
      "*** WARNING: this test is only supported on Linux, FreeBSD, and MacOS"
      "systems ***\n");
  return 0;
}
#endif  // GPR_LINUX || GPR_FREEBSD || GPR_APPLE
