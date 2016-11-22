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
    58,  112, 97,  116, 104, 58,  109, 101, 116, 104, 111, 100, 58,  115, 116,
    97,  116, 117, 115, 58,  97,  117, 116, 104, 111, 114, 105, 116, 121, 58,
    115, 99,  104, 101, 109, 101, 116, 101, 103, 114, 112, 99,  45,  109, 101,
    115, 115, 97,  103, 101, 103, 114, 112, 99,  45,  115, 116, 97,  116, 117,
    115, 103, 114, 112, 99,  45,  112, 97,  121, 108, 111, 97,  100, 45,  98,
    105, 110, 103, 114, 112, 99,  45,  101, 110, 99,  111, 100, 105, 110, 103,
    103, 114, 112, 99,  45,  97,  99,  99,  101, 112, 116, 45,  101, 110, 99,
    111, 100, 105, 110, 103, 99,  111, 110, 116, 101, 110, 116, 45,  116, 121,
    112, 101, 103, 114, 112, 99,  45,  105, 110, 116, 101, 114, 110, 97,  108,
    45,  101, 110, 99,  111, 100, 105, 110, 103, 45,  114, 101, 113, 117, 101,
    115, 116, 117, 115, 101, 114, 45,  97,  103, 101, 110, 116, 104, 111, 115,
    116, 108, 98,  45,  116, 111, 107, 101, 110, 108, 98,  45,  99,  111, 115,
    116, 45,  98,  105, 110, 103, 114, 112, 99,  45,  116, 105, 109, 101, 111,
    117, 116, 103, 114, 112, 99,  45,  116, 114, 97,  99,  105, 110, 103, 45,
    98,  105, 110, 103, 114, 112, 99,  45,  115, 116, 97,  116, 115, 45,  98,
    105, 110, 103, 114, 112, 99,  46,  119, 97,  105, 116, 95,  102, 111, 114,
    95,  114, 101, 97,  100, 121, 103, 114, 112, 99,  46,  116, 105, 109, 101,
    111, 117, 116, 103, 114, 112, 99,  46,  109, 97,  120, 95,  114, 101, 113,
    117, 101, 115, 116, 95,  109, 101, 115, 115, 97,  103, 101, 95,  98,  121,
    116, 101, 115, 103, 114, 112, 99,  46,  109, 97,  120, 95,  114, 101, 115,
    112, 111, 110, 115, 101, 95,  109, 101, 115, 115, 97,  103, 101, 95,  98,
    121, 116, 101, 115, 47,  103, 114, 112, 99,  46,  108, 98,  46,  118, 49,
    46,  76,  111, 97,  100, 66,  97,  108, 97,  110, 99,  101, 114, 47,  66,
    97,  108, 97,  110, 99,  101, 76,  111, 97,  100, 48,  49,  50,  105, 100,
    101, 110, 116, 105, 116, 121, 103, 122, 105, 112, 100, 101, 102, 108, 97,
    116, 101, 116, 114, 97,  105, 108, 101, 114, 115, 97,  112, 112, 108, 105,
    99,  97,  116, 105, 111, 110, 47,  103, 114, 112, 99,  80,  79,  83,  84,
    50,  48,  48,  52,  48,  52,  104, 116, 116, 112, 104, 116, 116, 112, 115,
    103, 114, 112, 99,  71,  69,  84,  80,  85,  84,  47,  47,  105, 110, 100,
    101, 120, 46,  104, 116, 109, 108, 50,  48,  52,  50,  48,  54,  51,  48,
    52,  52,  48,  48,  53,  48,  48,  97,  99,  99,  101, 112, 116, 45,  99,
    104, 97,  114, 115, 101, 116, 97,  99,  99,  101, 112, 116, 45,  101, 110,
    99,  111, 100, 105, 110, 103, 103, 122, 105, 112, 44,  32,  100, 101, 102,
    108, 97,  116, 101, 97,  99,  99,  101, 112, 116, 45,  108, 97,  110, 103,
    117, 97,  103, 101, 97,  99,  99,  101, 112, 116, 45,  114, 97,  110, 103,
    101, 115, 97,  99,  99,  101, 112, 116, 97,  99,  99,  101, 115, 115, 45,
    99,  111, 110, 116, 114, 111, 108, 45,  97,  108, 108, 111, 119, 45,  111,
    114, 105, 103, 105, 110, 97,  103, 101, 97,  108, 108, 111, 119, 97,  117,
    116, 104, 111, 114, 105, 122, 97,  116, 105, 111, 110, 99,  97,  99,  104,
    101, 45,  99,  111, 110, 116, 114, 111, 108, 99,  111, 110, 116, 101, 110,
    116, 45,  100, 105, 115, 112, 111, 115, 105, 116, 105, 111, 110, 99,  111,
    110, 116, 101, 110, 116, 45,  101, 110, 99,  111, 100, 105, 110, 103, 99,
    111, 110, 116, 101, 110, 116, 45,  108, 97,  110, 103, 117, 97,  103, 101,
    99,  111, 110, 116, 101, 110, 116, 45,  108, 101, 110, 103, 116, 104, 99,
    111, 110, 116, 101, 110, 116, 45,  108, 111, 99,  97,  116, 105, 111, 110,
    99,  111, 110, 116, 101, 110, 116, 45,  114, 97,  110, 103, 101, 99,  111,
    111, 107, 105, 101, 100, 97,  116, 101, 101, 116, 97,  103, 101, 120, 112,
    101, 99,  116, 101, 120, 112, 105, 114, 101, 115, 102, 114, 111, 109, 105,
    102, 45,  109, 97,  116, 99,  104, 105, 102, 45,  109, 111, 100, 105, 102,
    105, 101, 100, 45,  115, 105, 110, 99,  101, 105, 102, 45,  110, 111, 110,
    101, 45,  109, 97,  116, 99,  104, 105, 102, 45,  114, 97,  110, 103, 101,
    105, 102, 45,  117, 110, 109, 111, 100, 105, 102, 105, 101, 100, 45,  115,
    105, 110, 99,  101, 108, 97,  115, 116, 45,  109, 111, 100, 105, 102, 105,
    101, 100, 108, 105, 110, 107, 108, 111, 99,  97,  116, 105, 111, 110, 109,
    97,  120, 45,  102, 111, 114, 119, 97,  114, 100, 115, 112, 114, 111, 120,
    121, 45,  97,  117, 116, 104, 101, 110, 116, 105, 99,  97,  116, 101, 112,
    114, 111, 120, 121, 45,  97,  117, 116, 104, 111, 114, 105, 122, 97,  116,
    105, 111, 110, 114, 97,  110, 103, 101, 114, 101, 102, 101, 114, 101, 114,
    114, 101, 102, 114, 101, 115, 104, 114, 101, 116, 114, 121, 45,  97,  102,
    116, 101, 114, 115, 101, 114, 118, 101, 114, 115, 101, 116, 45,  99,  111,
    111, 107, 105, 101, 115, 116, 114, 105, 99,  116, 45,  116, 114, 97,  110,
    115, 112, 111, 114, 116, 45,  115, 101, 99,  117, 114, 105, 116, 121, 116,
    114, 97,  110, 115, 102, 101, 114, 45,  101, 110, 99,  111, 100, 105, 110,
    103, 118, 97,  114, 121, 118, 105, 97,  119, 119, 119, 45,  97,  117, 116,
    104, 101, 110, 116, 105, 99,  97,  116, 101, 105, 100, 101, 110, 116, 105,
    116, 121, 44,  100, 101, 102, 108, 97,  116, 101, 105, 100, 101, 110, 116,
    105, 116, 121, 44,  103, 122, 105, 112, 100, 101, 102, 108, 97,  116, 101,
    44,  103, 122, 105, 112, 105, 100, 101, 110, 116, 105, 116, 121, 44,  100,
    101, 102, 108, 97,  116, 101, 44,  103, 122, 105, 112};

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
    {{&static_vtable}, 0, 5},    {{&static_vtable}, 5, 7},
    {{&static_vtable}, 12, 7},   {{&static_vtable}, 19, 10},
    {{&static_vtable}, 29, 7},   {{&static_vtable}, 36, 2},
    {{&static_vtable}, 38, 12},  {{&static_vtable}, 50, 11},
    {{&static_vtable}, 61, 16},  {{&static_vtable}, 77, 13},
    {{&static_vtable}, 90, 20},  {{&static_vtable}, 110, 12},
    {{&static_vtable}, 122, 30}, {{&static_vtable}, 152, 10},
    {{&static_vtable}, 162, 4},  {{&static_vtable}, 166, 8},
    {{&static_vtable}, 174, 11}, {{&static_vtable}, 185, 12},
    {{&static_vtable}, 197, 16}, {{&static_vtable}, 213, 14},
    {{&static_vtable}, 227, 0},  {{&static_vtable}, 227, 19},
    {{&static_vtable}, 246, 12}, {{&static_vtable}, 258, 30},
    {{&static_vtable}, 288, 31}, {{&static_vtable}, 319, 36},
    {{&static_vtable}, 355, 1},  {{&static_vtable}, 356, 1},
    {{&static_vtable}, 357, 1},  {{&static_vtable}, 358, 8},
    {{&static_vtable}, 366, 4},  {{&static_vtable}, 370, 7},
    {{&static_vtable}, 377, 8},  {{&static_vtable}, 385, 16},
    {{&static_vtable}, 401, 4},  {{&static_vtable}, 405, 3},
    {{&static_vtable}, 408, 3},  {{&static_vtable}, 411, 4},
    {{&static_vtable}, 415, 5},  {{&static_vtable}, 420, 4},
    {{&static_vtable}, 424, 3},  {{&static_vtable}, 427, 3},
    {{&static_vtable}, 430, 1},  {{&static_vtable}, 431, 11},
    {{&static_vtable}, 442, 3},  {{&static_vtable}, 445, 3},
    {{&static_vtable}, 448, 3},  {{&static_vtable}, 451, 3},
    {{&static_vtable}, 454, 3},  {{&static_vtable}, 457, 14},
    {{&static_vtable}, 471, 15}, {{&static_vtable}, 486, 13},
    {{&static_vtable}, 499, 15}, {{&static_vtable}, 514, 13},
    {{&static_vtable}, 527, 6},  {{&static_vtable}, 533, 27},
    {{&static_vtable}, 560, 3},  {{&static_vtable}, 563, 5},
    {{&static_vtable}, 568, 13}, {{&static_vtable}, 581, 13},
    {{&static_vtable}, 594, 19}, {{&static_vtable}, 613, 16},
    {{&static_vtable}, 629, 16}, {{&static_vtable}, 645, 14},
    {{&static_vtable}, 659, 16}, {{&static_vtable}, 675, 13},
    {{&static_vtable}, 688, 6},  {{&static_vtable}, 694, 4},
    {{&static_vtable}, 698, 4},  {{&static_vtable}, 702, 6},
    {{&static_vtable}, 708, 7},  {{&static_vtable}, 715, 4},
    {{&static_vtable}, 719, 8},  {{&static_vtable}, 727, 17},
    {{&static_vtable}, 744, 13}, {{&static_vtable}, 757, 8},
    {{&static_vtable}, 765, 19}, {{&static_vtable}, 784, 13},
    {{&static_vtable}, 797, 4},  {{&static_vtable}, 801, 8},
    {{&static_vtable}, 809, 12}, {{&static_vtable}, 821, 18},
    {{&static_vtable}, 839, 19}, {{&static_vtable}, 858, 5},
    {{&static_vtable}, 863, 7},  {{&static_vtable}, 870, 7},
    {{&static_vtable}, 877, 11}, {{&static_vtable}, 888, 6},
    {{&static_vtable}, 894, 10}, {{&static_vtable}, 904, 25},
    {{&static_vtable}, 929, 17}, {{&static_vtable}, 946, 4},
    {{&static_vtable}, 950, 3},  {{&static_vtable}, 953, 16},
    {{&static_vtable}, 969, 16}, {{&static_vtable}, 985, 13},
    {{&static_vtable}, 998, 12}, {{&static_vtable}, 1010, 21},
};

