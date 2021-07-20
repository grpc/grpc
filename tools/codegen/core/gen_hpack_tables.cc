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

/* generates constant tables for hpack.cc */

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <grpc/support/log.h>
#include "src/core/ext/transport/chttp2/transport/huffsyms.h"

/*
 * Huffman decoder table generation
 */

#define MAXHUFFSTATES 1024

/* represents a set of symbols as an array of booleans indicating inclusion */
typedef struct { char included[GRPC_CHTTP2_NUM_HUFFSYMS]; } symset;
/* represents a lookup table indexed by a nibble */
typedef struct { unsigned values[16]; } nibblelut;

#define NOT_SET (~(unsigned)0)

/* returns a symset that includes all possible symbols */
static symset symset_all(void) {
  symset x;
  memset(x.included, 1, sizeof(x.included));
  return x;
}

/* returns a symset that includes no symbols */
static symset symset_none(void) {
  symset x;
  memset(x.included, 0, sizeof(x.included));
  return x;
}

/* returns an empty nibblelut */
static nibblelut nibblelut_empty(void) {
  nibblelut x;
  int i;
  for (i = 0; i < 16; i++) {
    x.values[i] = NOT_SET;
  }
  return x;
}

/* counts symbols in a symset - only used for debug builds */
#ifndef NDEBUG
static int nsyms(symset s) {
  int i;
  int c = 0;
  for (i = 0; i < GRPC_CHTTP2_NUM_HUFFSYMS; i++) {
    c += s.included[i] != 0;
  }
  return c;
}
#endif

/* global table of discovered huffman decoding states */
static struct {
  /* the bit offset that this state starts at */
  unsigned bitofs;
  /* the set of symbols that this state started with */
  symset syms;

  /* lookup table for the next state */
  nibblelut next;
  /* lookup table for what to emit */
  nibblelut emit;
} huffstates[MAXHUFFSTATES];
static unsigned nhuffstates = 0;

/* given a number of decoded bits and a set of symbols that are live,
   return the index into the decoder table for this state.
   set isnew to 1 if this state was previously undiscovered */
static unsigned state_index(unsigned bitofs, symset syms, unsigned *isnew) {
  unsigned i;
  for (i = 0; i < nhuffstates; i++) {
    if (huffstates[i].bitofs != bitofs) continue;
    if (0 != memcmp(huffstates[i].syms.included, syms.included,
                    GRPC_CHTTP2_NUM_HUFFSYMS))
      continue;
    *isnew = 0;
    return i;
  }
  GPR_ASSERT(nhuffstates != MAXHUFFSTATES);
 
  i = nhuffstates;
  nhuffstates++;
  
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
static void build_dec_tbl(unsigned state, unsigned nibble, int nibbits,
                          unsigned bitofs, unsigned emit, symset syms) {
  unsigned i;
  unsigned bit;

  /* If we have four bits in the nibble we're looking at, then we can fill in
     a slot in the lookup tables. */
  if (nibbits == 4) {
    unsigned isnew;
    /* Find the state that we are in: this may be a new state, in which case
       we recurse to fill it in, or we may have already seen this state, in
       which case the recursion terminates */
    unsigned st = state_index(bitofs, syms, &isnew);
    GPR_ASSERT(huffstates[state].next.values[nibble] == NOT_SET);
    huffstates[state].next.values[nibble] = st;
    huffstates[state].emit.values[nibble] = emit;
    if (isnew) {
      build_dec_tbl(st, 0, 0, bitofs, NOT_SET, syms);
    }
    return;
  }

  assert(nsyms(syms));

  /* A bit can be 0 or 1 */
  for (bit = 0; bit < 2; bit++) {
    /* walk over active symbols and see if they have this bit set */
    symset nextsyms = symset_none();
    for (i = 0; i < GRPC_CHTTP2_NUM_HUFFSYMS; i++) {
      if (!syms.included[i]) continue; /* disregard inactive symbols */
      if (((grpc_chttp2_huffsyms[i].bits >>
            (grpc_chttp2_huffsyms[i].length - bitofs - 1)) &
           1) == bit) {
        /* the bit is set, include it in the next recursive set */
        if (grpc_chttp2_huffsyms[i].length == bitofs + 1) {
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
  next:;
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

static void generate_huff_tables(void) {
  unsigned i;
  build_dec_tbl(state_index(0, symset_all(), &i), 0, 0, 0, NOT_SET,
                symset_all());

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

static void generate_base64_huff_encoder_table(void) {
  static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int i;

  printf(
      "static const struct { gpr_uint16 bits, gpr_uint8 length } "
      "base64_syms[64] = {\n");
  for (i = 0; i < 64; i++) {
    printf("{0x%x, %d},", grpc_chttp2_huffsyms[(unsigned char)alphabet[i]].bits,
           grpc_chttp2_huffsyms[(unsigned char)alphabet[i]].length);
  }
  printf("};\n");
}

int main(void) {
  generate_huff_tables();
  generate_base64_huff_encoder_table();

  return 0;
}
