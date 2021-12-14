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

#include <grpc/support/port_platform.h>

#include "src/core/lib/gpr/murmur_hash.h"

#include <string.h>

#include "absl/base/attributes.h"

#define ROTL32(x, r) (((x) << (r)) | ((x) >> (32 - (r))))

#define FMIX32(h)    \
  (h) ^= (h) >> 16;  \
  (h) *= 0x85ebca6b; \
  (h) ^= (h) >> 13;  \
  (h) *= 0xc2b2ae35; \
  (h) ^= (h) >> 16;

uint32_t gpr_murmur_hash3(const void* key, size_t len, uint32_t seed) {
  uint32_t h1 = seed;
  uint32_t k1;

  const uint32_t c1 = 0xcc9e2d51;
  const uint32_t c2 = 0x1b873593;

  const uint8_t* keyptr = static_cast<const uint8_t*>(key);
  const size_t bsize = sizeof(k1);
  const size_t nblocks = len / bsize;

  /* body */
  for (size_t i = 0; i < nblocks; i++, keyptr += bsize) {
    memcpy(&k1, keyptr, bsize);

    k1 *= c1;
    k1 = ROTL32(k1, 15);
    k1 *= c2;

    h1 ^= k1;
    h1 = ROTL32(h1, 13);
    h1 = h1 * 5 + 0xe6546b64;
  }

  k1 = 0;

  /* tail */
  switch (len & 3) {
    case 3:
      k1 ^= (static_cast<uint32_t>(keyptr[2])) << 16;
      ABSL_FALLTHROUGH_INTENDED;
    case 2:
      k1 ^= (static_cast<uint32_t>(keyptr[1])) << 8;
      ABSL_FALLTHROUGH_INTENDED;
    case 1:
      k1 ^= keyptr[0];
      k1 *= c1;
      k1 = ROTL32(k1, 15);
      k1 *= c2;
      h1 ^= k1;
  };

  /* finalization */
  h1 ^= static_cast<uint32_t>(len);
  FMIX32(h1);
  return h1;
}
