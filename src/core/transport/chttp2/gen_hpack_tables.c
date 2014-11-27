/*
 *
 * Copyright 2014, Google Inc.
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

/* generates constant tables for hpack.c */

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <grpc/support/log.h>

/*
 * first byte LUT generation
 */

typedef struct {
  const char *call;
  /* bit prefix for the field type */
  unsigned char prefix;
  /* length of the bit prefix for the field type */
  unsigned char prefix_length;
  /* index value: 0 = all zeros, 2 = all ones, 1 otherwise */
  unsigned char index;
} spec;

static const spec fields[] = {
    {"INDEXED_FIELD", 0X80, 1, 1},
    {"INDEXED_FIELD_X", 0X80, 1, 2},
    {"LITHDR_INCIDX", 0X40, 2, 1},
    {"LITHDR_INCIDX_X", 0X40, 2, 2},
    {"LITHDR_INCIDX_V", 0X40, 2, 0},
    {"LITHDR_NOTIDX", 0X00, 4, 1},
    {"LITHDR_NOTIDX_X", 0X00, 4, 2},
    {"LITHDR_NOTIDX_V", 0X00, 4, 0},
    {"LITHDR_NVRIDX", 0X10, 4, 1},
    {"LITHDR_NVRIDX_X", 0X10, 4, 2},
    {"LITHDR_NVRIDX_V", 0X10, 4, 0},
    {"MAX_TBL_SIZE", 0X20, 3, 1},
    {"MAX_TBL_SIZE_X", 0X20, 3, 2},
};

static const int num_fields = sizeof(fields) / sizeof(*fields);

static unsigned char prefix_mask(unsigned char prefix_len) {
  unsigned char i;
  unsigned char out = 0;
  for (i = 0; i < prefix_len; i++) {
    out |= 1 << (7 - i);
  }
  return out;
}

static unsigned char suffix_mask(unsigned char prefix_len) {
  return ~prefix_mask(prefix_len);
}

static void generate_first_byte_lut() {
  int i, j, n;
  const spec *chrspec;
  unsigned char suffix;

  n = printf("static CALLTYPE first_byte[256] = {");
  /* for each potential first byte of a header */
  for (i = 0; i < 256; i++) {
    /* find the field type that matches it */
    chrspec = NULL;
    for (j = 0; j < num_fields; j++) {
      if ((prefix_mask(fields[j].prefix_length) & i) == fields[j].prefix) {
        suffix = suffix_mask(fields[j].prefix_length) & i;
        if (suffix == suffix_mask(fields[j].prefix_length)) {
          if (fields[j].index != 2) continue;
        } else if (suffix == 0) {
          if (fields[j].index != 0) continue;
        } else {
          if (fields[j].index != 1) continue;
        }
        GPR_ASSERT(chrspec == NULL);
        chrspec = &fields[j];
      }
    }
    if (chrspec) {
      n += printf("%s, ", chrspec->call);
    } else {
      n += printf("ILLEGAL, ");
    }
    /* make some small effort towards readable output */
    if (n > 70) {
      printf("\n  ");
      n = 2;
    }
  }
  printf("};\n");
}

/*
 * Huffman decoder table generation
 */

#define NSYMS 257
#define MAXHUFFSTATES 1024

/* Constants pulled from the HPACK spec, and converted to C using the vim
   command:
   :%s/.*   \([0-9a-f]\+\)  \[ *\([0-9]\+\)\]/{0x\1, \2},/g */
