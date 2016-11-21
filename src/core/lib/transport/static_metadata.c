/*
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
 */

/*
 * WARNING: Auto-generated code.
 *
 * To make changes to this file, change
 * tools/codegen/core/gen_static_metadata.py, and then re-run it.
 *
 * See metadata.h for an explanation of the interface here, and metadata.c for
 * an explanation of what's going on.
 */

#include "src/core/lib/transport/static_metadata.h"

#include "src/core/lib/slice/slice_internal.h"

static uint8_t g_bytes[] = {
    48,  49,  50,  50,  48,  48,  50,  48,  52,  50,  48,  54,  51,  48,  52,
    52,  48,  48,  52,  48,  52,  53,  48,  48,  97,  99,  99,  101, 112, 116,
    97,  99,  99,  101, 112, 116, 45,  99,  104, 97,  114, 115, 101, 116, 97,
    99,  99,  101, 112, 116, 45,  101, 110, 99,  111, 100, 105, 110, 103, 97,
    99,  99,  101, 112, 116, 45,  108, 97,  110, 103, 117, 97,  103, 101, 97,
    99,  99,  101, 112, 116, 45,  114, 97,  110, 103, 101, 115, 97,  99,  99,
    101, 115, 115, 45,  99,  111, 110, 116, 114, 111, 108, 45,  97,  108, 108,
    111, 119, 45,  111, 114, 105, 103, 105, 110, 97,  103, 101, 97,  108, 108,
    111, 119, 97,  112, 112, 108, 105, 99,  97,  116, 105, 111, 110, 47,  103,
    114, 112, 99,  58,  97,  117, 116, 104, 111, 114, 105, 116, 121, 97,  117,
    116, 104, 111, 114, 105, 122, 97,  116, 105, 111, 110, 99,  97,  99,  104,
    101, 45,  99,  111, 110, 116, 114, 111, 108, 99,  111, 110, 116, 101, 110,
    116, 45,  100, 105, 115, 112, 111, 115, 105, 116, 105, 111, 110, 99,  111,
    110, 116, 101, 110, 116, 45,  101, 110, 99,  111, 100, 105, 110, 103, 99,
    111, 110, 116, 101, 110, 116, 45,  108, 97,  110, 103, 117, 97,  103, 101,
    99,  111, 110, 116, 101, 110, 116, 45,  108, 101, 110, 103, 116, 104, 99,
    111, 110, 116, 101, 110, 116, 45,  108, 111, 99,  97,  116, 105, 111, 110,
    99,  111, 110, 116, 101, 110, 116, 45,  114, 97,  110, 103, 101, 99,  111,
    110, 116, 101, 110, 116, 45,  116, 121, 112, 101, 99,  111, 111, 107, 105,
    101, 100, 97,  116, 101, 100, 101, 102, 108, 97,  116, 101, 100, 101, 102,
    108, 97,  116, 101, 44,  103, 122, 105, 112, 101, 116, 97,  103, 101, 120,
    112, 101, 99,  116, 101, 120, 112, 105, 114, 101, 115, 102, 114, 111, 109,
    71,  69,  84,  103, 114, 112, 99,  103, 114, 112, 99,  45,  97,  99,  99,
    101, 112, 116, 45,  101, 110, 99,  111, 100, 105, 110, 103, 103, 114, 112,
    99,  46,  109, 97,  120, 95,  114, 101, 113, 117, 101, 115, 116, 95,  109,
    101, 115, 115, 97,  103, 101, 95,  98,  121, 116, 101, 115, 103, 114, 112,
    99,  46,  109, 97,  120, 95,  114, 101, 115, 112, 111, 110, 115, 101, 95,
    109, 101, 115, 115, 97,  103, 101, 95,  98,  121, 116, 101, 115, 103, 114,
    112, 99,  46,  116, 105, 109, 101, 111, 117, 116, 103, 114, 112, 99,  46,
    119, 97,  105, 116, 95,  102, 111, 114, 95,  114, 101, 97,  100, 121, 103,
    114, 112, 99,  45,  101, 110, 99,  111, 100, 105, 110, 103, 103, 114, 112,
    99,  45,  105, 110, 116, 101, 114, 110, 97,  108, 45,  101, 110, 99,  111,
    100, 105, 110, 103, 45,  114, 101, 113, 117, 101, 115, 116, 103, 114, 112,
    99,  45,  109, 101, 115, 115, 97,  103, 101, 103, 114, 112, 99,  45,  112,
    97,  121, 108, 111, 97,  100, 45,  98,  105, 110, 103, 114, 112, 99,  45,
    115, 116, 97,  116, 115, 45,  98,  105, 110, 103, 114, 112, 99,  45,  115,
    116, 97,  116, 117, 115, 103, 114, 112, 99,  45,  116, 105, 109, 101, 111,
    117, 116, 103, 114, 112, 99,  45,  116, 114, 97,  99,  105, 110, 103, 45,
    98,  105, 110, 103, 122, 105, 112, 103, 122, 105, 112, 44,  32,  100, 101,
    102, 108, 97,  116, 101, 104, 111, 115, 116, 104, 116, 116, 112, 104, 116,
    116, 112, 115, 105, 100, 101, 110, 116, 105, 116, 121, 105, 100, 101, 110,
    116, 105, 116, 121, 44,  100, 101, 102, 108, 97,  116, 101, 105, 100, 101,
    110, 116, 105, 116, 121, 44,  100, 101, 102, 108, 97,  116, 101, 44,  103,
    122, 105, 112, 105, 100, 101, 110, 116, 105, 116, 121, 44,  103, 122, 105,
    112, 105, 102, 45,  109, 97,  116, 99,  104, 105, 102, 45,  109, 111, 100,
    105, 102, 105, 101, 100, 45,  115, 105, 110, 99,  101, 105, 102, 45,  110,
    111, 110, 101, 45,  109, 97,  116, 99,  104, 105, 102, 45,  114, 97,  110,
    103, 101, 105, 102, 45,  117, 110, 109, 111, 100, 105, 102, 105, 101, 100,
    45,  115, 105, 110, 99,  101, 108, 97,  115, 116, 45,  109, 111, 100, 105,
    102, 105, 101, 100, 108, 98,  45,  99,  111, 115, 116, 45,  98,  105, 110,
    108, 98,  45,  116, 111, 107, 101, 110, 108, 105, 110, 107, 108, 111, 99,
    97,  116, 105, 111, 110, 109, 97,  120, 45,  102, 111, 114, 119, 97,  114,
    100, 115, 58,  109, 101, 116, 104, 111, 100, 58,  112, 97,  116, 104, 80,
    79,  83,  84,  112, 114, 111, 120, 121, 45,  97,  117, 116, 104, 101, 110,
    116, 105, 99,  97,  116, 101, 112, 114, 111, 120, 121, 45,  97,  117, 116,
    104, 111, 114, 105, 122, 97,  116, 105, 111, 110, 80,  85,  84,  114, 97,
    110, 103, 101, 114, 101, 102, 101, 114, 101, 114, 114, 101, 102, 114, 101,
    115, 104, 114, 101, 116, 114, 121, 45,  97,  102, 116, 101, 114, 58,  115,
    99,  104, 101, 109, 101, 115, 101, 114, 118, 101, 114, 115, 101, 116, 45,
    99,  111, 111, 107, 105, 101, 47,  47,  103, 114, 112, 99,  46,  108, 98,
    46,  118, 49,  46,  76,  111, 97,  100, 66,  97,  108, 97,  110, 99,  101,
    114, 47,  66,  97,  108, 97,  110, 99,  101, 76,  111, 97,  100, 47,  105,
    110, 100, 101, 120, 46,  104, 116, 109, 108, 58,  115, 116, 97,  116, 117,
    115, 115, 116, 114, 105, 99,  116, 45,  116, 114, 97,  110, 115, 112, 111,
    114, 116, 45,  115, 101, 99,  117, 114, 105, 116, 121, 116, 101, 116, 114,
    97,  105, 108, 101, 114, 115, 116, 114, 97,  110, 115, 102, 101, 114, 45,
    101, 110, 99,  111, 100, 105, 110, 103, 117, 115, 101, 114, 45,  97,  103,
    101, 110, 116, 118, 97,  114, 121, 118, 105, 97,  119, 119, 119, 45,  97,
    117, 116, 104, 101, 110, 116, 105, 99,  97,  116, 101};

