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

#include "src/core/lib/security/credentials/alts/check_gcp_environment.h"

#if GPR_LINUX

#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/tmpfile.h"

static bool check_bios_data_linux_test(const char* data) {
  /* Create a file with contents data. */
  char* filename = nullptr;
  FILE* fp = gpr_tmpfile("check_gcp_environment_test", &filename);
  GPR_ASSERT(filename != nullptr);
  GPR_ASSERT(fp != nullptr);
  GPR_ASSERT(fwrite(data, 1, strlen(data), fp) == strlen(data));
  fclose(fp);
  bool result = grpc_core::internal::check_bios_data(
      reinterpret_cast<const char*>(filename));
  /* Cleanup. */
  remove(filename);
  gpr_free(filename);
  return result;
}

static void test_gcp_environment_check_success() {
  /* Exact match. */
  GPR_ASSERT(check_bios_data_linux_test("Google"));
  GPR_ASSERT(check_bios_data_linux_test("Google Compute Engine"));
  /* With leading and trailing whitespaces. */
  GPR_ASSERT(check_bios_data_linux_test(" Google  "));
  GPR_ASSERT(check_bios_data_linux_test("Google  "));
  GPR_ASSERT(check_bios_data_linux_test("   Google"));
  GPR_ASSERT(check_bios_data_linux_test("  Google Compute Engine  "));
  GPR_ASSERT(check_bios_data_linux_test("Google Compute Engine  "));
  GPR_ASSERT(check_bios_data_linux_test("  Google Compute Engine"));
  /* With leading and trailing \t and \n. */
  GPR_ASSERT(check_bios_data_linux_test("\t\tGoogle Compute Engine\t"));
  GPR_ASSERT(check_bios_data_linux_test("Google Compute Engine\n"));
  GPR_ASSERT(check_bios_data_linux_test("\n\n\tGoogle Compute Engine \n\t\t"));
}

static void test_gcp_environment_check_failure() {
  GPR_ASSERT(!check_bios_data_linux_test("non_existing-file"));
  GPR_ASSERT(!check_bios_data_linux_test("Google-Chrome"));
  GPR_ASSERT(!check_bios_data_linux_test("Amazon"));
  GPR_ASSERT(!check_bios_data_linux_test("Google-Chrome\t\t"));
  GPR_ASSERT(!check_bios_data_linux_test("Amazon"));
}

int main(int argc, char** argv) {
  /* Tests. */
  test_gcp_environment_check_success();
  test_gcp_environment_check_failure();
  return 0;
}

#else  // GPR_LINUX

int main(int argc, char** argv) { return 0; }

#endif  // GPR_LINUX
