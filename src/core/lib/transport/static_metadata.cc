/*
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
 */

/*
 * WARNING: Auto-generated code.
 *
 * To make changes to this file, change
 * tools/codegen/core/gen_static_metadata.py, and then re-run it.
 *
 * See metadata.h for an explanation of the interface here, and metadata.cc for
 * an explanation of what's going on.
 */

#include <grpc/support/port_platform.h>

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
    111, 100, 105, 110, 103, 103, 114, 112, 99,  45,  115, 101, 114, 118, 101,
    114, 45,  115, 116, 97,  116, 115, 45,  98,  105, 110, 103, 114, 112, 99,
    45,  116, 97,  103, 115, 45,  98,  105, 110, 103, 114, 112, 99,  45,  116,
    114, 97,  99,  101, 45,  98,  105, 110, 99,  111, 110, 116, 101, 110, 116,
    45,  116, 121, 112, 101, 99,  111, 110, 116, 101, 110, 116, 45,  101, 110,
    99,  111, 100, 105, 110, 103, 97,  99,  99,  101, 112, 116, 45,  101, 110,
    99,  111, 100, 105, 110, 103, 103, 114, 112, 99,  45,  105, 110, 116, 101,
    114, 110, 97,  108, 45,  101, 110, 99,  111, 100, 105, 110, 103, 45,  114,
    101, 113, 117, 101, 115, 116, 103, 114, 112, 99,  45,  105, 110, 116, 101,
    114, 110, 97,  108, 45,  115, 116, 114, 101, 97,  109, 45,  101, 110, 99,
    111, 100, 105, 110, 103, 45,  114, 101, 113, 117, 101, 115, 116, 117, 115,
    101, 114, 45,  97,  103, 101, 110, 116, 104, 111, 115, 116, 103, 114, 112,
    99,  108, 98,  95,  99,  108, 105, 101, 110, 116, 95,  115, 116, 97,  116,
    115, 103, 114, 112, 99,  45,  112, 114, 101, 118, 105, 111, 117, 115, 45,
    114, 112, 99,  45,  97,  116, 116, 101, 109, 112, 116, 115, 103, 114, 112,
    99,  45,  114, 101, 116, 114, 121, 45,  112, 117, 115, 104, 98,  97,  99,
    107, 45,  109, 115, 103, 114, 112, 99,  45,  116, 105, 109, 101, 111, 117,
    116, 49,  50,  51,  52,  103, 114, 112, 99,  46,  119, 97,  105, 116, 95,
    102, 111, 114, 95,  114, 101, 97,  100, 121, 103, 114, 112, 99,  46,  116,
    105, 109, 101, 111, 117, 116, 103, 114, 112, 99,  46,  109, 97,  120, 95,
    114, 101, 113, 117, 101, 115, 116, 95,  109, 101, 115, 115, 97,  103, 101,
    95,  98,  121, 116, 101, 115, 103, 114, 112, 99,  46,  109, 97,  120, 95,
    114, 101, 115, 112, 111, 110, 115, 101, 95,  109, 101, 115, 115, 97,  103,
    101, 95,  98,  121, 116, 101, 115, 47,  103, 114, 112, 99,  46,  108, 98,
    46,  118, 49,  46,  76,  111, 97,  100, 66,  97,  108, 97,  110, 99,  101,
    114, 47,  66,  97,  108, 97,  110, 99,  101, 76,  111, 97,  100, 47,  103,
    114, 112, 99,  46,  104, 101, 97,  108, 116, 104, 46,  118, 49,  46,  72,
    101, 97,  108, 116, 104, 47,  87,  97,  116, 99,  104, 47,  101, 110, 118,
    111, 121, 46,  115, 101, 114, 118, 105, 99,  101, 46,  100, 105, 115, 99,
    111, 118, 101, 114, 121, 46,  118, 50,  46,  65,  103, 103, 114, 101, 103,
    97,  116, 101, 100, 68,  105, 115, 99,  111, 118, 101, 114, 121, 83,  101,
    114, 118, 105, 99,  101, 47,  83,  116, 114, 101, 97,  109, 65,  103, 103,
    114, 101, 103, 97,  116, 101, 100, 82,  101, 115, 111, 117, 114, 99,  101,
    115, 100, 101, 102, 108, 97,  116, 101, 103, 122, 105, 112, 115, 116, 114,
    101, 97,  109, 47,  103, 122, 105, 112, 71,  69,  84,  80,  79,  83,  84,
    47,  47,  105, 110, 100, 101, 120, 46,  104, 116, 109, 108, 104, 116, 116,
    112, 104, 116, 116, 112, 115, 50,  48,  48,  50,  48,  52,  50,  48,  54,
    51,  48,  52,  52,  48,  48,  52,  48,  52,  53,  48,  48,  97,  99,  99,
    101, 112, 116, 45,  99,  104, 97,  114, 115, 101, 116, 103, 122, 105, 112,
    44,  32,  100, 101, 102, 108, 97,  116, 101, 97,  99,  99,  101, 112, 116,
    45,  108, 97,  110, 103, 117, 97,  103, 101, 97,  99,  99,  101, 112, 116,
    45,  114, 97,  110, 103, 101, 115, 97,  99,  99,  101, 112, 116, 97,  99,
    99,  101, 115, 115, 45,  99,  111, 110, 116, 114, 111, 108, 45,  97,  108,
    108, 111, 119, 45,  111, 114, 105, 103, 105, 110, 97,  103, 101, 97,  108,
    108, 111, 119, 97,  117, 116, 104, 111, 114, 105, 122, 97,  116, 105, 111,
    110, 99,  97,  99,  104, 101, 45,  99,  111, 110, 116, 114, 111, 108, 99,
    111, 110, 116, 101, 110, 116, 45,  100, 105, 115, 112, 111, 115, 105, 116,
    105, 111, 110, 99,  111, 110, 116, 101, 110, 116, 45,  108, 97,  110, 103,
    117, 97,  103, 101, 99,  111, 110, 116, 101, 110, 116, 45,  108, 101, 110,
    103, 116, 104, 99,  111, 110, 116, 101, 110, 116, 45,  108, 111, 99,  97,
    116, 105, 111, 110, 99,  111, 110, 116, 101, 110, 116, 45,  114, 97,  110,
    103, 101, 99,  111, 111, 107, 105, 101, 100, 97,  116, 101, 101, 116, 97,
    103, 101, 120, 112, 101, 99,  116, 101, 120, 112, 105, 114, 101, 115, 102,
    114, 111, 109, 105, 102, 45,  109, 97,  116, 99,  104, 105, 102, 45,  109,
    111, 100, 105, 102, 105, 101, 100, 45,  115, 105, 110, 99,  101, 105, 102,
    45,  110, 111, 110, 101, 45,  109, 97,  116, 99,  104, 105, 102, 45,  114,
    97,  110, 103, 101, 105, 102, 45,  117, 110, 109, 111, 100, 105, 102, 105,
    101, 100, 45,  115, 105, 110, 99,  101, 108, 97,  115, 116, 45,  109, 111,
    100, 105, 102, 105, 101, 100, 108, 105, 110, 107, 108, 111, 99,  97,  116,
    105, 111, 110, 109, 97,  120, 45,  102, 111, 114, 119, 97,  114, 100, 115,
    112, 114, 111, 120, 121, 45,  97,  117, 116, 104, 101, 110, 116, 105, 99,
    97,  116, 101, 112, 114, 111, 120, 121, 45,  97,  117, 116, 104, 111, 114,
    105, 122, 97,  116, 105, 111, 110, 114, 97,  110, 103, 101, 114, 101, 102,
    101, 114, 101, 114, 114, 101, 102, 114, 101, 115, 104, 114, 101, 116, 114,
    121, 45,  97,  102, 116, 101, 114, 115, 101, 114, 118, 101, 114, 115, 101,
    116, 45,  99,  111, 111, 107, 105, 101, 115, 116, 114, 105, 99,  116, 45,
    116, 114, 97,  110, 115, 112, 111, 114, 116, 45,  115, 101, 99,  117, 114,
    105, 116, 121, 116, 114, 97,  110, 115, 102, 101, 114, 45,  101, 110, 99,
    111, 100, 105, 110, 103, 118, 97,  114, 121, 118, 105, 97,  119, 119, 119,
    45,  97,  117, 116, 104, 101, 110, 116, 105, 99,  97,  116, 101, 48,  105,
    100, 101, 110, 116, 105, 116, 121, 116, 114, 97,  105, 108, 101, 114, 115,
    97,  112, 112, 108, 105, 99,  97,  116, 105, 111, 110, 47,  103, 114, 112,
    99,  103, 114, 112, 99,  80,  85,  84,  108, 98,  45,  99,  111, 115, 116,
    45,  98,  105, 110, 105, 100, 101, 110, 116, 105, 116, 121, 44,  100, 101,
    102, 108, 97,  116, 101, 105, 100, 101, 110, 116, 105, 116, 121, 44,  103,
    122, 105, 112, 100, 101, 102, 108, 97,  116, 101, 44,  103, 122, 105, 112,
    105, 100, 101, 110, 116, 105, 116, 121, 44,  100, 101, 102, 108, 97,  116,
    101, 44,  103, 122, 105, 112};