static void static_ref(void *unused) {}
static void static_unref(grpc_exec_ctx *exec_ctx, void *unused) {}
static const grpc_slice_refcount_vtable static_vtable = {
    static_ref, static_unref, grpc_static_slice_eq, grpc_static_slice_hash};
typedef struct {
  grpc_slice_refcount base;
  const uint16_t offset;
  const uint16_t length;
} static_slice_refcount;
static static_slice_refcount g_refcnts[GRPC_STATIC_MDSTR_COUNT] = {
    {{&static_vtable}, 0, 1},    {{&static_vtable}, 1, 1},
    {{&static_vtable}, 2, 1},    {{&static_vtable}, 3, 3},
    {{&static_vtable}, 6, 3},    {{&static_vtable}, 9, 3},
    {{&static_vtable}, 12, 3},   {{&static_vtable}, 15, 3},
    {{&static_vtable}, 18, 3},   {{&static_vtable}, 21, 3},
    {{&static_vtable}, 24, 6},   {{&static_vtable}, 30, 14},
    {{&static_vtable}, 44, 15},  {{&static_vtable}, 59, 15},
    {{&static_vtable}, 74, 13},  {{&static_vtable}, 87, 27},
    {{&static_vtable}, 114, 3},  {{&static_vtable}, 117, 5},
    {{&static_vtable}, 122, 16}, {{&static_vtable}, 138, 10},
    {{&static_vtable}, 148, 13}, {{&static_vtable}, 161, 13},
    {{&static_vtable}, 174, 19}, {{&static_vtable}, 193, 16},
    {{&static_vtable}, 209, 16}, {{&static_vtable}, 225, 14},
    {{&static_vtable}, 239, 16}, {{&static_vtable}, 255, 13},
    {{&static_vtable}, 268, 12}, {{&static_vtable}, 280, 6},
    {{&static_vtable}, 286, 4},  {{&static_vtable}, 290, 7},
    {{&static_vtable}, 297, 12}, {{&static_vtable}, 309, 0},
    {{&static_vtable}, 309, 4},  {{&static_vtable}, 313, 6},
    {{&static_vtable}, 319, 7},  {{&static_vtable}, 326, 4},
    {{&static_vtable}, 330, 3},  {{&static_vtable}, 333, 4},
    {{&static_vtable}, 337, 20}, {{&static_vtable}, 357, 30},
    {{&static_vtable}, 387, 31}, {{&static_vtable}, 418, 12},
    {{&static_vtable}, 430, 19}, {{&static_vtable}, 449, 13},
    {{&static_vtable}, 462, 30}, {{&static_vtable}, 492, 12},
    {{&static_vtable}, 504, 16}, {{&static_vtable}, 520, 14},
    {{&static_vtable}, 534, 11}, {{&static_vtable}, 545, 12},
    {{&static_vtable}, 557, 16}, {{&static_vtable}, 573, 4},
    {{&static_vtable}, 577, 13}, {{&static_vtable}, 590, 4},
    {{&static_vtable}, 594, 4},  {{&static_vtable}, 598, 5},
    {{&static_vtable}, 603, 8},  {{&static_vtable}, 611, 16},
    {{&static_vtable}, 627, 21}, {{&static_vtable}, 648, 13},
    {{&static_vtable}, 661, 8},  {{&static_vtable}, 669, 17},
    {{&static_vtable}, 686, 13}, {{&static_vtable}, 699, 8},
    {{&static_vtable}, 707, 19}, {{&static_vtable}, 726, 13},
    {{&static_vtable}, 739, 11}, {{&static_vtable}, 750, 8},
    {{&static_vtable}, 758, 4},  {{&static_vtable}, 762, 8},
    {{&static_vtable}, 770, 12}, {{&static_vtable}, 782, 7},
    {{&static_vtable}, 789, 5},  {{&static_vtable}, 794, 4},
    {{&static_vtable}, 798, 18}, {{&static_vtable}, 816, 19},
    {{&static_vtable}, 835, 3},  {{&static_vtable}, 838, 5},
    {{&static_vtable}, 843, 7},  {{&static_vtable}, 850, 7},
    {{&static_vtable}, 857, 11}, {{&static_vtable}, 868, 7},
    {{&static_vtable}, 875, 6},  {{&static_vtable}, 881, 10},
    {{&static_vtable}, 891, 1},  {{&static_vtable}, 892, 36},
    {{&static_vtable}, 928, 11}, {{&static_vtable}, 939, 7},
    {{&static_vtable}, 946, 25}, {{&static_vtable}, 971, 2},
    {{&static_vtable}, 973, 8},  {{&static_vtable}, 981, 17},
    {{&static_vtable}, 998, 10}, {{&static_vtable}, 1008, 4},
    {{&static_vtable}, 1012, 3}, {{&static_vtable}, 1015, 16},
};

