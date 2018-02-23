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

#ifdef GPR_LINUX

#include <grpc/support/alloc.h>
#include <string.h>

#define GRPC_ALTS_PRODUCT_NAME_FILE "/sys/class/dmi/id/product_name"

static int compute_engine_detection_done = 0;
static bool is_on_compute_engine = false;

namespace grpc_core {
namespace internal {

bool check_bios_data(const char* bios_data_file) {
  char* bios_data = read_bios_file(bios_data_file);
  bool result = (!strcmp(bios_data, GRPC_ALTS_EXPECT_NAME_GOOGLE)) ||
                (!strcmp(bios_data, GRPC_ALTS_EXPECT_NAME_GCE));
  gpr_free(bios_data);
  return result;
}

}  // namespace internal
}  // namespace grpc_core

bool is_running_on_gcp() {
  if (compute_engine_detection_done) {
    return is_on_compute_engine;
  }
  compute_engine_detection_done = 1;
  bool result =
      grpc_core::internal::check_bios_data(GRPC_ALTS_PRODUCT_NAME_FILE);
  is_on_compute_engine = result;
  return result;
}

#endif  // GPR_LINUX
