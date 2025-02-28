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

#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <stdio.h>
#include <string.h>

#include "gtest/gtest.h"
#include "src/core/credentials/transport/alts/check_gcp_environment.h"
#include "src/core/util/crash.h"
#include "src/core/util/tmpfile.h"

#ifdef GPR_WINDOWS

namespace grpc_core {
namespace internal {

bool check_windows_registry_product_name(HKEY root_key,
                                         const char* reg_key_path,
                                         const char* reg_key_name);

}  // namespace internal
}  // namespace grpc_core

static bool check_bios_data_windows_test(const char* data) {
  char const reg_key_path[] = "SYSTEM\\HardwareConfig\\Current\\";
  char const reg_key_name[] = "grpcTestValueName";
  // Modify the registry for the current user to contain the
  // test value. We cannot use the system registry because the
  // user may not have privileges to change it.
  auto rc = RegSetKeyValueA(HKEY_CURRENT_USER, reg_key_path, reg_key_name,
                            REG_SZ, reinterpret_cast<const BYTE*>(data),
                            static_cast<DWORD>(strlen(data)));
  if (rc != 0) {
    return false;
  }

  auto result = grpc_core::internal::check_windows_registry_product_name(
      HKEY_CURRENT_USER, reg_key_path, reg_key_name);

  (void)RegDeleteKeyValueA(HKEY_CURRENT_USER, reg_key_path, reg_key_name);

  return result;
}

TEST(CheckGcpEnvironmentWindowsTest, GcpEnvironmentCheckSuccess) {
  // This is the only value observed in production.
  ASSERT_TRUE(check_bios_data_windows_test("Google Compute Engine"));
  // Be generous and accept other values that were accepted by the previous
  // implementation.
  ASSERT_TRUE(check_bios_data_windows_test("Google"));
  ASSERT_TRUE(check_bios_data_windows_test("Google\n"));
  ASSERT_TRUE(check_bios_data_windows_test("Google\r"));
  ASSERT_TRUE(check_bios_data_windows_test("Google\r\n"));
  ASSERT_TRUE(check_bios_data_windows_test("   Google   \r\n"));
  ASSERT_TRUE(check_bios_data_windows_test(" \t\t Google\r\n"));
  ASSERT_TRUE(check_bios_data_windows_test(" \t\t Google\t\t  \r\n"));
}

TEST(CheckGcpEnvironmentWindowsTest, GcpEnvironmentCheckFailure) {
  ASSERT_FALSE(check_bios_data_windows_test("\t\tAmazon\n"));
  ASSERT_FALSE(check_bios_data_windows_test("  Amazon\r\n"));
}

#endif  // GPR_WINDOWS

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