static const struct {
  unsigned bits;
  unsigned length;
} huffsyms[NSYMS] = {
      {0x1ff8, 13},
      {0x7fffd8, 23},
      {0xfffffe2, 28},
      {0xfffffe3, 28},
      {0xfffffe4, 28},
      {0xfffffe5, 28},
      {0xfffffe6, 28},
      {0xfffffe7, 28},
      {0xfffffe8, 28},
      {0xffffea, 24},
      {0x3ffffffc, 30},
      {0xfffffe9, 28},
      {0xfffffea, 28},
      {0x3ffffffd, 30},
      {0xfffffeb, 28},
      {0xfffffec, 28},
      {0xfffffed, 28},
      {0xfffffee, 28},
      {0xfffffef, 28},
      {0xffffff0, 28},
      {0xffffff1, 28},
      {0xffffff2, 28},
      {0x3ffffffe, 30},
      {0xffffff3, 28},
      {0xffffff4, 28},
      {0xffffff5, 28},
      {0xffffff6, 28},
      {0xffffff7, 28},
      {0xffffff8, 28},
      {0xffffff9, 28},
      {0xffffffa, 28},
      {0xffffffb, 28},
      {0x14, 6},
      {0x3f8, 10},
      {0x3f9, 10},
      {0xffa, 12},
      {0x1ff9, 13},
      {0x15, 6},
      {0xf8, 8},
      {0x7fa, 11},
      {0x3fa, 10},
      {0x3fb, 10},
      {0xf9, 8},
      {0x7fb, 11},
      {0xfa, 8},
      {0x16, 6},
      {0x17, 6},
      {0x18, 6},
      {0x0, 5},
      {0x1, 5},
      {0x2, 5},
      {0x19, 6},
      {0x1a, 6},
      {0x1b, 6},
      {0x1c, 6},
      {0x1d, 6},
      {0x1e, 6},
      {0x1f, 6},
      {0x5c, 7},
      {0xfb, 8},
      {0x7ffc, 15},
      {0x20, 6},
      {0xffb, 12},
      {0x3fc, 10},
      {0x1ffa, 13},
      {0x21, 6},
      {0x5d, 7},
      {0x5e, 7},
      {0x5f, 7},
      {0x60, 7},
      {0x61, 7},
      {0x62, 7},
      {0x63, 7},
      {0x64, 7},
      {0x65, 7},
      {0x66, 7},
      {0x67, 7},
      {0x68, 7},
      {0x69, 7},
      {0x6a, 7},
      {0x6b, 7},
      {0x6c, 7},
      {0x6d, 7},
      {0x6e, 7},
      {0x6f, 7},
      {0x70, 7},
      {0x71, 7},
      {0x72, 7},
      {0xfc, 8},
      {0x73, 7},
      {0xfd, 8},
      {0x1ffb, 13},
      {0x7fff0, 19},
      {0x1ffc, 13},
      {0x3ffc, 14},
      {0x22, 6},
      {0x7ffd, 15},
      {0x3, 5},
      {0x23, 6},
      {0x4, 5},
      {0x24, 6},
      {0x5, 5},
      {0x25, 6},
      {0x26, 6},
      {0x27, 6},
      {0x6, 5},
      {0x74, 7},
      {0x75, 7},
      {0x28, 6},
      {0x29, 6},
      {0x2a, 6},
      {0x7, 5},
      {0x2b, 6},
      {0x76, 7},
      {0x2c, 6},
      {0x8, 5},
      {0x9, 5},
      {0x2d, 6},
      {0x77, 7},
      {0x78, 7},
      {0x79, 7},
      {0x7a, 7},
      {0x7b, 7},
      {0x7ffe, 15},
      {0x7fc, 11},
      {0x3ffd, 14},
      {0x1ffd, 13},
      {0xffffffc, 28},
      {0xfffe6, 20},
      {0x3fffd2, 22},
      {0xfffe7, 20},
      {0xfffe8, 20},
      {0x3fffd3, 22},
      {0x3fffd4, 22},
      {0x3fffd5, 22},
      {0x7fffd9, 23},
      {0x3fffd6, 22},
      {0x7fffda, 23},
      {0x7fffdb, 23},
      {0x7fffdc, 23},
      {0x7fffdd, 23},
      {0x7fffde, 23},
      {0xffffeb, 24},
      {0x7fffdf, 23},
      {0xffffec, 24},
      {0xffffed, 24},
      {0x3fffd7, 22},
      {0x7fffe0, 23},
      {0xffffee, 24},
      {0x7fffe1, 23},
      {0x7fffe2, 23},
      {0x7fffe3, 23},
      {0x7fffe4, 23},
      {0x1fffdc, 21},
      {0x3fffd8, 22},
      {0x7fffe5, 23},
      {0x3fffd9, 22},
      {0x7fffe6, 23},
      {0x7fffe7, 23},
      {0xffffef, 24},
      {0x3fffda, 22},
      {0x1fffdd, 21},
      {0xfffe9, 20},
      {0x3fffdb, 22},
      {0x3fffdc, 22},
      {0x7fffe8, 23},
      {0x7fffe9, 23},
      {0x1fffde, 21},
      {0x7fffea, 23},
      {0x3fffdd, 22},
      {0x3fffde, 22},
      {0xfffff0, 24},
      {0x1fffdf, 21},
      {0x3fffdf, 22},
      {0x7fffeb, 23},
      {0x7fffec, 23},
      {0x1fffe0, 21},
      {0x1fffe1, 21},
      {0x3fffe0, 22},
      {0x1fffe2, 21},
      {0x7fffed, 23},
      {0x3fffe1, 22},
      {0x7fffee, 23},
      {0x7fffef, 23},
      {0xfffea, 20},
      {0x3fffe2, 22},
      {0x3fffe3, 22},
      {0x3fffe4, 22},
      {0x7ffff0, 23},
      {0x3fffe5, 22},
      {0x3fffe6, 22},
      {0x7ffff1, 23},
      {0x3ffffe0, 26},
      {0x3ffffe1, 26},
      {0xfffeb, 20},
      {0x7fff1, 19},
      {0x3fffe7, 22},
      {0x7ffff2, 23},
      {0x3fffe8, 22},
      {0x1ffffec, 25},
      {0x3ffffe2, 26},
      {0x3ffffe3, 26},
      {0x3ffffe4, 26},
      {0x7ffffde, 27},
      {0x7ffffdf, 27},
      {0x3ffffe5, 26},
      {0xfffff1, 24},
      {0x1ffffed, 25},
      {0x7fff2, 19},
      {0x1fffe3, 21},
      {0x3ffffe6, 26},
      {0x7ffffe0, 27},
      {0x7ffffe1, 27},
      {0x3ffffe7, 26},
      {0x7ffffe2, 27},
      {0xfffff2, 24},
      {0x1fffe4, 21},
      {0x1fffe5, 21},
      {0x3ffffe8, 26},
      {0x3ffffe9, 26},
      {0xffffffd, 28},
      {0x7ffffe3, 27},
      {0x7ffffe4, 27},
      {0x7ffffe5, 27},
      {0xfffec, 20},
      {0xfffff3, 24},
      {0xfffed, 20},
      {0x1fffe6, 21},
      {0x3fffe9, 22},
      {0x1fffe7, 21},
      {0x1fffe8, 21},
      {0x7ffff3, 23},
      {0x3fffea, 22},
      {0x3fffeb, 22},
      {0x1ffffee, 25},
      {0x1ffffef, 25},
      {0xfffff4, 24},
      {0xfffff5, 24},
      {0x3ffffea, 26},
      {0x7ffff4, 23},
      {0x3ffffeb, 26},
      {0x7ffffe6, 27},
      {0x3ffffec, 26},
      {0x3ffffed, 26},
      {0x7ffffe7, 27},
      {0x7ffffe8, 27},
      {0x7ffffe9, 27},
      {0x7ffffea, 27},
      {0x7ffffeb, 27},
      {0xffffffe, 28},
      {0x7ffffec, 27},
      {0x7ffffed, 27},
      {0x7ffffee, 27},
      {0x7ffffef, 27},
      {0x7fffff0, 27},
      {0x3ffffee, 26},
      {0x3fffffff, 30},
};