bool grpc_is_static_metadata_string(grpc_slice slice) {
  return slice.refcount != NULL && slice.refcount->vtable == &static_vtable;
}

const grpc_slice grpc_static_slice_table[GRPC_STATIC_MDSTR_COUNT] = {
    {.refcount = &g_refcnts[0].base, .data.refcounted = {g_bytes + 0, 1}},
    {.refcount = &g_refcnts[1].base, .data.refcounted = {g_bytes + 1, 1}},
    {.refcount = &g_refcnts[2].base, .data.refcounted = {g_bytes + 2, 1}},
    {.refcount = &g_refcnts[3].base, .data.refcounted = {g_bytes + 3, 3}},
    {.refcount = &g_refcnts[4].base, .data.refcounted = {g_bytes + 6, 3}},
    {.refcount = &g_refcnts[5].base, .data.refcounted = {g_bytes + 9, 3}},
    {.refcount = &g_refcnts[6].base, .data.refcounted = {g_bytes + 12, 3}},
    {.refcount = &g_refcnts[7].base, .data.refcounted = {g_bytes + 15, 3}},
    {.refcount = &g_refcnts[8].base, .data.refcounted = {g_bytes + 18, 3}},
    {.refcount = &g_refcnts[9].base, .data.refcounted = {g_bytes + 21, 3}},
    {.refcount = &g_refcnts[10].base, .data.refcounted = {g_bytes + 24, 6}},
    {.refcount = &g_refcnts[11].base, .data.refcounted = {g_bytes + 30, 14}},
    {.refcount = &g_refcnts[12].base, .data.refcounted = {g_bytes + 44, 15}},
    {.refcount = &g_refcnts[13].base, .data.refcounted = {g_bytes + 59, 15}},
    {.refcount = &g_refcnts[14].base, .data.refcounted = {g_bytes + 74, 13}},
    {.refcount = &g_refcnts[15].base, .data.refcounted = {g_bytes + 87, 27}},
    {.refcount = &g_refcnts[16].base, .data.refcounted = {g_bytes + 114, 3}},
    {.refcount = &g_refcnts[17].base, .data.refcounted = {g_bytes + 117, 5}},
    {.refcount = &g_refcnts[18].base, .data.refcounted = {g_bytes + 122, 16}},
    {.refcount = &g_refcnts[19].base, .data.refcounted = {g_bytes + 138, 10}},
    {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 148, 13}},
    {.refcount = &g_refcnts[21].base, .data.refcounted = {g_bytes + 161, 13}},
    {.refcount = &g_refcnts[22].base, .data.refcounted = {g_bytes + 174, 19}},
    {.refcount = &g_refcnts[23].base, .data.refcounted = {g_bytes + 193, 16}},
    {.refcount = &g_refcnts[24].base, .data.refcounted = {g_bytes + 209, 16}},
    {.refcount = &g_refcnts[25].base, .data.refcounted = {g_bytes + 225, 14}},
    {.refcount = &g_refcnts[26].base, .data.refcounted = {g_bytes + 239, 16}},
    {.refcount = &g_refcnts[27].base, .data.refcounted = {g_bytes + 255, 13}},
    {.refcount = &g_refcnts[28].base, .data.refcounted = {g_bytes + 268, 12}},
    {.refcount = &g_refcnts[29].base, .data.refcounted = {g_bytes + 280, 6}},
    {.refcount = &g_refcnts[30].base, .data.refcounted = {g_bytes + 286, 4}},
    {.refcount = &g_refcnts[31].base, .data.refcounted = {g_bytes + 290, 7}},
    {.refcount = &g_refcnts[32].base, .data.refcounted = {g_bytes + 297, 12}},
    {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}},
    {.refcount = &g_refcnts[34].base, .data.refcounted = {g_bytes + 309, 4}},
    {.refcount = &g_refcnts[35].base, .data.refcounted = {g_bytes + 313, 6}},
    {.refcount = &g_refcnts[36].base, .data.refcounted = {g_bytes + 319, 7}},
    {.refcount = &g_refcnts[37].base, .data.refcounted = {g_bytes + 326, 4}},
    {.refcount = &g_refcnts[38].base, .data.refcounted = {g_bytes + 330, 3}},
    {.refcount = &g_refcnts[39].base, .data.refcounted = {g_bytes + 333, 4}},
    {.refcount = &g_refcnts[40].base, .data.refcounted = {g_bytes + 337, 20}},
    {.refcount = &g_refcnts[41].base, .data.refcounted = {g_bytes + 357, 30}},
    {.refcount = &g_refcnts[42].base, .data.refcounted = {g_bytes + 387, 31}},
    {.refcount = &g_refcnts[43].base, .data.refcounted = {g_bytes + 418, 12}},
    {.refcount = &g_refcnts[44].base, .data.refcounted = {g_bytes + 430, 19}},
    {.refcount = &g_refcnts[45].base, .data.refcounted = {g_bytes + 449, 13}},
    {.refcount = &g_refcnts[46].base, .data.refcounted = {g_bytes + 462, 30}},
    {.refcount = &g_refcnts[47].base, .data.refcounted = {g_bytes + 492, 12}},
    {.refcount = &g_refcnts[48].base, .data.refcounted = {g_bytes + 504, 16}},
    {.refcount = &g_refcnts[49].base, .data.refcounted = {g_bytes + 520, 14}},
    {.refcount = &g_refcnts[50].base, .data.refcounted = {g_bytes + 534, 11}},
    {.refcount = &g_refcnts[51].base, .data.refcounted = {g_bytes + 545, 12}},
    {.refcount = &g_refcnts[52].base, .data.refcounted = {g_bytes + 557, 16}},
    {.refcount = &g_refcnts[53].base, .data.refcounted = {g_bytes + 573, 4}},
    {.refcount = &g_refcnts[54].base, .data.refcounted = {g_bytes + 577, 13}},
    {.refcount = &g_refcnts[55].base, .data.refcounted = {g_bytes + 590, 4}},
    {.refcount = &g_refcnts[56].base, .data.refcounted = {g_bytes + 594, 4}},
    {.refcount = &g_refcnts[57].base, .data.refcounted = {g_bytes + 598, 5}},
    {.refcount = &g_refcnts[58].base, .data.refcounted = {g_bytes + 603, 8}},
    {.refcount = &g_refcnts[59].base, .data.refcounted = {g_bytes + 611, 16}},
    {.refcount = &g_refcnts[60].base, .data.refcounted = {g_bytes + 627, 21}},
    {.refcount = &g_refcnts[61].base, .data.refcounted = {g_bytes + 648, 13}},
    {.refcount = &g_refcnts[62].base, .data.refcounted = {g_bytes + 661, 8}},
    {.refcount = &g_refcnts[63].base, .data.refcounted = {g_bytes + 669, 17}},
    {.refcount = &g_refcnts[64].base, .data.refcounted = {g_bytes + 686, 13}},
    {.refcount = &g_refcnts[65].base, .data.refcounted = {g_bytes + 699, 8}},
    {.refcount = &g_refcnts[66].base, .data.refcounted = {g_bytes + 707, 19}},
    {.refcount = &g_refcnts[67].base, .data.refcounted = {g_bytes + 726, 13}},
    {.refcount = &g_refcnts[68].base, .data.refcounted = {g_bytes + 739, 11}},
    {.refcount = &g_refcnts[69].base, .data.refcounted = {g_bytes + 750, 8}},
    {.refcount = &g_refcnts[70].base, .data.refcounted = {g_bytes + 758, 4}},
    {.refcount = &g_refcnts[71].base, .data.refcounted = {g_bytes + 762, 8}},
    {.refcount = &g_refcnts[72].base, .data.refcounted = {g_bytes + 770, 12}},
    {.refcount = &g_refcnts[73].base, .data.refcounted = {g_bytes + 782, 7}},
    {.refcount = &g_refcnts[74].base, .data.refcounted = {g_bytes + 789, 5}},
    {.refcount = &g_refcnts[75].base, .data.refcounted = {g_bytes + 794, 4}},
    {.refcount = &g_refcnts[76].base, .data.refcounted = {g_bytes + 798, 18}},
    {.refcount = &g_refcnts[77].base, .data.refcounted = {g_bytes + 816, 19}},
    {.refcount = &g_refcnts[78].base, .data.refcounted = {g_bytes + 835, 3}},
    {.refcount = &g_refcnts[79].base, .data.refcounted = {g_bytes + 838, 5}},
    {.refcount = &g_refcnts[80].base, .data.refcounted = {g_bytes + 843, 7}},
    {.refcount = &g_refcnts[81].base, .data.refcounted = {g_bytes + 850, 7}},
    {.refcount = &g_refcnts[82].base, .data.refcounted = {g_bytes + 857, 11}},
    {.refcount = &g_refcnts[83].base, .data.refcounted = {g_bytes + 868, 7}},
    {.refcount = &g_refcnts[84].base, .data.refcounted = {g_bytes + 875, 6}},
    {.refcount = &g_refcnts[85].base, .data.refcounted = {g_bytes + 881, 10}},
    {.refcount = &g_refcnts[86].base, .data.refcounted = {g_bytes + 891, 1}},
    {.refcount = &g_refcnts[87].base, .data.refcounted = {g_bytes + 892, 36}},
    {.refcount = &g_refcnts[88].base, .data.refcounted = {g_bytes + 928, 11}},
    {.refcount = &g_refcnts[89].base, .data.refcounted = {g_bytes + 939, 7}},
    {.refcount = &g_refcnts[90].base, .data.refcounted = {g_bytes + 946, 25}},
    {.refcount = &g_refcnts[91].base, .data.refcounted = {g_bytes + 971, 2}},
    {.refcount = &g_refcnts[92].base, .data.refcounted = {g_bytes + 973, 8}},
    {.refcount = &g_refcnts[93].base, .data.refcounted = {g_bytes + 981, 17}},
    {.refcount = &g_refcnts[94].base, .data.refcounted = {g_bytes + 998, 10}},
    {.refcount = &g_refcnts[95].base, .data.refcounted = {g_bytes + 1008, 4}},
    {.refcount = &g_refcnts[96].base, .data.refcounted = {g_bytes + 1012, 3}},
    {.refcount = &g_refcnts[97].base, .data.refcounted = {g_bytes + 1015, 16}},
};