static grpc_slice_refcount static_sub_refcnt;
grpc_slice_refcount grpc_static_metadata_refcounts[GRPC_STATIC_MDSTR_COUNT] = {
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
    grpc_slice_refcount(&static_sub_refcnt, grpc_slice_refcount::Type::STATIC),
};

const grpc_slice grpc_static_slice_table[GRPC_STATIC_MDSTR_COUNT] = {
    {&grpc_static_metadata_refcounts[0], {{5, g_bytes + 0}}},
    {&grpc_static_metadata_refcounts[1], {{7, g_bytes + 5}}},
    {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
    {&grpc_static_metadata_refcounts[3], {{10, g_bytes + 19}}},
    {&grpc_static_metadata_refcounts[4], {{7, g_bytes + 29}}},
    {&grpc_static_metadata_refcounts[5], {{2, g_bytes + 36}}},
    {&grpc_static_metadata_refcounts[6], {{12, g_bytes + 38}}},
    {&grpc_static_metadata_refcounts[7], {{11, g_bytes + 50}}},
    {&grpc_static_metadata_refcounts[8], {{16, g_bytes + 61}}},
    {&grpc_static_metadata_refcounts[9], {{13, g_bytes + 77}}},
    {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
    {&grpc_static_metadata_refcounts[11], {{21, g_bytes + 110}}},
    {&grpc_static_metadata_refcounts[12], {{13, g_bytes + 131}}},
    {&grpc_static_metadata_refcounts[13], {{14, g_bytes + 144}}},
    {&grpc_static_metadata_refcounts[14], {{12, g_bytes + 158}}},
    {&grpc_static_metadata_refcounts[15], {{16, g_bytes + 170}}},
    {&grpc_static_metadata_refcounts[16], {{15, g_bytes + 186}}},
    {&grpc_static_metadata_refcounts[17], {{30, g_bytes + 201}}},
    {&grpc_static_metadata_refcounts[18], {{37, g_bytes + 231}}},
    {&grpc_static_metadata_refcounts[19], {{10, g_bytes + 268}}},
    {&grpc_static_metadata_refcounts[20], {{4, g_bytes + 278}}},
    {&grpc_static_metadata_refcounts[21], {{19, g_bytes + 282}}},
    {&grpc_static_metadata_refcounts[22], {{26, g_bytes + 301}}},
    {&grpc_static_metadata_refcounts[23], {{22, g_bytes + 327}}},
    {&grpc_static_metadata_refcounts[24], {{12, g_bytes + 349}}},
    {&grpc_static_metadata_refcounts[25], {{1, g_bytes + 361}}},
    {&grpc_static_metadata_refcounts[26], {{1, g_bytes + 362}}},
    {&grpc_static_metadata_refcounts[27], {{1, g_bytes + 363}}},
    {&grpc_static_metadata_refcounts[28], {{1, g_bytes + 364}}},
    {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}},
    {&grpc_static_metadata_refcounts[30], {{19, g_bytes + 365}}},
    {&grpc_static_metadata_refcounts[31], {{12, g_bytes + 384}}},
    {&grpc_static_metadata_refcounts[32], {{30, g_bytes + 396}}},
    {&grpc_static_metadata_refcounts[33], {{31, g_bytes + 426}}},
    {&grpc_static_metadata_refcounts[34], {{36, g_bytes + 457}}},
    {&grpc_static_metadata_refcounts[35], {{28, g_bytes + 493}}},
    {&grpc_static_metadata_refcounts[36], {{80, g_bytes + 521}}},
    {&grpc_static_metadata_refcounts[37], {{7, g_bytes + 601}}},
    {&grpc_static_metadata_refcounts[38], {{4, g_bytes + 608}}},
    {&grpc_static_metadata_refcounts[39], {{11, g_bytes + 612}}},
    {&grpc_static_metadata_refcounts[40], {{3, g_bytes + 623}}},
    {&grpc_static_metadata_refcounts[41], {{4, g_bytes + 626}}},
    {&grpc_static_metadata_refcounts[42], {{1, g_bytes + 630}}},
    {&grpc_static_metadata_refcounts[43], {{11, g_bytes + 631}}},
    {&grpc_static_metadata_refcounts[44], {{4, g_bytes + 642}}},
    {&grpc_static_metadata_refcounts[45], {{5, g_bytes + 646}}},
    {&grpc_static_metadata_refcounts[46], {{3, g_bytes + 651}}},
    {&grpc_static_metadata_refcounts[47], {{3, g_bytes + 654}}},
    {&grpc_static_metadata_refcounts[48], {{3, g_bytes + 657}}},
    {&grpc_static_metadata_refcounts[49], {{3, g_bytes + 660}}},
    {&grpc_static_metadata_refcounts[50], {{3, g_bytes + 663}}},
    {&grpc_static_metadata_refcounts[51], {{3, g_bytes + 666}}},
    {&grpc_static_metadata_refcounts[52], {{3, g_bytes + 669}}},
    {&grpc_static_metadata_refcounts[53], {{14, g_bytes + 672}}},
    {&grpc_static_metadata_refcounts[54], {{13, g_bytes + 686}}},
    {&grpc_static_metadata_refcounts[55], {{15, g_bytes + 699}}},
    {&grpc_static_metadata_refcounts[56], {{13, g_bytes + 714}}},
    {&grpc_static_metadata_refcounts[57], {{6, g_bytes + 727}}},
    {&grpc_static_metadata_refcounts[58], {{27, g_bytes + 733}}},
    {&grpc_static_metadata_refcounts[59], {{3, g_bytes + 760}}},
    {&grpc_static_metadata_refcounts[60], {{5, g_bytes + 763}}},
    {&grpc_static_metadata_refcounts[61], {{13, g_bytes + 768}}},
    {&grpc_static_metadata_refcounts[62], {{13, g_bytes + 781}}},
    {&grpc_static_metadata_refcounts[63], {{19, g_bytes + 794}}},
    {&grpc_static_metadata_refcounts[64], {{16, g_bytes + 813}}},
    {&grpc_static_metadata_refcounts[65], {{14, g_bytes + 829}}},
    {&grpc_static_metadata_refcounts[66], {{16, g_bytes + 843}}},
    {&grpc_static_metadata_refcounts[67], {{13, g_bytes + 859}}},
    {&grpc_static_metadata_refcounts[68], {{6, g_bytes + 872}}},
    {&grpc_static_metadata_refcounts[69], {{4, g_bytes + 878}}},
    {&grpc_static_metadata_refcounts[70], {{4, g_bytes + 882}}},
    {&grpc_static_metadata_refcounts[71], {{6, g_bytes + 886}}},
    {&grpc_static_metadata_refcounts[72], {{7, g_bytes + 892}}},
    {&grpc_static_metadata_refcounts[73], {{4, g_bytes + 899}}},
    {&grpc_static_metadata_refcounts[74], {{8, g_bytes + 903}}},
    {&grpc_static_metadata_refcounts[75], {{17, g_bytes + 911}}},
    {&grpc_static_metadata_refcounts[76], {{13, g_bytes + 928}}},
    {&grpc_static_metadata_refcounts[77], {{8, g_bytes + 941}}},
    {&grpc_static_metadata_refcounts[78], {{19, g_bytes + 949}}},
    {&grpc_static_metadata_refcounts[79], {{13, g_bytes + 968}}},
    {&grpc_static_metadata_refcounts[80], {{4, g_bytes + 981}}},
    {&grpc_static_metadata_refcounts[81], {{8, g_bytes + 985}}},
    {&grpc_static_metadata_refcounts[82], {{12, g_bytes + 993}}},
    {&grpc_static_metadata_refcounts[83], {{18, g_bytes + 1005}}},
    {&grpc_static_metadata_refcounts[84], {{19, g_bytes + 1023}}},
    {&grpc_static_metadata_refcounts[85], {{5, g_bytes + 1042}}},
    {&grpc_static_metadata_refcounts[86], {{7, g_bytes + 1047}}},
    {&grpc_static_metadata_refcounts[87], {{7, g_bytes + 1054}}},
    {&grpc_static_metadata_refcounts[88], {{11, g_bytes + 1061}}},
    {&grpc_static_metadata_refcounts[89], {{6, g_bytes + 1072}}},
    {&grpc_static_metadata_refcounts[90], {{10, g_bytes + 1078}}},
    {&grpc_static_metadata_refcounts[91], {{25, g_bytes + 1088}}},
    {&grpc_static_metadata_refcounts[92], {{17, g_bytes + 1113}}},
    {&grpc_static_metadata_refcounts[93], {{4, g_bytes + 1130}}},
    {&grpc_static_metadata_refcounts[94], {{3, g_bytes + 1134}}},
    {&grpc_static_metadata_refcounts[95], {{16, g_bytes + 1137}}},
    {&grpc_static_metadata_refcounts[96], {{1, g_bytes + 1153}}},
    {&grpc_static_metadata_refcounts[97], {{8, g_bytes + 1154}}},
    {&grpc_static_metadata_refcounts[98], {{8, g_bytes + 1162}}},
    {&grpc_static_metadata_refcounts[99], {{16, g_bytes + 1170}}},
    {&grpc_static_metadata_refcounts[100], {{4, g_bytes + 1186}}},
    {&grpc_static_metadata_refcounts[101], {{3, g_bytes + 1190}}},
    {&grpc_static_metadata_refcounts[102], {{11, g_bytes + 1193}}},
    {&grpc_static_metadata_refcounts[103], {{16, g_bytes + 1204}}},
    {&grpc_static_metadata_refcounts[104], {{13, g_bytes + 1220}}},
    {&grpc_static_metadata_refcounts[105], {{12, g_bytes + 1233}}},
    {&grpc_static_metadata_refcounts[106], {{21, g_bytes + 1245}}},
};

uintptr_t grpc_static_mdelem_user_data[GRPC_STATIC_MDELEM_COUNT] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 4, 4, 6, 6, 8, 8, 2, 4, 4};

