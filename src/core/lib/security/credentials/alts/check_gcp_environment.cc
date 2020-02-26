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

#include "src/core/lib/security/credentials/alts/check_gcp_environment.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

const size_t kBiosDataBufferSize = 256;

static char* trim(const char* src) {
  if (src == nullptr || *src == '\0') {
    return nullptr;
  }
  char* des = nullptr;
  size_t start = 0, end = strlen(src) - 1;
  /* find the last character that is not a whitespace. */
  while (end != 0 && isspace(src[end])) {
    end--;
  }
  /* find the first character that is not a whitespace. */
  while (start < strlen(src) && isspace(src[start])) {
    start++;
  }
  if (start <= end) {
    des = static_cast<char*>(
        gpr_zalloc(sizeof(char) * (end - start + 2 /* '\0' */)));
    memcpy(des, src + start, end - start + 1);
  }
  return des;
}

namespace grpc_core {
namespace internal {

char* read_bios_file(const char* bios_file) {
  FILE* fp = fopen(bios_file, "r");
  if (!fp) {
    gpr_log(GPR_ERROR, "BIOS data file cannot be opened.");
    return nullptr;
  }
  char buf[kBiosDataBufferSize + 1];
  size_t ret = fread(buf, sizeof(char), kBiosDataBufferSize, fp);
  buf[ret] = '\0';
  char* trimmed_buf = trim(buf);
  fclose(fp);
  return trimmed_buf;
}

}  // namespace internal
}  // namespace grpc_core