/* represents a set of symbols as an array of booleans indicating inclusion */
typedef struct { char included[NSYMS]; } symset;
/* represents a lookup table indexed by a nibble */
typedef struct { int values[16]; } nibblelut;

/* returns a symset that includes all possible symbols */
static symset symset_all() {
  symset x;
  memset(x.included, 1, sizeof(x.included));
  return x;
}

/* returns a symset that includes no symbols */
static symset symset_none() {
  symset x;
  memset(x.included, 0, sizeof(x.included));
  return x;
}

/* returns an empty nibblelut */
static nibblelut nibblelut_empty() {
  nibblelut x;
  int i;
  for (i = 0; i < 16; i++) {
    x.values[i] = -1;
  }
  return x;
}

/* counts symbols in a symset - only used for debug builds */
#ifndef NDEBUG
static int nsyms(symset s) {
  int i;
  int c = 0;
  for (i = 0; i < NSYMS; i++) {
    c += s.included[i] != 0;
  }
  return c;
}
#endif

/* global table of discovered huffman decoding states */
static struct {
  /* the bit offset that this state starts at */
  int bitofs;
  /* the set of symbols that this state started with */
  symset syms;

  /* lookup table for the next state */
  nibblelut next;
  /* lookup table for what to emit */
  nibblelut emit;
} huffstates[MAXHUFFSTATES];
static int nhuffstates = 0;

/* given a number of decoded bits and a set of symbols that are live,
   return the index into the decoder table for this state.
   set isnew to 1 if this state was previously undiscovered */