static const int8_t elems_r[] = {
    15, 10,  -8, 0,  2,  -42, -81, -43, 0,   6,  -8,  0,   0,   0,   2,
    -3, -10, 0,  0,  1,  0,   0,   0,   0,   0,  0,   0,   0,   0,   0,
    0,  0,   0,  0,  0,  0,   0,   0,   0,   0,  0,   0,   0,   0,   0,
    0,  0,   0,  0,  0,  0,   0,   0,   -65, 0,  -68, -69, -70, -52, 0,
    32, 31,  31, 30, 29, 28,  27,  26,  25,  24, 23,  22,  21,  20,  19,
    18, 17,  16, 16, 15, 14,  13,  12,  11,  10, 9,   8,   7,   6,   5,
    4,  3,   2,  3,  2,  2,   6,   0,   0,   0,  0,   0,   0,   -6,  0};
static uint32_t elems_phash(uint32_t i) {
  i -= 42;
  uint32_t x = i % 105;
  uint32_t y = i / 105;
  uint32_t h = x;
  if (y < GPR_ARRAY_SIZE(elems_r)) {
    uint32_t delta = (uint32_t)elems_r[y];
    h += delta;
  }
  return h;
}

static const uint16_t elem_keys[] = {
    260,   261,  262,  263,  264,  265,  266,   1107, 1108,  1741, 147,  148,
    472,   473,  1634, 42,   43,   1527, 1750,  1000, 1001,  774,  775,  1643,
    633,   845,  2062, 2169, 5700, 5914, 6021,  6128, 6342,  6449, 1766, 6556,
    6663,  6770, 6877, 6984, 7091, 7198, 7305,  7412, 7519,  7626, 7733, 7840,
    7947,  8054, 8161, 6235, 8268, 8375, 8482,  8589, 8696,  8803, 8910, 9017,
    9124,  9231, 9338, 9445, 9552, 9659, 9766,  1167, 528,   9873, 9980, 208,
    10087, 1173, 1174, 1175, 1176, 1809, 10194, 1060, 10943, 0,    1702, 0,
    1816,  0,    0,    1597, 0,    0,    350,   0,    0,     0,    0,    0,
    0,     0,    0,    0,    0,    0,    0,     0,    0,     0,    0,    0,
    0,     0,    0,    0,    0,    0,    0,     0,    0,     0,    0,    0,
    0,     0,    0,    0,    0,    0,    0,     0,    0,     0,    0,    0,
    0,     0,    0,    0};