bool grpc_is_static_metadata_string(grpc_slice slice) {
  return slice.refcount != NULL && slice.refcount->vtable == &static_vtable;
}

const grpc_slice grpc_static_slice_table[GRPC_STATIC_MDSTR_COUNT] = {
    {.refcount = &g_refcnts[0].base, .data.refcounted = {g_bytes + 0, 5}},
    {.refcount = &g_refcnts[1].base, .data.refcounted = {g_bytes + 5, 7}},
    {.refcount = &g_refcnts[2].base, .data.refcounted = {g_bytes + 12, 7}},
    {.refcount = &g_refcnts[3].base, .data.refcounted = {g_bytes + 19, 10}},
    {.refcount = &g_refcnts[4].base, .data.refcounted = {g_bytes + 29, 7}},
    {.refcount = &g_refcnts[5].base, .data.refcounted = {g_bytes + 36, 2}},
    {.refcount = &g_refcnts[6].base, .data.refcounted = {g_bytes + 38, 12}},
    {.refcount = &g_refcnts[7].base, .data.refcounted = {g_bytes + 50, 11}},
    {.refcount = &g_refcnts[8].base, .data.refcounted = {g_bytes + 61, 16}},
    {.refcount = &g_refcnts[9].base, .data.refcounted = {g_bytes + 77, 13}},
    {.refcount = &g_refcnts[10].base, .data.refcounted = {g_bytes + 90, 20}},
    {.refcount = &g_refcnts[11].base, .data.refcounted = {g_bytes + 110, 12}},
    {.refcount = &g_refcnts[12].base, .data.refcounted = {g_bytes + 122, 30}},
    {.refcount = &g_refcnts[13].base, .data.refcounted = {g_bytes + 152, 10}},
    {.refcount = &g_refcnts[14].base, .data.refcounted = {g_bytes + 162, 4}},
    {.refcount = &g_refcnts[15].base, .data.refcounted = {g_bytes + 166, 8}},
    {.refcount = &g_refcnts[16].base, .data.refcounted = {g_bytes + 174, 11}},
    {.refcount = &g_refcnts[17].base, .data.refcounted = {g_bytes + 185, 12}},
    {.refcount = &g_refcnts[18].base, .data.refcounted = {g_bytes + 197, 16}},
    {.refcount = &g_refcnts[19].base, .data.refcounted = {g_bytes + 213, 14}},
    {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}},
    {.refcount = &g_refcnts[21].base, .data.refcounted = {g_bytes + 227, 19}},
    {.refcount = &g_refcnts[22].base, .data.refcounted = {g_bytes + 246, 12}},
    {.refcount = &g_refcnts[23].base, .data.refcounted = {g_bytes + 258, 30}},
    {.refcount = &g_refcnts[24].base, .data.refcounted = {g_bytes + 288, 31}},
    {.refcount = &g_refcnts[25].base, .data.refcounted = {g_bytes + 319, 36}},
    {.refcount = &g_refcnts[26].base, .data.refcounted = {g_bytes + 355, 1}},
    {.refcount = &g_refcnts[27].base, .data.refcounted = {g_bytes + 356, 1}},
    {.refcount = &g_refcnts[28].base, .data.refcounted = {g_bytes + 357, 1}},
    {.refcount = &g_refcnts[29].base, .data.refcounted = {g_bytes + 358, 8}},
    {.refcount = &g_refcnts[30].base, .data.refcounted = {g_bytes + 366, 4}},
    {.refcount = &g_refcnts[31].base, .data.refcounted = {g_bytes + 370, 7}},
    {.refcount = &g_refcnts[32].base, .data.refcounted = {g_bytes + 377, 8}},
    {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 385, 16}},
    {.refcount = &g_refcnts[34].base, .data.refcounted = {g_bytes + 401, 4}},
    {.refcount = &g_refcnts[35].base, .data.refcounted = {g_bytes + 405, 3}},
    {.refcount = &g_refcnts[36].base, .data.refcounted = {g_bytes + 408, 3}},
    {.refcount = &g_refcnts[37].base, .data.refcounted = {g_bytes + 411, 4}},
    {.refcount = &g_refcnts[38].base, .data.refcounted = {g_bytes + 415, 5}},
    {.refcount = &g_refcnts[39].base, .data.refcounted = {g_bytes + 420, 4}},
    {.refcount = &g_refcnts[40].base, .data.refcounted = {g_bytes + 424, 3}},
    {.refcount = &g_refcnts[41].base, .data.refcounted = {g_bytes + 427, 3}},
    {.refcount = &g_refcnts[42].base, .data.refcounted = {g_bytes + 430, 1}},
    {.refcount = &g_refcnts[43].base, .data.refcounted = {g_bytes + 431, 11}},
    {.refcount = &g_refcnts[44].base, .data.refcounted = {g_bytes + 442, 3}},
    {.refcount = &g_refcnts[45].base, .data.refcounted = {g_bytes + 445, 3}},
    {.refcount = &g_refcnts[46].base, .data.refcounted = {g_bytes + 448, 3}},
    {.refcount = &g_refcnts[47].base, .data.refcounted = {g_bytes + 451, 3}},
    {.refcount = &g_refcnts[48].base, .data.refcounted = {g_bytes + 454, 3}},
    {.refcount = &g_refcnts[49].base, .data.refcounted = {g_bytes + 457, 14}},
    {.refcount = &g_refcnts[50].base, .data.refcounted = {g_bytes + 471, 15}},
    {.refcount = &g_refcnts[51].base, .data.refcounted = {g_bytes + 486, 13}},
    {.refcount = &g_refcnts[52].base, .data.refcounted = {g_bytes + 499, 15}},
    {.refcount = &g_refcnts[53].base, .data.refcounted = {g_bytes + 514, 13}},
    {.refcount = &g_refcnts[54].base, .data.refcounted = {g_bytes + 527, 6}},
    {.refcount = &g_refcnts[55].base, .data.refcounted = {g_bytes + 533, 27}},
    {.refcount = &g_refcnts[56].base, .data.refcounted = {g_bytes + 560, 3}},
    {.refcount = &g_refcnts[57].base, .data.refcounted = {g_bytes + 563, 5}},
    {.refcount = &g_refcnts[58].base, .data.refcounted = {g_bytes + 568, 13}},
    {.refcount = &g_refcnts[59].base, .data.refcounted = {g_bytes + 581, 13}},
    {.refcount = &g_refcnts[60].base, .data.refcounted = {g_bytes + 594, 19}},
    {.refcount = &g_refcnts[61].base, .data.refcounted = {g_bytes + 613, 16}},
    {.refcount = &g_refcnts[62].base, .data.refcounted = {g_bytes + 629, 16}},
    {.refcount = &g_refcnts[63].base, .data.refcounted = {g_bytes + 645, 14}},
    {.refcount = &g_refcnts[64].base, .data.refcounted = {g_bytes + 659, 16}},
    {.refcount = &g_refcnts[65].base, .data.refcounted = {g_bytes + 675, 13}},
    {.refcount = &g_refcnts[66].base, .data.refcounted = {g_bytes + 688, 6}},
    {.refcount = &g_refcnts[67].base, .data.refcounted = {g_bytes + 694, 4}},
    {.refcount = &g_refcnts[68].base, .data.refcounted = {g_bytes + 698, 4}},
    {.refcount = &g_refcnts[69].base, .data.refcounted = {g_bytes + 702, 6}},
    {.refcount = &g_refcnts[70].base, .data.refcounted = {g_bytes + 708, 7}},
    {.refcount = &g_refcnts[71].base, .data.refcounted = {g_bytes + 715, 4}},
    {.refcount = &g_refcnts[72].base, .data.refcounted = {g_bytes + 719, 8}},
    {.refcount = &g_refcnts[73].base, .data.refcounted = {g_bytes + 727, 17}},
    {.refcount = &g_refcnts[74].base, .data.refcounted = {g_bytes + 744, 13}},
    {.refcount = &g_refcnts[75].base, .data.refcounted = {g_bytes + 757, 8}},
    {.refcount = &g_refcnts[76].base, .data.refcounted = {g_bytes + 765, 19}},
    {.refcount = &g_refcnts[77].base, .data.refcounted = {g_bytes + 784, 13}},
    {.refcount = &g_refcnts[78].base, .data.refcounted = {g_bytes + 797, 4}},
    {.refcount = &g_refcnts[79].base, .data.refcounted = {g_bytes + 801, 8}},
    {.refcount = &g_refcnts[80].base, .data.refcounted = {g_bytes + 809, 12}},
    {.refcount = &g_refcnts[81].base, .data.refcounted = {g_bytes + 821, 18}},
    {.refcount = &g_refcnts[82].base, .data.refcounted = {g_bytes + 839, 19}},
    {.refcount = &g_refcnts[83].base, .data.refcounted = {g_bytes + 858, 5}},
    {.refcount = &g_refcnts[84].base, .data.refcounted = {g_bytes + 863, 7}},
    {.refcount = &g_refcnts[85].base, .data.refcounted = {g_bytes + 870, 7}},
    {.refcount = &g_refcnts[86].base, .data.refcounted = {g_bytes + 877, 11}},
    {.refcount = &g_refcnts[87].base, .data.refcounted = {g_bytes + 888, 6}},
    {.refcount = &g_refcnts[88].base, .data.refcounted = {g_bytes + 894, 10}},
    {.refcount = &g_refcnts[89].base, .data.refcounted = {g_bytes + 904, 25}},
    {.refcount = &g_refcnts[90].base, .data.refcounted = {g_bytes + 929, 17}},
    {.refcount = &g_refcnts[91].base, .data.refcounted = {g_bytes + 946, 4}},
    {.refcount = &g_refcnts[92].base, .data.refcounted = {g_bytes + 950, 3}},
    {.refcount = &g_refcnts[93].base, .data.refcounted = {g_bytes + 953, 16}},
    {.refcount = &g_refcnts[94].base, .data.refcounted = {g_bytes + 969, 16}},
    {.refcount = &g_refcnts[95].base, .data.refcounted = {g_bytes + 985, 13}},
    {.refcount = &g_refcnts[96].base, .data.refcounted = {g_bytes + 998, 12}},
    {.refcount = &g_refcnts[97].base, .data.refcounted = {g_bytes + 1010, 21}},
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
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 4, 4, 6, 6, 8, 8};