int grpc_static_metadata_index(grpc_slice slice) {
  static_slice_refcount *r = (static_slice_refcount *)slice.refcount;
  if (slice.data.refcounted.bytes == g_bytes + r->offset &&
      slice.data.refcounted.length == r->length)
    return (int)(r - g_refcnts);
  return -1;
}

uintptr_t grpc_static_mdelem_user_data[GRPC_STATIC_MDELEM_COUNT] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 4, 8, 6, 2, 4, 8, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

#define ELEMS_PHASHLEN 0x40
#define ELEMS_PHASHNKEYS 81
#define ELEMS_PHASHRANGE 128
#define ELEMS_PHASHSALT 0x9e3779b9

static const uint8_t elems_tab[] = {
    0,  17, 61, 28, 4,  12, 47, 0,  0,  0,  61, 0,  47, 0,  61, 76,
    61, 70, 76, 0,  0,  10, 4,  60, 0,  0,  0,  16, 88, 47, 1,  76,
    76, 0,  76, 0,  61, 0,  23, 0,  0,  51, 1,  92, 32, 0,  25, 0,
    34, 0,  37, 0,  76, 76, 32, 38, 70, 79, 81, 0,  64, 0,  0,  0,
};

static uint32_t elems_phash(uint32_t val) {
  val -= 917;

  uint32_t a, b, rsl;

  b = (val & 0x3f);
  a = ((val << 18) >> 26);
  rsl = (a ^ elems_tab[b]);
  return rsl;
}

