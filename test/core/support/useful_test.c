/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/useful.h>
#include "test/core/util/test_config.h"

int main(int argc, char **argv) {
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