#define ELEMS_PHASHLEN 0x40
#define ELEMS_PHASHNKEYS 81
#define ELEMS_PHASHRANGE 128
#define ELEMS_PHASHSALT 0x9e3779b9

static const uint8_t elems_tab[] = {
    20, 1,  0,  61, 61, 34, 10, 16, 0,  0,  0,  0,  34, 61, 0,  1,
    0,  0,  0,  61, 0,  88, 0,  4,  0,  47, 0,  47, 12, 7,  0,  16,
    51, 87, 76, 4,  79, 10, 70, 47, 76, 61, 71, 88, 0,  88, 0,  47,
    0,  16, 0,  83, 0,  57, 0,  75, 0,  42, 0,  90, 0,  42, 70, 0,
};

static uint32_t elems_phash(uint32_t val) {
  val += (uint32_t)-11;

  uint32_t a, b, rsl;

  b = (val & 0x3f);
  a = ((val << 18) >> 26);
  rsl = (a ^ elems_tab[b]);
  return rsl;
}

static const uint16_t elem_keys[] = {
    138,  522,  714,  5116, 1098, 430,  5802, 232,  8840, 913,  240,  8644,
    231,  8742, 7762, 1392, 42,   5410, 4822, 5998, 139,  1490, 5900, 7664,
    6292, 8448, 6684, 7272, 7370, 8350, 8154, 7958, 7566, 912,  9036, 7860,
    6488, 8546, 1111, 9134, 712,  5214, 132,  1074, 1010, 5312, 314,  242,
    8252, 4951, 8938, 43,   7076, 6096, 6586, 6194, 1294, 1076, 5606, 1588,
    5704, 244,  911,  5508, 6390, 7174, 6880, 1077, 713,  1009, 241,  8056,
    1075, 6782, 7468, 4920, 243,  429,  431,  1011, 6978, 0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0};
