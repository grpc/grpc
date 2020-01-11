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

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/tsi/alts/frame_protector/alts_counter.h"
#include "test/core/tsi/alts/crypt/gsec_test_util.h"

const size_t kSmallCounterSize = 4;
const size_t kSmallOverflowSize = 1;
const size_t kGcmCounterSize = 12;
const size_t kGcmOverflowSize = 5;

static bool do_bytes_represent_client(alts_counter* ctr,
                                      unsigned char* /*counter*/, size_t size) {
  return (ctr->counter[size - 1] & 0x80) == 0x80;
}

static void alts_counter_test_input_sanity_check(size_t counter_size,
                                                 size_t overflow_size) {
  alts_counter* ctr = nullptr;
  char* error_details = nullptr;

  /* Input sanity check on alts_counter_create(). */
  /* Invalid counter size. */
  grpc_status_code status =
      alts_counter_create(true, 0, overflow_size, &ctr, &error_details);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INVALID_ARGUMENT, error_details,
      "counter_size is invalid."));
  gpr_free(error_details);

  /* Invalid overflow size. */
  status = alts_counter_create(true, counter_size, 0, &ctr, &error_details);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INVALID_ARGUMENT, error_details,
      "overflow_size is invalid."));
  gpr_free(error_details);

  /* alts_counter is nullptr. */
  status = alts_counter_create(true, counter_size, overflow_size, nullptr,
                               &error_details);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INVALID_ARGUMENT, error_details,
      "crypter_counter is nullptr."));
  gpr_free(error_details);

  status = alts_counter_create(true, counter_size, overflow_size, &ctr,
                               &error_details);
  GPR_ASSERT(status == GRPC_STATUS_OK);

  /* Input sanity check on alts_counter_increment(). */
  /* crypter_counter is nullptr. */
  bool is_overflow = false;
  status = alts_counter_increment(nullptr, &is_overflow, &error_details);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INVALID_ARGUMENT, error_details,
      "crypter_counter is nullptr."));
  gpr_free(error_details);
  /* is_overflow is nullptr. */
  status = alts_counter_increment(ctr, nullptr, &error_details);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INVALID_ARGUMENT, error_details,
      "is_overflow is nullptr."));
  gpr_free(error_details);
  alts_counter_destroy(ctr);
}

static void alts_counter_test_overflow_full_range(bool is_client,
                                                  size_t counter_size,
                                                  size_t overflow_size) {
  alts_counter* ctr = nullptr;
  char* error_details = nullptr;
  grpc_status_code status = alts_counter_create(
      is_client, counter_size, overflow_size, &ctr, &error_details);
  GPR_ASSERT(status == GRPC_STATUS_OK);
  unsigned char* expected =
      static_cast<unsigned char*>(gpr_zalloc(counter_size));
  if (is_client) {
    expected[counter_size - 1] = 0x80;
  }
  /* Do a single iteration to ensure the counter is initialized as expected. */
  GPR_ASSERT(do_bytes_represent_client(ctr, alts_counter_get_counter(ctr),
                                       counter_size) == is_client);
  GPR_ASSERT(memcmp(alts_counter_get_counter(ctr), expected, counter_size) ==
             0);
  bool is_overflow = false;
  GPR_ASSERT(alts_counter_increment(ctr, &is_overflow, &error_details) ==
             GRPC_STATUS_OK);
  GPR_ASSERT(!is_overflow);
  /**
   * The counter can return 2^{overflow_size * 8} counters. The
   * high-order bit is fixed to the client/server. The last call will yield a
   * useable counter, but overflow the counter object.
   */
  int iterations = 1 << (overflow_size * 8);
  int ind = 1;
  for (ind = 1; ind < iterations - 1; ind++) {
    GPR_ASSERT(do_bytes_represent_client(ctr, alts_counter_get_counter(ctr),
                                         counter_size) == is_client);
    GPR_ASSERT(alts_counter_increment(ctr, &is_overflow, &error_details) ==
               GRPC_STATUS_OK);
    GPR_ASSERT(!is_overflow);
  }
  GPR_ASSERT(do_bytes_represent_client(ctr, alts_counter_get_counter(ctr),
                                       counter_size) == is_client);
  GPR_ASSERT(alts_counter_increment(ctr, &is_overflow, &error_details) ==
             GRPC_STATUS_FAILED_PRECONDITION);
  GPR_ASSERT(is_overflow);
  gpr_free(expected);
  alts_counter_destroy(ctr);
}

/* Set the counter manually and make sure it overflows as expected. */
static void alts_counter_test_overflow_single_increment(bool is_client,
                                                        size_t counter_size,
                                                        size_t overflow_size) {
  alts_counter* ctr = nullptr;
  char* error_details = nullptr;
  grpc_status_code status = alts_counter_create(
      is_client, counter_size, overflow_size, &ctr, &error_details);
  GPR_ASSERT(status == GRPC_STATUS_OK);
  unsigned char* expected =
      static_cast<unsigned char*>(gpr_zalloc(counter_size));
  memset(expected, 0xFF, overflow_size);
  expected[0] = 0xFE;

  if (is_client) {
    expected[counter_size - 1] = 0x80;
  }
  memcpy(ctr->counter, expected, counter_size);
  GPR_ASSERT(do_bytes_represent_client(ctr, alts_counter_get_counter(ctr),
                                       counter_size) == is_client);
  GPR_ASSERT(memcmp(expected, alts_counter_get_counter(ctr), counter_size) ==
             0);
  bool is_overflow = false;
  GPR_ASSERT(alts_counter_increment(ctr, &is_overflow, &error_details) ==
             GRPC_STATUS_OK);
  GPR_ASSERT(!is_overflow);
  GPR_ASSERT(do_bytes_represent_client(ctr, alts_counter_get_counter(ctr),
                                       counter_size) == is_client);
  expected[0] = static_cast<unsigned char>(expected[0] + 1);
  GPR_ASSERT(memcmp(expected, alts_counter_get_counter(ctr), counter_size) ==
             0);
  GPR_ASSERT(alts_counter_increment(ctr, &is_overflow, &error_details) ==
             GRPC_STATUS_FAILED_PRECONDITION);
  GPR_ASSERT(is_overflow);
  gpr_free(expected);
  alts_counter_destroy(ctr);
}

int main(int /*argc*/, char** /*argv*/) {
  alts_counter_test_input_sanity_check(kGcmCounterSize, kGcmOverflowSize);
  alts_counter_test_overflow_full_range(true, kSmallCounterSize,
                                        kSmallOverflowSize);
  alts_counter_test_overflow_full_range(false, kSmallCounterSize,
                                        kSmallOverflowSize);
  alts_counter_test_overflow_single_increment(true, kGcmCounterSize,
                                              kGcmOverflowSize);
  alts_counter_test_overflow_single_increment(false, kGcmCounterSize,
                                              kGcmOverflowSize);

  return 0;
}
