/*
 *
 * Copyright 2015 gRPC authors.
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

#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/useful.h>
#include "test/core/util/test_config.h"

int main(int argc, char** argv) {
  int four[4];
  int five[5];
  uint32_t bitset = 0;
  grpc_test_init(argc, argv);

  GPR_ASSERT(GPR_MIN(1, 2) == 1);
  GPR_ASSERT(GPR_MAX(1, 2) == 2);
  GPR_ASSERT(GPR_MIN(2, 1) == 1);
  GPR_ASSERT(GPR_MAX(2, 1) == 2);
  GPR_ASSERT(GPR_CLAMP(1, 0, 2) == 1);
  GPR_ASSERT(GPR_CLAMP(0, 0, 2) == 0);
  GPR_ASSERT(GPR_CLAMP(2, 0, 2) == 2);
  GPR_ASSERT(GPR_CLAMP(-1, 0, 2) == 0);
  GPR_ASSERT(GPR_CLAMP(3, 0, 2) == 2);
  GPR_ASSERT(GPR_ROTL((uint32_t)0x80000001, 1) == 3);
  GPR_ASSERT(GPR_ROTR((uint32_t)0x80000001, 1) == 0xc0000000);
  GPR_ASSERT(GPR_ARRAY_SIZE(four) == 4);
  GPR_ASSERT(GPR_ARRAY_SIZE(five) == 5);

  GPR_ASSERT(GPR_BITCOUNT((1u << 31) - 1) == 31);
  GPR_ASSERT(GPR_BITCOUNT(1u << 3) == 1);
  GPR_ASSERT(GPR_BITCOUNT(0) == 0);

  GPR_ASSERT(GPR_BITSET(&bitset, 3) == 8);
  GPR_ASSERT(GPR_BITCOUNT(bitset) == 1);
  GPR_ASSERT(GPR_BITGET(bitset, 3) == 1);
  GPR_ASSERT(GPR_BITSET(&bitset, 1) == 10);
  GPR_ASSERT(GPR_BITCOUNT(bitset) == 2);
  GPR_ASSERT(GPR_BITCLEAR(&bitset, 3) == 2);
  GPR_ASSERT(GPR_BITCOUNT(bitset) == 1);
  GPR_ASSERT(GPR_BITGET(bitset, 3) == 0);

  return 0;
}
