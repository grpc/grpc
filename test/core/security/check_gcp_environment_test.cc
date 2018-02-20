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

#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/security/credentials/alts/check_gcp_environment.h"

static char* create_data_file(const char* data) {
  char* filename = nullptr;
  FILE* fp = gpr_tmpfile("alts_credentials_test", &filename);
  GPR_ASSERT(filename != nullptr);
  GPR_ASSERT(fp != nullptr);
  GPR_ASSERT(fwrite(data, 1, strlen(data), fp) == strlen(data));
  fclose(fp);
  return filename;
}

static bool check_bios_data_test(const char* data, bool is_linux) {
  char* filename = create_data_file(data);
  bool result = check_bios_data(filename, is_linux);
  /* Cleanup. */
  remove(filename);
  gpr_free(filename);
  return result;
}

static void test_gcp_environment_check_success() {
  /* Exact match. */
  GPR_ASSERT(check_bios_data_test("Google", true /* is_linux */));
  GPR_ASSERT(check_bios_data_test("Google Compute Engine", true));
  /* With leading and trailing whitespaces. */
  GPR_ASSERT(check_bios_data_test(" Google  ", true));
  GPR_ASSERT(check_bios_data_test("Google  ", true));
  GPR_ASSERT(check_bios_data_test("   Google", true));
  GPR_ASSERT(check_bios_data_test("  Google Compute Engine  ", true));
  GPR_ASSERT(check_bios_data_test("Google Compute Engine  ", true));
  GPR_ASSERT(check_bios_data_test("  Google Compute Engine", true));
  /* With leading and trailing \t and \n. */
  GPR_ASSERT(check_bios_data_test("\t\tGoogle Compute Engine\t", true));
  GPR_ASSERT(check_bios_data_test("Google Compute Engine\n", true));
  GPR_ASSERT(check_bios_data_test("\n\n\tGoogle Compute Engine \n\t\t", true));
  GPR_ASSERT(check_bios_data_test("Google", false /* is_linux */));
  GPR_ASSERT(check_bios_data_test("Google\n", false));
  GPR_ASSERT(check_bios_data_test("Google\r", false));
  GPR_ASSERT(check_bios_data_test("Google\r\n", false));
  GPR_ASSERT(check_bios_data_test("   Google   \r\n", false));
  GPR_ASSERT(check_bios_data_test(" \t\t Google\r\n", false));
  GPR_ASSERT(check_bios_data_test(" \t\t Google\t\t  \r\n", false));
}

static void test_gcp_environment_check_failure() {
  GPR_ASSERT(!check_bios_data_test("non_existing-file", true /* is_linux */));
  GPR_ASSERT(!check_bios_data_test("Google-Chrome", true));
  GPR_ASSERT(!check_bios_data_test("Amazon", true));
  GPR_ASSERT(!check_bios_data_test("Google-Chrome\t\t", true));
  GPR_ASSERT(!check_bios_data_test("Amazon", true));
  GPR_ASSERT(!check_bios_data_test("\t\tAmazon\n", false /* is_linux */));
  GPR_ASSERT(!check_bios_data_test("  Amazon\r\n", false));
}

int main(int argc, char** argv) {
  /* Tests. */
  test_gcp_environment_check_success();
  test_gcp_environment_check_failure();
  return 0;
}