static int state_index(int bitofs, symset syms, int *isnew) {
  int i;
  for (i = 0; i < nhuffstates; i++) {
    if (huffstates[i].bitofs != bitofs) continue;
    if (0 != memcmp(huffstates[i].syms.included, syms.included, NSYMS))
      continue;
    *isnew = 0;
    return i;
  }
  GPR_ASSERT(nhuffstates != MAXHUFFSTATES);
  i = nhuffstates++;
  huffstates[i].bitofs = bitofs;
  huffstates[i].syms = syms;
  huffstates[i].next = nibblelut_empty();
  huffstates[i].emit = nibblelut_empty();
  *isnew = 1;
  return i;
}

/* recursively build a decoding table

   state   - the huffman state that we are trying to fill in
   nibble  - the current nibble
   nibbits - the number of bits in the nibble that have been filled in
   bitofs  - the number of bits of symbol that have been decoded
   emit    - the symbol to emit on this nibble (or -1 if no symbol has been
             found)
   syms    - the set of symbols that could be matched */
static void build_dec_tbl(int state, int nibble, int nibbits, int bitofs,
                          int emit, symset syms) {
  int i;
  int bit;

  /* If we have four bits in the nibble we're looking at, then we can fill in
     a slot in the lookup tables. */
  if (nibbits == 4) {
    int isnew;
    /* Find the state that we are in: this may be a new state, in which case
       we recurse to fill it in, or we may have already seen this state, in
       which case the recursion terminates */
    int st = state_index(bitofs, syms, &isnew);
    GPR_ASSERT(huffstates[state].next.values[nibble] == -1);
    huffstates[state].next.values[nibble] = st;
    huffstates[state].emit.values[nibble] = emit;
    if (isnew) {
      build_dec_tbl(st, 0, 0, bitofs, -1, syms);
    }
    return;
  }

  assert(nsyms(syms));

  /* A bit can be 0 or 1 */
  for (bit = 0; bit < 2; bit++) {
    /* walk over active symbols and see if they have this bit set */
    symset nextsyms = symset_none();
    for (i = 0; i < NSYMS; i++) {
      if (!syms.included[i]) continue; /* disregard inactive symbols */
      if (((huffsyms[i].bits >> (huffsyms[i].length - bitofs - 1)) & 1) ==
          bit) {
        /* the bit is set, include it in the next recursive set */
        if (huffsyms[i].length == bitofs + 1) {
          /* additionally, we've gotten to the end of a symbol - this is a
             special recursion step: re-activate all the symbols, reset
             bitofs to zero, and recurse */
          build_dec_tbl(state, (nibble << 1) | bit, nibbits + 1, 0, i,
                        symset_all());
          /* skip the remainder of this loop */
          goto next;
        }
        nextsyms.included[i] = 1;
      }
    }
    /* recurse down for this bit */
    build_dec_tbl(state, (nibble << 1) | bit, nibbits + 1, bitofs + 1, emit,
                  nextsyms);
  next:
    ;
  }
}

static nibblelut ctbl[MAXHUFFSTATES];
static int nctbl;

static int ctbl_idx(nibblelut x) {
  int i;
  for (i = 0; i < nctbl; i++) {
    if (0 == memcmp(&x, ctbl + i, sizeof(nibblelut))) return i;
  }
  ctbl[i] = x;
  nctbl++;
  return i;
}

static void dump_ctbl(const char *name) {
  int i, j;
  printf("static const gpr_int16 %s[%d*16] = {\n", name, nctbl);
  for (i = 0; i < nctbl; i++) {
    for (j = 0; j < 16; j++) {
      printf("%d,", ctbl[i].values[j]);
    }
    printf("\n");
  }
  printf("};\n");
}

static void generate_huff_tables() {
  int i;
  build_dec_tbl(state_index(0, symset_all(), &i), 0, 0, 0, -1, symset_all());

  nctbl = 0;
  printf("static const gpr_uint8 next_tbl[%d] = {", nhuffstates);
  for (i = 0; i < nhuffstates; i++) {
    printf("%d,", ctbl_idx(huffstates[i].next));
  }
  printf("};\n");
  dump_ctbl("next_sub_tbl");

  nctbl = 0;
  printf("static const gpr_uint16 emit_tbl[%d] = {", nhuffstates);
  for (i = 0; i < nhuffstates; i++) {
    printf("%d,", ctbl_idx(huffstates[i].emit));
  }
  printf("};\n");
  dump_ctbl("emit_sub_tbl");
}

int main(void) {
  generate_huff_tables();
  generate_first_byte_lut();

  return 0;
}