static const uint8_t elem_idxs[] = {
    15, 6,  2,  27, 41, 12, 34, 10, 69, 5,  19, 67, 9,  68, 58, 48, 17,
    30, 24, 36, 16, 55, 35, 57, 39, 65, 44, 51, 52, 64, 62, 60, 54, 4,
    72, 59, 42, 66, 7,  73, 0,  28, 8,  76, 77, 29, 14, 21, 63, 26, 71,
    18, 49, 37, 43, 38, 70, 79, 32, 56, 33, 23, 3,  31, 40, 50, 46, 80,
    1,  74, 20, 61, 78, 45, 53, 25, 22, 11, 13, 75, 47};

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
    {{.refcount = &g_refcnts[7].base, .data.refcounted = {g_bytes + 50, 11}},
     {.refcount = &g_refcnts[26].base, .data.refcounted = {g_bytes + 355, 1}}},
    {{.refcount = &g_refcnts[7].base, .data.refcounted = {g_bytes + 50, 11}},
     {.refcount = &g_refcnts[27].base, .data.refcounted = {g_bytes + 356, 1}}},
    {{.refcount = &g_refcnts[7].base, .data.refcounted = {g_bytes + 50, 11}},
     {.refcount = &g_refcnts[28].base, .data.refcounted = {g_bytes + 357, 1}}},
    {{.refcount = &g_refcnts[9].base, .data.refcounted = {g_bytes + 77, 13}},
     {.refcount = &g_refcnts[29].base, .data.refcounted = {g_bytes + 358, 8}}},
    {{.refcount = &g_refcnts[9].base, .data.refcounted = {g_bytes + 77, 13}},
     {.refcount = &g_refcnts[30].base, .data.refcounted = {g_bytes + 366, 4}}},
    {{.refcount = &g_refcnts[9].base, .data.refcounted = {g_bytes + 77, 13}},
     {.refcount = &g_refcnts[31].base, .data.refcounted = {g_bytes + 370, 7}}},
    {{.refcount = &g_refcnts[5].base, .data.refcounted = {g_bytes + 36, 2}},
     {.refcount = &g_refcnts[32].base, .data.refcounted = {g_bytes + 377, 8}}},
    {{.refcount = &g_refcnts[11].base, .data.refcounted = {g_bytes + 110, 12}},
     {.refcount = &g_refcnts[33].base, .data.refcounted = {g_bytes + 385, 16}}},
    {{.refcount = &g_refcnts[1].base, .data.refcounted = {g_bytes + 5, 7}},
     {.refcount = &g_refcnts[34].base, .data.refcounted = {g_bytes + 401, 4}}},
    {{.refcount = &g_refcnts[2].base, .data.refcounted = {g_bytes + 12, 7}},
     {.refcount = &g_refcnts[35].base, .data.refcounted = {g_bytes + 405, 3}}},
    {{.refcount = &g_refcnts[2].base, .data.refcounted = {g_bytes + 12, 7}},
     {.refcount = &g_refcnts[36].base, .data.refcounted = {g_bytes + 408, 3}}},
    {{.refcount = &g_refcnts[4].base, .data.refcounted = {g_bytes + 29, 7}},
     {.refcount = &g_refcnts[37].base, .data.refcounted = {g_bytes + 411, 4}}},
    {{.refcount = &g_refcnts[4].base, .data.refcounted = {g_bytes + 29, 7}},
     {.refcount = &g_refcnts[38].base, .data.refcounted = {g_bytes + 415, 5}}},
    {{.refcount = &g_refcnts[4].base, .data.refcounted = {g_bytes + 29, 7}},
     {.refcount = &g_refcnts[39].base, .data.refcounted = {g_bytes + 420, 4}}},
    {{.refcount = &g_refcnts[3].base, .data.refcounted = {g_bytes + 19, 10}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[1].base, .data.refcounted = {g_bytes + 5, 7}},
     {.refcount = &g_refcnts[40].base, .data.refcounted = {g_bytes + 424, 3}}},
    {{.refcount = &g_refcnts[1].base, .data.refcounted = {g_bytes + 5, 7}},
     {.refcount = &g_refcnts[41].base, .data.refcounted = {g_bytes + 427, 3}}},
    {{.refcount = &g_refcnts[0].base, .data.refcounted = {g_bytes + 0, 5}},
     {.refcount = &g_refcnts[42].base, .data.refcounted = {g_bytes + 430, 1}}},
    {{.refcount = &g_refcnts[0].base, .data.refcounted = {g_bytes + 0, 5}},
     {.refcount = &g_refcnts[43].base, .data.refcounted = {g_bytes + 431, 11}}},
    {{.refcount = &g_refcnts[2].base, .data.refcounted = {g_bytes + 12, 7}},
     {.refcount = &g_refcnts[44].base, .data.refcounted = {g_bytes + 442, 3}}},
    {{.refcount = &g_refcnts[2].base, .data.refcounted = {g_bytes + 12, 7}},
     {.refcount = &g_refcnts[45].base, .data.refcounted = {g_bytes + 445, 3}}},
    {{.refcount = &g_refcnts[2].base, .data.refcounted = {g_bytes + 12, 7}},
     {.refcount = &g_refcnts[46].base, .data.refcounted = {g_bytes + 448, 3}}},
    {{.refcount = &g_refcnts[2].base, .data.refcounted = {g_bytes + 12, 7}},
     {.refcount = &g_refcnts[47].base, .data.refcounted = {g_bytes + 451, 3}}},
    {{.refcount = &g_refcnts[2].base, .data.refcounted = {g_bytes + 12, 7}},
     {.refcount = &g_refcnts[48].base, .data.refcounted = {g_bytes + 454, 3}}},
    {{.refcount = &g_refcnts[49].base, .data.refcounted = {g_bytes + 457, 14}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[50].base, .data.refcounted = {g_bytes + 471, 15}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[50].base, .data.refcounted = {g_bytes + 471, 15}},
     {.refcount = &g_refcnts[51].base, .data.refcounted = {g_bytes + 486, 13}}},
    {{.refcount = &g_refcnts[52].base, .data.refcounted = {g_bytes + 499, 15}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[53].base, .data.refcounted = {g_bytes + 514, 13}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[54].base, .data.refcounted = {g_bytes + 527, 6}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[55].base, .data.refcounted = {g_bytes + 533, 27}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[56].base, .data.refcounted = {g_bytes + 560, 3}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[57].base, .data.refcounted = {g_bytes + 563, 5}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[58].base, .data.refcounted = {g_bytes + 568, 13}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[59].base, .data.refcounted = {g_bytes + 581, 13}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[60].base, .data.refcounted = {g_bytes + 594, 19}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[61].base, .data.refcounted = {g_bytes + 613, 16}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[62].base, .data.refcounted = {g_bytes + 629, 16}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[63].base, .data.refcounted = {g_bytes + 645, 14}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[64].base, .data.refcounted = {g_bytes + 659, 16}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[65].base, .data.refcounted = {g_bytes + 675, 13}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[11].base, .data.refcounted = {g_bytes + 110, 12}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[66].base, .data.refcounted = {g_bytes + 688, 6}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[67].base, .data.refcounted = {g_bytes + 694, 4}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[68].base, .data.refcounted = {g_bytes + 698, 4}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[69].base, .data.refcounted = {g_bytes + 702, 6}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[70].base, .data.refcounted = {g_bytes + 708, 7}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[71].base, .data.refcounted = {g_bytes + 715, 4}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[14].base, .data.refcounted = {g_bytes + 162, 4}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[72].base, .data.refcounted = {g_bytes + 719, 8}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[73].base, .data.refcounted = {g_bytes + 727, 17}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[74].base, .data.refcounted = {g_bytes + 744, 13}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[75].base, .data.refcounted = {g_bytes + 757, 8}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[76].base, .data.refcounted = {g_bytes + 765, 19}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[77].base, .data.refcounted = {g_bytes + 784, 13}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[15].base, .data.refcounted = {g_bytes + 166, 8}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[16].base, .data.refcounted = {g_bytes + 174, 11}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[78].base, .data.refcounted = {g_bytes + 797, 4}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[79].base, .data.refcounted = {g_bytes + 801, 8}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[80].base, .data.refcounted = {g_bytes + 809, 12}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[81].base, .data.refcounted = {g_bytes + 821, 18}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[82].base, .data.refcounted = {g_bytes + 839, 19}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[83].base, .data.refcounted = {g_bytes + 858, 5}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[84].base, .data.refcounted = {g_bytes + 863, 7}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[85].base, .data.refcounted = {g_bytes + 870, 7}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[86].base, .data.refcounted = {g_bytes + 877, 11}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[87].base, .data.refcounted = {g_bytes + 888, 6}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[88].base, .data.refcounted = {g_bytes + 894, 10}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[89].base, .data.refcounted = {g_bytes + 904, 25}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[90].base, .data.refcounted = {g_bytes + 929, 17}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[13].base, .data.refcounted = {g_bytes + 152, 10}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[91].base, .data.refcounted = {g_bytes + 946, 4}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[92].base, .data.refcounted = {g_bytes + 950, 3}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[93].base, .data.refcounted = {g_bytes + 953, 16}},
     {.refcount = &g_refcnts[20].base, .data.refcounted = {g_bytes + 227, 0}}},
    {{.refcount = &g_refcnts[10].base, .data.refcounted = {g_bytes + 90, 20}},
     {.refcount = &g_refcnts[29].base, .data.refcounted = {g_bytes + 358, 8}}},
    {{.refcount = &g_refcnts[10].base, .data.refcounted = {g_bytes + 90, 20}},
     {.refcount = &g_refcnts[31].base, .data.refcounted = {g_bytes + 370, 7}}},
    {{.refcount = &g_refcnts[10].base, .data.refcounted = {g_bytes + 90, 20}},
     {.refcount = &g_refcnts[94].base, .data.refcounted = {g_bytes + 969, 16}}},
    {{.refcount = &g_refcnts[10].base, .data.refcounted = {g_bytes + 90, 20}},
     {.refcount = &g_refcnts[30].base, .data.refcounted = {g_bytes + 366, 4}}},
    {{.refcount = &g_refcnts[10].base, .data.refcounted = {g_bytes + 90, 20}},
     {.refcount = &g_refcnts[95].base, .data.refcounted = {g_bytes + 985, 13}}},
    {{.refcount = &g_refcnts[10].base, .data.refcounted = {g_bytes + 90, 20}},
     {.refcount = &g_refcnts[96].base, .data.refcounted = {g_bytes + 998, 12}}},
    {{.refcount = &g_refcnts[10].base, .data.refcounted = {g_bytes + 90, 20}},
     {.refcount = &g_refcnts[97].base,
      .data.refcounted = {g_bytes + 1010, 21}}},
};
#define BATCH_PHASHLEN 0x10
#define BATCH_PHASHNKEYS 17
#define BATCH_PHASHRANGE 32
#define BATCH_PHASHSALT 0x9e3779b9

