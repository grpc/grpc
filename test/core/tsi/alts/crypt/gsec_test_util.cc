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

#include "test/core/tsi/alts/crypt/gsec_test_util.h"

#include <time.h>

#include <grpc/support/alloc.h>

void gsec_test_random_bytes(uint8_t* bytes, size_t length) {
  srand(time(nullptr));
  size_t ind;
  for (ind = 0; ind < length; ind++) {
    bytes[ind] = static_cast<uint8_t>(rand() % 255 + 1);
  }
}

void gsec_test_random_array(uint8_t** bytes, size_t length) {
  if (bytes != nullptr) {
    *bytes = static_cast<uint8_t*>(gpr_malloc(length));
    gsec_test_random_bytes(*bytes, length);
  } else {
    fprintf(stderr, "bytes buffer is nullptr in gsec_test_random_array().");
    abort();
  }
}

uint32_t gsec_test_bias_random_uint32(uint32_t max_length) {
  uint32_t value;
  gsec_test_random_bytes(reinterpret_cast<uint8_t*>(&value), sizeof(value));
  return value % max_length;
}

void gsec_test_copy(const uint8_t* src, uint8_t** des, size_t source_len) {
  if (src != nullptr && des != nullptr) {
    *des = static_cast<uint8_t*>(gpr_malloc(source_len));
    memcpy(*des, src, source_len);
  } else {
    fprintf(stderr, "Either src or des buffer is nullptr in gsec_test_copy().");
    abort();
  }
}

void gsec_test_copy_and_alter_random_byte(const uint8_t* src, uint8_t** des,
                                          size_t source_len) {
  if (src != nullptr && des != nullptr) {
    *des = static_cast<uint8_t*>(gpr_malloc(source_len));
    memcpy(*des, src, source_len);
    uint32_t offset;
    offset = gsec_test_bias_random_uint32(static_cast<uint32_t>(source_len));
    (*(*des + offset))++;
  } else {
    fprintf(stderr,
            "Either src or des is nullptr in "
            "gsec_test_copy_and_alter_random_byte().");
    abort();
  }
}

int gsec_test_expect_compare_code_and_substr(grpc_status_code status1,
                                             grpc_status_code status2,
                                             const char* msg1,
                                             const char* msg2) {
  int failure = 1;
  if (status1 != status2) {
    fprintf(stderr, "Status %d does not equal %d.\n", status1, status2);
    failure = 0;
  }
  if (strstr(msg1, msg2) == nullptr) {
    fprintf(stderr, "Status message <%s> does not contain <%s>.\n", msg1, msg2);
    failure = 0;
  }
  return failure;
}
