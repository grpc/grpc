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

/* generates constant table for metadata.c */

#include <stdio.h>
#include <string.h>

static unsigned char legal_bits[256 / 8];

static void legal(int x) {
  int byte = x / 8;
  int bit = x % 8;
  /* NB: the following integer arithmetic operation needs to be in its
   * expanded form due to the "integral promotion" performed (see section
   * 3.2.1.1 of the C89 draft standard). A cast to the smaller container type
   * is then required to avoid the compiler warning */
  legal_bits[byte] =
      (unsigned char)((legal_bits[byte] | (unsigned char)(1 << bit)));
}

static void dump(void) {
  int i;

  printf("static const uint8_t legal_header_bits[256/8] = ");
  for (i = 0; i < 256 / 8; i++)
    printf("%c 0x%02x", i ? ',' : '{', legal_bits[i]);
  printf(" };\n");
}

static void clear(void) { memset(legal_bits, 0, sizeof(legal_bits)); }

int main(void) {
  int i;

  clear();
  for (i = 'a'; i <= 'z'; i++) legal(i);
  for (i = '0'; i <= '9'; i++) legal(i);
  legal('-');
  legal('_');
  legal('.');
  dump();

  clear();
  for (i = 32; i <= 126; i++) {
    legal(i);
  }
  dump();

  return 0;
}