static const uint16_t elem_keys[] = {
    2091, 1405, 8728, 2777, 7192, 2287, 2581, 2483, 2973, 4441, 3561, 3951,
    6403, 4463, 9441, 8726, 2875, 5423, 8730, 7338, 6109, 6207, 6697, 6893,
    7229, 8363, 8729, 3952, 8173, 8191, 8725, 8853, 9245, 9343, 1601, 8727,
    7481, 7340, 7971, 7775, 6501, 3973, 3659, 3979, 3463, 3980, 1307, 8190,
    9010, 8731, 4901, 6599, 3365, 7579, 6795, 9147, 9539, 8069, 6305, 7873,
    1209, 1111, 1699, 1503, 7089, 4468, 2189, 4900, 7232, 2385, 6991, 3978,
    1993, 4902, 2679, 2762, 1013, 3981, 1230, 1895, 8265, 0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0};
static const uint8_t elem_idxs[] = {
    11, 5,  70, 19, 51, 13, 16, 15, 21, 33, 24, 26, 43, 34, 79, 68, 20,
    39, 72, 54, 40, 41, 46, 48, 52, 66, 71, 27, 62, 64, 67, 74, 77, 78,
    7,  69, 56, 55, 60, 58, 44, 28, 25, 30, 23, 31, 4,  63, 75, 73, 37,
    45, 22, 57, 47, 76, 80, 61, 42, 59, 2,  0,  8,  6,  50, 35, 12, 36,
    53, 14, 49, 29, 10, 38, 17, 18, 1,  32, 3,  9,  65};

