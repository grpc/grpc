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
#include <string.h>

#include <gtest/gtest.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/security/credentials/alts/check_gcp_environment.h"

#if GPR_LINUX

static bool check_bios_data_linux_test(const char* data) {
  // Create a file with contents data.
  char* filename = nullptr;
  FILE* fp = gpr_tmpfile("check_gcp_environment_test", &filename);
  EXPECT_NE(filename, nullptr);
  EXPECT_NE(fp, nullptr);
  EXPECT_EQ(fwrite(data, 1, strlen(data), fp), strlen(data));
  fclose(fp);
  bool result = grpc_core::internal::check_bios_data(
      reinterpret_cast<const char*>(filename));
  // Cleanup.
  remove(filename);
  gpr_free(filename);
  return result;
}

TEST(CheckGcpEnvironmentLinuxTest, GcpEnvironmentCheckSuccess) {
  // Exact match.
  ASSERT_TRUE(check_bios_data_linux_test("Google"));
  ASSERT_TRUE(check_bios_data_linux_test("Google Compute Engine"));
  // With leading and trailing whitespaces.
  ASSERT_TRUE(check_bios_data_linux_test(" Google  "));
  ASSERT_TRUE(check_bios_data_linux_test("Google  "));
  ASSERT_TRUE(check_bios_data_linux_test("   Google"));
  ASSERT_TRUE(check_bios_data_linux_test("  Google Compute Engine  "));
  ASSERT_TRUE(check_bios_data_linux_test("Google Compute Engine  "));
  ASSERT_TRUE(check_bios_data_linux_test("  Google Compute Engine"));
  // With leading and trailing \t and \n.
  ASSERT_TRUE(check_bios_data_linux_test("\t\tGoogle Compute Engine\t"));
  ASSERT_TRUE(check_bios_data_linux_test("Google Compute Engine\n"));
  ASSERT_TRUE(check_bios_data_linux_test("\n\n\tGoogle Compute Engine \n\t\t"));
}

TEST(CheckGcpEnvironmentLinuxTest, GcpEnvironmentCheckFailure) {
  ASSERT_FALSE(check_bios_data_linux_test("non_existing-file"));
  ASSERT_FALSE(check_bios_data_linux_test("Google-Chrome"));
  ASSERT_FALSE(check_bios_data_linux_test("Amazon"));
  ASSERT_FALSE(check_bios_data_linux_test("Google-Chrome\t\t"));
  ASSERT_FALSE(check_bios_data_linux_test("Amazon"));
  ASSERT_FALSE(check_bios_data_linux_test("\n"));
}

#endif  // GPR_LINUX

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