static const uint8_t batch_tab[] = {
    0, 13, 0, 13, 0, 13, 0, 13, 0, 13, 0, 15, 0, 13, 0, 23,
};

static uint32_t batch_phash(uint32_t val) {
  val += (uint32_t)0;

  uint32_t a, b, rsl;

  b = (val & 0xf);
  a = ((val << 27) >> 28);
  rsl = (a ^ batch_tab[b]);
  return rsl;
}

static const uint8_t batch_hash_to_idx[] = {
    0,  2, 4, 6, 8, 10, 12, 14, 16, 9, 11, 13, 3, 1, 7, 5,
    15, 0, 0, 0, 0, 0,  0,  0,  0,  0, 0,  0,  0, 0, 0, 0};

grpc_metadata_batch_callouts_index grpc_batch_index_of(grpc_slice slice) {
  if (!grpc_is_static_metadata_string(slice)) return GRPC_BATCH_CALLOUTS_COUNT;
  uint32_t idx = (uint32_t)grpc_static_metadata_index(slice);
  uint32_t hash = batch_phash(idx);
  if (hash < GPR_ARRAY_SIZE(batch_hash_to_idx) &&
      batch_hash_to_idx[hash] == idx)
    return (grpc_metadata_batch_callouts_index)hash;
  return GRPC_BATCH_CALLOUTS_COUNT;
}

const uint8_t grpc_static_accept_encoding_metadata[8] = {0,  74, 75, 76,
                                                         77, 78, 79, 80};