static const uint8_t elem_idxs[] = {
    7,  8,   9,  10,  11, 12,  13,  76, 78,  71,  1,  2,  5,  6,  25, 3,
    4,  30,  83, 66,  65, 62,  63,  73, 67,  61,  57, 37, 14, 16, 17, 18,
    20, 21,  15, 22,  23, 24,  26,  27, 28,  29,  31, 32, 33, 34, 35, 36,
    38, 39,  40, 19,  41, 42,  43,  44, 45,  46,  47, 48, 49, 50, 51, 52,
    53, 54,  55, 75,  69, 56,  58,  70, 59,  77,  79, 80, 81, 82, 60, 64,
    74, 255, 72, 255, 84, 255, 255, 68, 255, 255, 0};

grpc_mdelem grpc_static_mdelem_for_static_strings(intptr_t a, intptr_t b) {
  if (a == -1 || b == -1) return GRPC_MDNULL;
  uint32_t k = static_cast<uint32_t>(a * 107 + b);
  uint32_t h = elems_phash(k);
  return h < GPR_ARRAY_SIZE(elem_keys) && elem_keys[h] == k &&
                 elem_idxs[h] != 255
             ? GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[elem_idxs[h]].data(),
                                GRPC_MDELEM_STORAGE_STATIC)
             : GRPC_MDNULL;
}