grpc_mdelem grpc_static_mdelem_for_static_strings(int a, int b) {
  if (a == -1 || b == -1) return GRPC_MDNULL;
  uint32_t k = (uint32_t)(a * 98 + b);
  uint32_t h = elems_phash(k);
  return elem_keys[h] == k
             ? GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[elem_idxs[h]],
                                GRPC_MDELEM_STORAGE_STATIC)
             : GRPC_MDNULL;
}

grpc_mdelem_data grpc_static_mdelem_table[GRPC_STATIC_MDELEM_COUNT] = {
    {{.refcount = &g_refcnts[11].base, .data.refcounted = {g_bytes + 30, 14}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[10].base, .data.refcounted = {g_bytes + 24, 6}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[12].base, .data.refcounted = {g_bytes + 44, 15}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[12].base, .data.refcounted = {g_bytes + 44, 15}},
     {.refcount = &g_refcnts[54].base, .data.refcounted = {g_bytes + 577, 13}}},
    {{.refcount = &g_refcnts[13].base, .data.refcounted = {g_bytes + 59, 15}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[14].base, .data.refcounted = {g_bytes + 74, 13}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[15].base, .data.refcounted = {g_bytes + 87, 27}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[16].base, .data.refcounted = {g_bytes + 114, 3}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[17].base, .data.refcounted = {g_bytes + 117, 5}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[19].base, .data.refcounted = {g_bytes + 138, 10}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 148, 13}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[21].base, .data.refcounted = {g_bytes + 161, 13}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[22].base, .data.refcounted = {g_bytes + 174, 19}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[23].base, .data.refcounted = {g_bytes + 193, 16}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[24].base, .data.refcounted = {g_bytes + 209, 16}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[25].base, .data.refcounted = {g_bytes + 225, 14}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[26].base, .data.refcounted = {g_bytes + 239, 16}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[27].base, .data.refcounted = {g_bytes + 255, 13}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[28].base, .data.refcounted = {g_bytes + 268, 12}},
     {.refcount = &g_refcnts[18].base, .data.refcounted = {g_bytes + 122, 16}}},
    {{.refcount = &g_refcnts[28].base, .data.refcounted = {g_bytes + 268, 12}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[29].base, .data.refcounted = {g_bytes + 280, 6}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[30].base, .data.refcounted = {g_bytes + 286, 4}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[34].base, .data.refcounted = {g_bytes + 309, 4}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[35].base, .data.refcounted = {g_bytes + 313, 6}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[36].base, .data.refcounted = {g_bytes + 319, 7}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[37].base, .data.refcounted = {g_bytes + 326, 4}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[40].base, .data.refcounted = {g_bytes + 337, 20}},
     {.refcount = &g_refcnts[31].base, .data.refcounted = {g_bytes + 290, 7}}},
    {{.refcount = &g_refcnts[40].base, .data.refcounted = {g_bytes + 337, 20}},
     {.refcount = &g_refcnts[32].base, .data.refcounted = {g_bytes + 297, 12}}},
    {{.refcount = &g_refcnts[40].base, .data.refcounted = {g_bytes + 337, 20}},
     {.refcount = &g_refcnts[53].base, .data.refcounted = {g_bytes + 573, 4}}},
    {{.refcount = &g_refcnts[40].base, .data.refcounted = {g_bytes + 337, 20}},
     {.refcount = &g_refcnts[58].base, .data.refcounted = {g_bytes + 603, 8}}},
    {{.refcount = &g_refcnts[40].base, .data.refcounted = {g_bytes + 337, 20}},
     {.refcount = &g_refcnts[59].base, .data.refcounted = {g_bytes + 611, 16}}},
    {{.refcount = &g_refcnts[40].base, .data.refcounted = {g_bytes + 337, 20}},
     {.refcount = &g_refcnts[60].base, .data.refcounted = {g_bytes + 627, 21}}},
    {{.refcount = &g_refcnts[40].base, .data.refcounted = {g_bytes + 337, 20}},
     {.refcount = &g_refcnts[61].base, .data.refcounted = {g_bytes + 648, 13}}},
    {{.refcount = &g_refcnts[45].base, .data.refcounted = {g_bytes + 449, 13}},
     {.refcount = &g_refcnts[31].base, .data.refcounted = {g_bytes + 290, 7}}},
    {{.refcount = &g_refcnts[45].base, .data.refcounted = {g_bytes + 449, 13}},
     {.refcount = &g_refcnts[53].base, .data.refcounted = {g_bytes + 573, 4}}},
    {{.refcount = &g_refcnts[45].base, .data.refcounted = {g_bytes + 449, 13}},
     {.refcount = &g_refcnts[58].base, .data.refcounted = {g_bytes + 603, 8}}},
    {{.refcount = &g_refcnts[50].base, .data.refcounted = {g_bytes + 534, 11}},
     {.refcount = &g_refcnts[0].base, .data.refcounted = {g_bytes + 0, 1}}},
    {{.refcount = &g_refcnts[50].base, .data.refcounted = {g_bytes + 534, 11}},
     {.refcount = &g_refcnts[1].base, .data.refcounted = {g_bytes + 1, 1}}},
    {{.refcount = &g_refcnts[50].base, .data.refcounted = {g_bytes + 534, 11}},
     {.refcount = &g_refcnts[2].base, .data.refcounted = {g_bytes + 2, 1}}},
    {{.refcount = &g_refcnts[55].base, .data.refcounted = {g_bytes + 590, 4}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[62].base, .data.refcounted = {g_bytes + 661, 8}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[63].base, .data.refcounted = {g_bytes + 669, 17}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[64].base, .data.refcounted = {g_bytes + 686, 13}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[65].base, .data.refcounted = {g_bytes + 699, 8}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[66].base, .data.refcounted = {g_bytes + 707, 19}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[67].base, .data.refcounted = {g_bytes + 726, 13}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[68].base, .data.refcounted = {g_bytes + 739, 11}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[69].base, .data.refcounted = {g_bytes + 750, 8}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[70].base, .data.refcounted = {g_bytes + 758, 4}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[71].base, .data.refcounted = {g_bytes + 762, 8}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[72].base, .data.refcounted = {g_bytes + 770, 12}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[73].base, .data.refcounted = {g_bytes + 782, 7}},
     {.refcount = &g_refcnts[38].base, .data.refcounted = {g_bytes + 330, 3}}},
    {{.refcount = &g_refcnts[73].base, .data.refcounted = {g_bytes + 782, 7}},
     {.refcount = &g_refcnts[75].base, .data.refcounted = {g_bytes + 794, 4}}},
    {{.refcount = &g_refcnts[73].base, .data.refcounted = {g_bytes + 782, 7}},
     {.refcount = &g_refcnts[78].base, .data.refcounted = {g_bytes + 835, 3}}},
    {{.refcount = &g_refcnts[74].base, .data.refcounted = {g_bytes + 789, 5}},
     {.refcount = &g_refcnts[86].base, .data.refcounted = {g_bytes + 891, 1}}},
    {{.refcount = &g_refcnts[74].base, .data.refcounted = {g_bytes + 789, 5}},
     {.refcount = &g_refcnts[88].base, .data.refcounted = {g_bytes + 928, 11}}},
    {{.refcount = &g_refcnts[76].base, .data.refcounted = {g_bytes + 798, 18}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[77].base, .data.refcounted = {g_bytes + 816, 19}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[79].base, .data.refcounted = {g_bytes + 838, 5}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[80].base, .data.refcounted = {g_bytes + 843, 7}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[81].base, .data.refcounted = {g_bytes + 850, 7}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[82].base, .data.refcounted = {g_bytes + 857, 11}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[83].base, .data.refcounted = {g_bytes + 868, 7}},
     {.refcount = &g_refcnts[39].base, .data.refcounted = {g_bytes + 333, 4}}},
    {{.refcount = &g_refcnts[83].base, .data.refcounted = {g_bytes + 868, 7}},
     {.refcount = &g_refcnts[56].base, .data.refcounted = {g_bytes + 594, 4}}},
    {{.refcount = &g_refcnts[83].base, .data.refcounted = {g_bytes + 868, 7}},
     {.refcount = &g_refcnts[57].base, .data.refcounted = {g_bytes + 598, 5}}},
    {{.refcount = &g_refcnts[84].base, .data.refcounted = {g_bytes + 875, 6}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[85].base, .data.refcounted = {g_bytes + 881, 10}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[89].base, .data.refcounted = {g_bytes + 939, 7}},
     {.refcount = &g_refcnts[3].base, .data.refcounted = {g_bytes + 3, 3}}},
    {{.refcount = &g_refcnts[89].base, .data.refcounted = {g_bytes + 939, 7}},
     {.refcount = &g_refcnts[4].base, .data.refcounted = {g_bytes + 6, 3}}},
    {{.refcount = &g_refcnts[89].base, .data.refcounted = {g_bytes + 939, 7}},
     {.refcount = &g_refcnts[5].base, .data.refcounted = {g_bytes + 9, 3}}},
    {{.refcount = &g_refcnts[89].base, .data.refcounted = {g_bytes + 939, 7}},
     {.refcount = &g_refcnts[6].base, .data.refcounted = {g_bytes + 12, 3}}},
    {{.refcount = &g_refcnts[89].base, .data.refcounted = {g_bytes + 939, 7}},
     {.refcount = &g_refcnts[7].base, .data.refcounted = {g_bytes + 15, 3}}},
    {{.refcount = &g_refcnts[89].base, .data.refcounted = {g_bytes + 939, 7}},
     {.refcount = &g_refcnts[8].base, .data.refcounted = {g_bytes + 18, 3}}},
    {{.refcount = &g_refcnts[89].base, .data.refcounted = {g_bytes + 939, 7}},
     {.refcount = &g_refcnts[9].base, .data.refcounted = {g_bytes + 21, 3}}},
    {{.refcount = &g_refcnts[90].base, .data.refcounted = {g_bytes + 946, 25}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[91].base, .data.refcounted = {g_bytes + 971, 2}},
     {.refcount = &g_refcnts[92].base, .data.refcounted = {g_bytes + 973, 8}}},
    {{.refcount = &g_refcnts[93].base, .data.refcounted = {g_bytes + 981, 17}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[94].base, .data.refcounted = {g_bytes + 998, 10}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[95].base, .data.refcounted = {g_bytes + 1008, 4}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[96].base, .data.refcounted = {g_bytes + 1012, 3}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
    {{.refcount = &g_refcnts[97].base, .data.refcounted = {g_bytes + 1015, 16}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 309, 0}}},
};
const uint8_t grpc_static_accept_encoding_metadata[8] = {0,  29, 26, 30,
                                                         28, 32, 27, 31};