grpc_core::StaticMetadata grpc_static_mdelem_table[GRPC_STATIC_MDELEM_COUNT] = {
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[3], {{10, g_bytes + 19}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 0),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[1], {{7, g_bytes + 5}}},
        {&grpc_static_metadata_refcounts[40], {{3, g_bytes + 623}}}, 1),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[1], {{7, g_bytes + 5}}},
        {&grpc_static_metadata_refcounts[41], {{4, g_bytes + 626}}}, 2),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[0], {{5, g_bytes + 0}}},
        {&grpc_static_metadata_refcounts[42], {{1, g_bytes + 630}}}, 3),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[0], {{5, g_bytes + 0}}},
        {&grpc_static_metadata_refcounts[43], {{11, g_bytes + 631}}}, 4),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[4], {{7, g_bytes + 29}}},
        {&grpc_static_metadata_refcounts[44], {{4, g_bytes + 642}}}, 5),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[4], {{7, g_bytes + 29}}},
        {&grpc_static_metadata_refcounts[45], {{5, g_bytes + 646}}}, 6),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[46], {{3, g_bytes + 651}}}, 7),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[47], {{3, g_bytes + 654}}}, 8),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[48], {{3, g_bytes + 657}}}, 9),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[49], {{3, g_bytes + 660}}}, 10),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[50], {{3, g_bytes + 663}}}, 11),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[51], {{3, g_bytes + 666}}}, 12),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[52], {{3, g_bytes + 669}}}, 13),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[53], {{14, g_bytes + 672}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 14),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[16], {{15, g_bytes + 186}}},
        {&grpc_static_metadata_refcounts[54], {{13, g_bytes + 686}}}, 15),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[55], {{15, g_bytes + 699}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 16),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[56], {{13, g_bytes + 714}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 17),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[57], {{6, g_bytes + 727}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 18),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[58], {{27, g_bytes + 733}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 19),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[59], {{3, g_bytes + 760}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 20),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[60], {{5, g_bytes + 763}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 21),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[61], {{13, g_bytes + 768}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 22),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[62], {{13, g_bytes + 781}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 23),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[63], {{19, g_bytes + 794}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 24),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[15], {{16, g_bytes + 170}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 25),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[64], {{16, g_bytes + 813}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 26),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[65], {{14, g_bytes + 829}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 27),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[66], {{16, g_bytes + 843}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 28),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[67], {{13, g_bytes + 859}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 29),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[14], {{12, g_bytes + 158}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 30),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[68], {{6, g_bytes + 872}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 31),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[69], {{4, g_bytes + 878}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 32),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[70], {{4, g_bytes + 882}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 33),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[71], {{6, g_bytes + 886}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 34),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[72], {{7, g_bytes + 892}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 35),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[73], {{4, g_bytes + 899}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 36),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[20], {{4, g_bytes + 278}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 37),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[74], {{8, g_bytes + 903}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 38),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[75], {{17, g_bytes + 911}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 39),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[76], {{13, g_bytes + 928}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 40),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[77], {{8, g_bytes + 941}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 41),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[78], {{19, g_bytes + 949}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 42),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[79], {{13, g_bytes + 968}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 43),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[80], {{4, g_bytes + 981}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 44),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[81], {{8, g_bytes + 985}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 45),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[82], {{12, g_bytes + 993}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 46),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[83], {{18, g_bytes + 1005}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 47),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[84], {{19, g_bytes + 1023}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 48),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[85], {{5, g_bytes + 1042}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 49),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[86], {{7, g_bytes + 1047}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 50),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[87], {{7, g_bytes + 1054}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 51),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[88], {{11, g_bytes + 1061}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 52),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[89], {{6, g_bytes + 1072}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 53),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[90], {{10, g_bytes + 1078}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 54),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[91], {{25, g_bytes + 1088}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 55),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[92], {{17, g_bytes + 1113}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 56),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[19], {{10, g_bytes + 268}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 57),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[93], {{4, g_bytes + 1130}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 58),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[94], {{3, g_bytes + 1134}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 59),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[95], {{16, g_bytes + 1137}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 60),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[7], {{11, g_bytes + 50}}},
        {&grpc_static_metadata_refcounts[96], {{1, g_bytes + 1153}}}, 61),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[7], {{11, g_bytes + 50}}},
        {&grpc_static_metadata_refcounts[25], {{1, g_bytes + 361}}}, 62),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[7], {{11, g_bytes + 50}}},
        {&grpc_static_metadata_refcounts[26], {{1, g_bytes + 362}}}, 63),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[9], {{13, g_bytes + 77}}},
        {&grpc_static_metadata_refcounts[97], {{8, g_bytes + 1154}}}, 64),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[9], {{13, g_bytes + 77}}},
        {&grpc_static_metadata_refcounts[38], {{4, g_bytes + 608}}}, 65),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[9], {{13, g_bytes + 77}}},
        {&grpc_static_metadata_refcounts[37], {{7, g_bytes + 601}}}, 66),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[5], {{2, g_bytes + 36}}},
        {&grpc_static_metadata_refcounts[98], {{8, g_bytes + 1162}}}, 67),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[14], {{12, g_bytes + 158}}},
        {&grpc_static_metadata_refcounts[99], {{16, g_bytes + 1170}}}, 68),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[4], {{7, g_bytes + 29}}},
        {&grpc_static_metadata_refcounts[100], {{4, g_bytes + 1186}}}, 69),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[1], {{7, g_bytes + 5}}},
        {&grpc_static_metadata_refcounts[101], {{3, g_bytes + 1190}}}, 70),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[16], {{15, g_bytes + 186}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 71),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[15], {{16, g_bytes + 170}}},
        {&grpc_static_metadata_refcounts[97], {{8, g_bytes + 1154}}}, 72),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[15], {{16, g_bytes + 170}}},
        {&grpc_static_metadata_refcounts[38], {{4, g_bytes + 608}}}, 73),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[102], {{11, g_bytes + 1193}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 365}}}, 74),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[97], {{8, g_bytes + 1154}}}, 75),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[37], {{7, g_bytes + 601}}}, 76),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[103], {{16, g_bytes + 1204}}}, 77),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[38], {{4, g_bytes + 608}}}, 78),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[104], {{13, g_bytes + 1220}}}, 79),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[105], {{12, g_bytes + 1233}}}, 80),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[106], {{21, g_bytes + 1245}}}, 81),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[16], {{15, g_bytes + 186}}},
        {&grpc_static_metadata_refcounts[97], {{8, g_bytes + 1154}}}, 82),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[16], {{15, g_bytes + 186}}},
        {&grpc_static_metadata_refcounts[38], {{4, g_bytes + 608}}}, 83),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[16], {{15, g_bytes + 186}}},
        {&grpc_static_metadata_refcounts[104], {{13, g_bytes + 1220}}}, 84),
};
const uint8_t grpc_static_accept_encoding_metadata[8] = {0,  75, 76, 77,
                                                         78, 79, 80, 81};

const uint8_t grpc_static_accept_stream_encoding_metadata[4] = {0, 82, 83, 84};
