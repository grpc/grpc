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
    101, 114, 45,  97,  103, 101, 110, 116, 104, 111, 115, 116, 108, 98,  45,
    116, 111, 107, 101, 110, 103, 114, 112, 99,  45,  112, 114, 101, 118, 105,
    111, 117, 115, 45,  114, 112, 99,  45,  97,  116, 116, 101, 109, 112, 116,
    115, 103, 114, 112, 99,  45,  114, 101, 116, 114, 121, 45,  112, 117, 115,
    104, 98,  97,  99,  107, 45,  109, 115, 103, 114, 112, 99,  45,  116, 105,
    109, 101, 111, 117, 116, 49,  50,  51,  52,  103, 114, 112, 99,  46,  119,
    97,  105, 116, 95,  102, 111, 114, 95,  114, 101, 97,  100, 121, 103, 114,
    112, 99,  46,  116, 105, 109, 101, 111, 117, 116, 103, 114, 112, 99,  46,
    109, 97,  120, 95,  114, 101, 113, 117, 101, 115, 116, 95,  109, 101, 115,
    115, 97,  103, 101, 95,  98,  121, 116, 101, 115, 103, 114, 112, 99,  46,
    109, 97,  120, 95,  114, 101, 115, 112, 111, 110, 115, 101, 95,  109, 101,
    115, 115, 97,  103, 101, 95,  98,  121, 116, 101, 115, 47,  103, 114, 112,
    99,  46,  108, 98,  46,  118, 49,  46,  76,  111, 97,  100, 66,  97,  108,
    97,  110, 99,  101, 114, 47,  66,  97,  108, 97,  110, 99,  101, 76,  111,
    97,  100, 47,  103, 114, 112, 99,  46,  104, 101, 97,  108, 116, 104, 46,
    118, 49,  46,  72,  101, 97,  108, 116, 104, 47,  87,  97,  116, 99,  104,
    47,  101, 110, 118, 111, 121, 46,  115, 101, 114, 118, 105, 99,  101, 46,
    100, 105, 115, 99,  111, 118, 101, 114, 121, 46,  118, 50,  46,  65,  103,
    103, 114, 101, 103, 97,  116, 101, 100, 68,  105, 115, 99,  111, 118, 101,
    114, 121, 83,  101, 114, 118, 105, 99,  101, 47,  83,  116, 114, 101, 97,
    109, 65,  103, 103, 114, 101, 103, 97,  116, 101, 100, 82,  101, 115, 111,
    117, 114, 99,  101, 115, 100, 101, 102, 108, 97,  116, 101, 103, 122, 105,
    112, 115, 116, 114, 101, 97,  109, 47,  103, 122, 105, 112, 71,  69,  84,
    80,  79,  83,  84,  47,  47,  105, 110, 100, 101, 120, 46,  104, 116, 109,
    108, 104, 116, 116, 112, 104, 116, 116, 112, 115, 50,  48,  48,  50,  48,
    52,  50,  48,  54,  51,  48,  52,  52,  48,  48,  52,  48,  52,  53,  48,
    48,  97,  99,  99,  101, 112, 116, 45,  99,  104, 97,  114, 115, 101, 116,
    103, 122, 105, 112, 44,  32,  100, 101, 102, 108, 97,  116, 101, 97,  99,
    99,  101, 112, 116, 45,  108, 97,  110, 103, 117, 97,  103, 101, 97,  99,
    99,  101, 112, 116, 45,  114, 97,  110, 103, 101, 115, 97,  99,  99,  101,
    112, 116, 97,  99,  99,  101, 115, 115, 45,  99,  111, 110, 116, 114, 111,
    108, 45,  97,  108, 108, 111, 119, 45,  111, 114, 105, 103, 105, 110, 97,
    103, 101, 97,  108, 108, 111, 119, 97,  117, 116, 104, 111, 114, 105, 122,
    97,  116, 105, 111, 110, 99,  97,  99,  104, 101, 45,  99,  111, 110, 116,
    114, 111, 108, 99,  111, 110, 116, 101, 110, 116, 45,  100, 105, 115, 112,
    111, 115, 105, 116, 105, 111, 110, 99,  111, 110, 116, 101, 110, 116, 45,
    108, 97,  110, 103, 117, 97,  103, 101, 99,  111, 110, 116, 101, 110, 116,
    45,  108, 101, 110, 103, 116, 104, 99,  111, 110, 116, 101, 110, 116, 45,
    108, 111, 99,  97,  116, 105, 111, 110, 99,  111, 110, 116, 101, 110, 116,
    45,  114, 97,  110, 103, 101, 99,  111, 111, 107, 105, 101, 100, 97,  116,
    101, 101, 116, 97,  103, 101, 120, 112, 101, 99,  116, 101, 120, 112, 105,
    114, 101, 115, 102, 114, 111, 109, 105, 102, 45,  109, 97,  116, 99,  104,
    105, 102, 45,  109, 111, 100, 105, 102, 105, 101, 100, 45,  115, 105, 110,
    99,  101, 105, 102, 45,  110, 111, 110, 101, 45,  109, 97,  116, 99,  104,
    105, 102, 45,  114, 97,  110, 103, 101, 105, 102, 45,  117, 110, 109, 111,
    100, 105, 102, 105, 101, 100, 45,  115, 105, 110, 99,  101, 108, 97,  115,
    116, 45,  109, 111, 100, 105, 102, 105, 101, 100, 108, 105, 110, 107, 108,
    111, 99,  97,  116, 105, 111, 110, 109, 97,  120, 45,  102, 111, 114, 119,
    97,  114, 100, 115, 112, 114, 111, 120, 121, 45,  97,  117, 116, 104, 101,
    110, 116, 105, 99,  97,  116, 101, 112, 114, 111, 120, 121, 45,  97,  117,
    116, 104, 111, 114, 105, 122, 97,  116, 105, 111, 110, 114, 97,  110, 103,
    101, 114, 101, 102, 101, 114, 101, 114, 114, 101, 102, 114, 101, 115, 104,
    114, 101, 116, 114, 121, 45,  97,  102, 116, 101, 114, 115, 101, 114, 118,
    101, 114, 115, 101, 116, 45,  99,  111, 111, 107, 105, 101, 115, 116, 114,
    105, 99,  116, 45,  116, 114, 97,  110, 115, 112, 111, 114, 116, 45,  115,
    101, 99,  117, 114, 105, 116, 121, 116, 114, 97,  110, 115, 102, 101, 114,
    45,  101, 110, 99,  111, 100, 105, 110, 103, 118, 97,  114, 121, 118, 105,
    97,  119, 119, 119, 45,  97,  117, 116, 104, 101, 110, 116, 105, 99,  97,
    116, 101, 48,  105, 100, 101, 110, 116, 105, 116, 121, 116, 114, 97,  105,
    108, 101, 114, 115, 97,  112, 112, 108, 105, 99,  97,  116, 105, 111, 110,
    47,  103, 114, 112, 99,  103, 114, 112, 99,  80,  85,  84,  108, 98,  45,
    99,  111, 115, 116, 45,  98,  105, 110, 105, 100, 101, 110, 116, 105, 116,
    121, 44,  100, 101, 102, 108, 97,  116, 101, 105, 100, 101, 110, 116, 105,
    116, 121, 44,  103, 122, 105, 112, 100, 101, 102, 108, 97,  116, 101, 44,
    103, 122, 105, 112, 105, 100, 101, 110, 116, 105, 116, 121, 44,  100, 101,
    102, 108, 97,  116, 101, 44,  103, 122, 105, 112};

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
    {&grpc_static_metadata_refcounts[21], {{8, g_bytes + 282}}},
    {&grpc_static_metadata_refcounts[22], {{26, g_bytes + 290}}},
    {&grpc_static_metadata_refcounts[23], {{22, g_bytes + 316}}},
    {&grpc_static_metadata_refcounts[24], {{12, g_bytes + 338}}},
    {&grpc_static_metadata_refcounts[25], {{1, g_bytes + 350}}},
    {&grpc_static_metadata_refcounts[26], {{1, g_bytes + 351}}},
    {&grpc_static_metadata_refcounts[27], {{1, g_bytes + 352}}},
    {&grpc_static_metadata_refcounts[28], {{1, g_bytes + 353}}},
    {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}},
    {&grpc_static_metadata_refcounts[30], {{19, g_bytes + 354}}},
    {&grpc_static_metadata_refcounts[31], {{12, g_bytes + 373}}},
    {&grpc_static_metadata_refcounts[32], {{30, g_bytes + 385}}},
    {&grpc_static_metadata_refcounts[33], {{31, g_bytes + 415}}},
    {&grpc_static_metadata_refcounts[34], {{36, g_bytes + 446}}},
    {&grpc_static_metadata_refcounts[35], {{28, g_bytes + 482}}},
    {&grpc_static_metadata_refcounts[36], {{80, g_bytes + 510}}},
    {&grpc_static_metadata_refcounts[37], {{7, g_bytes + 590}}},
    {&grpc_static_metadata_refcounts[38], {{4, g_bytes + 597}}},
    {&grpc_static_metadata_refcounts[39], {{11, g_bytes + 601}}},
    {&grpc_static_metadata_refcounts[40], {{3, g_bytes + 612}}},
    {&grpc_static_metadata_refcounts[41], {{4, g_bytes + 615}}},
    {&grpc_static_metadata_refcounts[42], {{1, g_bytes + 619}}},
    {&grpc_static_metadata_refcounts[43], {{11, g_bytes + 620}}},
    {&grpc_static_metadata_refcounts[44], {{4, g_bytes + 631}}},
    {&grpc_static_metadata_refcounts[45], {{5, g_bytes + 635}}},
    {&grpc_static_metadata_refcounts[46], {{3, g_bytes + 640}}},
    {&grpc_static_metadata_refcounts[47], {{3, g_bytes + 643}}},
    {&grpc_static_metadata_refcounts[48], {{3, g_bytes + 646}}},
    {&grpc_static_metadata_refcounts[49], {{3, g_bytes + 649}}},
    {&grpc_static_metadata_refcounts[50], {{3, g_bytes + 652}}},
    {&grpc_static_metadata_refcounts[51], {{3, g_bytes + 655}}},
    {&grpc_static_metadata_refcounts[52], {{3, g_bytes + 658}}},
    {&grpc_static_metadata_refcounts[53], {{14, g_bytes + 661}}},
    {&grpc_static_metadata_refcounts[54], {{13, g_bytes + 675}}},
    {&grpc_static_metadata_refcounts[55], {{15, g_bytes + 688}}},
    {&grpc_static_metadata_refcounts[56], {{13, g_bytes + 703}}},
    {&grpc_static_metadata_refcounts[57], {{6, g_bytes + 716}}},
    {&grpc_static_metadata_refcounts[58], {{27, g_bytes + 722}}},
    {&grpc_static_metadata_refcounts[59], {{3, g_bytes + 749}}},
    {&grpc_static_metadata_refcounts[60], {{5, g_bytes + 752}}},
    {&grpc_static_metadata_refcounts[61], {{13, g_bytes + 757}}},
    {&grpc_static_metadata_refcounts[62], {{13, g_bytes + 770}}},
    {&grpc_static_metadata_refcounts[63], {{19, g_bytes + 783}}},
    {&grpc_static_metadata_refcounts[64], {{16, g_bytes + 802}}},
    {&grpc_static_metadata_refcounts[65], {{14, g_bytes + 818}}},
    {&grpc_static_metadata_refcounts[66], {{16, g_bytes + 832}}},
    {&grpc_static_metadata_refcounts[67], {{13, g_bytes + 848}}},
    {&grpc_static_metadata_refcounts[68], {{6, g_bytes + 861}}},
    {&grpc_static_metadata_refcounts[69], {{4, g_bytes + 867}}},
    {&grpc_static_metadata_refcounts[70], {{4, g_bytes + 871}}},
    {&grpc_static_metadata_refcounts[71], {{6, g_bytes + 875}}},
    {&grpc_static_metadata_refcounts[72], {{7, g_bytes + 881}}},
    {&grpc_static_metadata_refcounts[73], {{4, g_bytes + 888}}},
    {&grpc_static_metadata_refcounts[74], {{8, g_bytes + 892}}},
    {&grpc_static_metadata_refcounts[75], {{17, g_bytes + 900}}},
    {&grpc_static_metadata_refcounts[76], {{13, g_bytes + 917}}},
    {&grpc_static_metadata_refcounts[77], {{8, g_bytes + 930}}},
    {&grpc_static_metadata_refcounts[78], {{19, g_bytes + 938}}},
    {&grpc_static_metadata_refcounts[79], {{13, g_bytes + 957}}},
    {&grpc_static_metadata_refcounts[80], {{4, g_bytes + 970}}},
    {&grpc_static_metadata_refcounts[81], {{8, g_bytes + 974}}},
    {&grpc_static_metadata_refcounts[82], {{12, g_bytes + 982}}},
    {&grpc_static_metadata_refcounts[83], {{18, g_bytes + 994}}},
    {&grpc_static_metadata_refcounts[84], {{19, g_bytes + 1012}}},
    {&grpc_static_metadata_refcounts[85], {{5, g_bytes + 1031}}},
    {&grpc_static_metadata_refcounts[86], {{7, g_bytes + 1036}}},
    {&grpc_static_metadata_refcounts[87], {{7, g_bytes + 1043}}},
    {&grpc_static_metadata_refcounts[88], {{11, g_bytes + 1050}}},
    {&grpc_static_metadata_refcounts[89], {{6, g_bytes + 1061}}},
    {&grpc_static_metadata_refcounts[90], {{10, g_bytes + 1067}}},
    {&grpc_static_metadata_refcounts[91], {{25, g_bytes + 1077}}},
    {&grpc_static_metadata_refcounts[92], {{17, g_bytes + 1102}}},
    {&grpc_static_metadata_refcounts[93], {{4, g_bytes + 1119}}},
    {&grpc_static_metadata_refcounts[94], {{3, g_bytes + 1123}}},
    {&grpc_static_metadata_refcounts[95], {{16, g_bytes + 1126}}},
    {&grpc_static_metadata_refcounts[96], {{1, g_bytes + 1142}}},
    {&grpc_static_metadata_refcounts[97], {{8, g_bytes + 1143}}},
    {&grpc_static_metadata_refcounts[98], {{8, g_bytes + 1151}}},
    {&grpc_static_metadata_refcounts[99], {{16, g_bytes + 1159}}},
    {&grpc_static_metadata_refcounts[100], {{4, g_bytes + 1175}}},
    {&grpc_static_metadata_refcounts[101], {{3, g_bytes + 1179}}},
    {&grpc_static_metadata_refcounts[102], {{11, g_bytes + 1182}}},
    {&grpc_static_metadata_refcounts[103], {{16, g_bytes + 1193}}},
    {&grpc_static_metadata_refcounts[104], {{13, g_bytes + 1209}}},
    {&grpc_static_metadata_refcounts[105], {{12, g_bytes + 1222}}},
    {&grpc_static_metadata_refcounts[106], {{21, g_bytes + 1234}}},
};

/* Warning: the core static metadata currently operates under the soft
constraint that the first GRPC_CHTTP2_LAST_STATIC_ENTRY (61) entries must
contain metadata specified by the http2 hpack standard. The CHTTP2 transport
reads the core metadata with this assumption in mind. If the order of the core
static metadata is to be changed, then the CHTTP2 transport must be changed as
well to stop relying on the core metadata. */

grpc_mdelem grpc_static_mdelem_manifested[GRPC_STATIC_MDELEM_COUNT] = {
    // clang-format off
    /* GRPC_MDELEM_AUTHORITY_EMPTY: 
     ":authority": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[0].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_METHOD_GET: 
     ":method": "GET" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[1].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_METHOD_POST: 
     ":method": "POST" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[2].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_PATH_SLASH: 
     ":path": "/" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[3].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_PATH_SLASH_INDEX_DOT_HTML: 
     ":path": "/index.html" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[4].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_SCHEME_HTTP: 
     ":scheme": "http" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[5].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_SCHEME_HTTPS: 
     ":scheme": "https" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[6].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_STATUS_200: 
     ":status": "200" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[7].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_STATUS_204: 
     ":status": "204" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[8].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_STATUS_206: 
     ":status": "206" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[9].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_STATUS_304: 
     ":status": "304" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[10].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_STATUS_400: 
     ":status": "400" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[11].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_STATUS_404: 
     ":status": "404" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[12].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_STATUS_500: 
     ":status": "500" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[13].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_ACCEPT_CHARSET_EMPTY: 
     "accept-charset": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[14].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_ACCEPT_ENCODING_GZIP_COMMA_DEFLATE: 
     "accept-encoding": "gzip, deflate" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[15].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_ACCEPT_LANGUAGE_EMPTY: 
     "accept-language": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[16].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_ACCEPT_RANGES_EMPTY: 
     "accept-ranges": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[17].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_ACCEPT_EMPTY: 
     "accept": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[18].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_ACCESS_CONTROL_ALLOW_ORIGIN_EMPTY: 
     "access-control-allow-origin": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[19].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_AGE_EMPTY: 
     "age": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[20].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_ALLOW_EMPTY: 
     "allow": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[21].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_AUTHORIZATION_EMPTY: 
     "authorization": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[22].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_CACHE_CONTROL_EMPTY: 
     "cache-control": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[23].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_CONTENT_DISPOSITION_EMPTY: 
     "content-disposition": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[24].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_CONTENT_ENCODING_EMPTY: 
     "content-encoding": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[25].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_CONTENT_LANGUAGE_EMPTY: 
     "content-language": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[26].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_CONTENT_LENGTH_EMPTY: 
     "content-length": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[27].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_CONTENT_LOCATION_EMPTY: 
     "content-location": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[28].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_CONTENT_RANGE_EMPTY: 
     "content-range": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[29].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_CONTENT_TYPE_EMPTY: 
     "content-type": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[30].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_COOKIE_EMPTY: 
     "cookie": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[31].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_DATE_EMPTY: 
     "date": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[32].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_ETAG_EMPTY: 
     "etag": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[33].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_EXPECT_EMPTY: 
     "expect": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[34].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_EXPIRES_EMPTY: 
     "expires": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[35].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_FROM_EMPTY: 
     "from": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[36].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_HOST_EMPTY: 
     "host": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[37].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_IF_MATCH_EMPTY: 
     "if-match": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[38].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_IF_MODIFIED_SINCE_EMPTY: 
     "if-modified-since": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[39].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_IF_NONE_MATCH_EMPTY: 
     "if-none-match": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[40].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_IF_RANGE_EMPTY: 
     "if-range": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[41].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_IF_UNMODIFIED_SINCE_EMPTY: 
     "if-unmodified-since": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[42].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_LAST_MODIFIED_EMPTY: 
     "last-modified": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[43].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_LINK_EMPTY: 
     "link": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[44].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_LOCATION_EMPTY: 
     "location": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[45].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_MAX_FORWARDS_EMPTY: 
     "max-forwards": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[46].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_PROXY_AUTHENTICATE_EMPTY: 
     "proxy-authenticate": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[47].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_PROXY_AUTHORIZATION_EMPTY: 
     "proxy-authorization": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[48].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_RANGE_EMPTY: 
     "range": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[49].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_REFERER_EMPTY: 
     "referer": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[50].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_REFRESH_EMPTY: 
     "refresh": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[51].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_RETRY_AFTER_EMPTY: 
     "retry-after": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[52].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_SERVER_EMPTY: 
     "server": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[53].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_SET_COOKIE_EMPTY: 
     "set-cookie": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[54].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_STRICT_TRANSPORT_SECURITY_EMPTY: 
     "strict-transport-security": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[55].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_TRANSFER_ENCODING_EMPTY: 
     "transfer-encoding": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[56].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_USER_AGENT_EMPTY: 
     "user-agent": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[57].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_VARY_EMPTY: 
     "vary": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[58].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_VIA_EMPTY: 
     "via": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[59].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_WWW_AUTHENTICATE_EMPTY: 
     "www-authenticate": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[60].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_GRPC_STATUS_0: 
     "grpc-status": "0" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[61].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_GRPC_STATUS_1: 
     "grpc-status": "1" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[62].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_GRPC_STATUS_2: 
     "grpc-status": "2" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[63].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_GRPC_ENCODING_IDENTITY: 
     "grpc-encoding": "identity" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[64].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_GRPC_ENCODING_GZIP: 
     "grpc-encoding": "gzip" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[65].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_GRPC_ENCODING_DEFLATE: 
     "grpc-encoding": "deflate" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[66].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_TE_TRAILERS: 
     "te": "trailers" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[67].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_CONTENT_TYPE_APPLICATION_SLASH_GRPC: 
     "content-type": "application/grpc" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[68].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_SCHEME_GRPC: 
     ":scheme": "grpc" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[69].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_METHOD_PUT: 
     ":method": "PUT" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[70].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_ACCEPT_ENCODING_EMPTY: 
     "accept-encoding": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[71].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_CONTENT_ENCODING_IDENTITY: 
     "content-encoding": "identity" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[72].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_CONTENT_ENCODING_GZIP: 
     "content-encoding": "gzip" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[73].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_LB_TOKEN_EMPTY: 
     "lb-token": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[74].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_LB_COST_BIN_EMPTY: 
     "lb-cost-bin": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[75].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY: 
     "grpc-accept-encoding": "identity" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[76].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_GRPC_ACCEPT_ENCODING_DEFLATE: 
     "grpc-accept-encoding": "deflate" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[77].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_DEFLATE: 
     "grpc-accept-encoding": "identity,deflate" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[78].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_GRPC_ACCEPT_ENCODING_GZIP: 
     "grpc-accept-encoding": "gzip" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[79].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_GZIP: 
     "grpc-accept-encoding": "identity,gzip" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[80].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_GRPC_ACCEPT_ENCODING_DEFLATE_COMMA_GZIP: 
     "grpc-accept-encoding": "deflate,gzip" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[81].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_DEFLATE_COMMA_GZIP: 
     "grpc-accept-encoding": "identity,deflate,gzip" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[82].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_ACCEPT_ENCODING_IDENTITY: 
     "accept-encoding": "identity" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[83].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_ACCEPT_ENCODING_GZIP: 
     "accept-encoding": "gzip" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[84].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_ACCEPT_ENCODING_IDENTITY_COMMA_GZIP: 
     "accept-encoding": "identity,gzip" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[85].data(),
        GRPC_MDELEM_STORAGE_STATIC)
    // clang-format on
};
uintptr_t grpc_static_mdelem_user_data[GRPC_STATIC_MDELEM_COUNT] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 4, 4, 6, 6, 8, 8, 2, 4, 4};

static const int8_t elems_r[] = {
    15, 10,  -8, 0,  2,  -42, -81, -43, 0,   6,  -8,  0,   0,   0,   2,
    -3, -10, 0,  0,  1,  0,   -1,  0,   0,   0,  0,   0,   0,   0,   0,
    0,  0,   0,  0,  0,  0,   0,   0,   0,   0,  0,   0,   0,   0,   0,
    0,  0,   0,  0,  0,  0,   0,   0,   -64, 0,  -67, -68, -69, -70, 0,
    35, 34,  33, 32, 31, 30,  29,  28,  27,  26, 25,  24,  23,  22,  21,
    20, 19,  18, 17, 16, 15,  14,  13,  12,  11, 10,  9,   8,   7,   6,
    5,  4,   5,  4,  4,  8,   8,   0,   0,   0,  0,   0,   0,   -5,  0};
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
    260,  261,  262,  263,  264,  265,  266,   1107, 1108,  1741,  147,  148,
    472,  473,  1634, 42,   43,   1527, 1750,  1000, 1001,  774,   775,  1643,
    633,  845,  2062, 2169, 2276, 5700, 5914,  6021, 6128,  6235,  1766, 6342,
    6449, 6556, 6663, 6770, 6877, 6984, 7091,  7198, 7305,  7412,  7519, 7626,
    7733, 7840, 7947, 8054, 8161, 8268, 8375,  8482, 8589,  8696,  8803, 8910,
    9017, 9124, 9231, 9338, 9445, 9552, 9659,  1167, 528,   9766,  9873, 208,
    9980, 1173, 1174, 1175, 1176, 1809, 10087, 1060, 10194, 10943, 1702, 0,
    1816, 0,    0,    1597, 0,    0,    350,   0,    0,     0,     0,    0,
    0,    0,    0,    0,    0,    0,    0,     0,    0,     0,     0,    0,
    0,    0,    0,    0,    0,    0,    0,     0,    0,     0,     0,    0,
    0,    0,    0,    0,    0,    0,    0,     0,    0,     0,     0,    0,
    0,    0,    0,    0,    0,    0,    0};
static const uint8_t elem_idxs[] = {
    7,  8,  9,  10,  11, 12,  13,  77, 79,  71,  1,  2,  5,  6,  25, 3,
    4,  30, 84, 66,  65, 62,  63,  73, 67,  61,  57, 37, 74, 14, 16, 17,
    18, 19, 15, 20,  21, 22,  23,  24, 26,  27,  28, 29, 31, 32, 33, 34,
    35, 36, 38, 39,  40, 41,  42,  43, 44,  45,  46, 47, 48, 49, 50, 51,
    52, 53, 54, 76,  69, 55,  56,  70, 58,  78,  80, 81, 82, 83, 59, 64,
    60, 75, 72, 255, 85, 255, 255, 68, 255, 255, 0};

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
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 0),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[1], {{7, g_bytes + 5}}},
        {&grpc_static_metadata_refcounts[40], {{3, g_bytes + 612}}}, 1),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[1], {{7, g_bytes + 5}}},
        {&grpc_static_metadata_refcounts[41], {{4, g_bytes + 615}}}, 2),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[0], {{5, g_bytes + 0}}},
        {&grpc_static_metadata_refcounts[42], {{1, g_bytes + 619}}}, 3),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[0], {{5, g_bytes + 0}}},
        {&grpc_static_metadata_refcounts[43], {{11, g_bytes + 620}}}, 4),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[4], {{7, g_bytes + 29}}},
        {&grpc_static_metadata_refcounts[44], {{4, g_bytes + 631}}}, 5),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[4], {{7, g_bytes + 29}}},
        {&grpc_static_metadata_refcounts[45], {{5, g_bytes + 635}}}, 6),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[46], {{3, g_bytes + 640}}}, 7),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[47], {{3, g_bytes + 643}}}, 8),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[48], {{3, g_bytes + 646}}}, 9),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[49], {{3, g_bytes + 649}}}, 10),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[50], {{3, g_bytes + 652}}}, 11),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[51], {{3, g_bytes + 655}}}, 12),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[52], {{3, g_bytes + 658}}}, 13),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[53], {{14, g_bytes + 661}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 14),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[16], {{15, g_bytes + 186}}},
        {&grpc_static_metadata_refcounts[54], {{13, g_bytes + 675}}}, 15),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[55], {{15, g_bytes + 688}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 16),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[56], {{13, g_bytes + 703}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 17),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[57], {{6, g_bytes + 716}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 18),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[58], {{27, g_bytes + 722}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 19),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[59], {{3, g_bytes + 749}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 20),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[60], {{5, g_bytes + 752}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 21),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[61], {{13, g_bytes + 757}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 22),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[62], {{13, g_bytes + 770}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 23),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[63], {{19, g_bytes + 783}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 24),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[15], {{16, g_bytes + 170}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 25),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[64], {{16, g_bytes + 802}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 26),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[65], {{14, g_bytes + 818}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 27),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[66], {{16, g_bytes + 832}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 28),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[67], {{13, g_bytes + 848}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 29),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[14], {{12, g_bytes + 158}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 30),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[68], {{6, g_bytes + 861}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 31),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[69], {{4, g_bytes + 867}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 32),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[70], {{4, g_bytes + 871}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 33),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[71], {{6, g_bytes + 875}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 34),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[72], {{7, g_bytes + 881}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 35),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[73], {{4, g_bytes + 888}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 36),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[20], {{4, g_bytes + 278}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 37),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[74], {{8, g_bytes + 892}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 38),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[75], {{17, g_bytes + 900}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 39),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[76], {{13, g_bytes + 917}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 40),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[77], {{8, g_bytes + 930}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 41),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[78], {{19, g_bytes + 938}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 42),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[79], {{13, g_bytes + 957}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 43),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[80], {{4, g_bytes + 970}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 44),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[81], {{8, g_bytes + 974}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 45),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[82], {{12, g_bytes + 982}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 46),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[83], {{18, g_bytes + 994}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 47),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[84], {{19, g_bytes + 1012}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 48),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[85], {{5, g_bytes + 1031}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 49),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[86], {{7, g_bytes + 1036}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 50),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[87], {{7, g_bytes + 1043}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 51),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[88], {{11, g_bytes + 1050}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 52),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[89], {{6, g_bytes + 1061}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 53),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[90], {{10, g_bytes + 1067}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 54),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[91], {{25, g_bytes + 1077}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 55),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[92], {{17, g_bytes + 1102}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 56),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[19], {{10, g_bytes + 268}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 57),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[93], {{4, g_bytes + 1119}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 58),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[94], {{3, g_bytes + 1123}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 59),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[95], {{16, g_bytes + 1126}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 60),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[7], {{11, g_bytes + 50}}},
        {&grpc_static_metadata_refcounts[96], {{1, g_bytes + 1142}}}, 61),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[7], {{11, g_bytes + 50}}},
        {&grpc_static_metadata_refcounts[25], {{1, g_bytes + 350}}}, 62),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[7], {{11, g_bytes + 50}}},
        {&grpc_static_metadata_refcounts[26], {{1, g_bytes + 351}}}, 63),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[9], {{13, g_bytes + 77}}},
        {&grpc_static_metadata_refcounts[97], {{8, g_bytes + 1143}}}, 64),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[9], {{13, g_bytes + 77}}},
        {&grpc_static_metadata_refcounts[38], {{4, g_bytes + 597}}}, 65),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[9], {{13, g_bytes + 77}}},
        {&grpc_static_metadata_refcounts[37], {{7, g_bytes + 590}}}, 66),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[5], {{2, g_bytes + 36}}},
        {&grpc_static_metadata_refcounts[98], {{8, g_bytes + 1151}}}, 67),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[14], {{12, g_bytes + 158}}},
        {&grpc_static_metadata_refcounts[99], {{16, g_bytes + 1159}}}, 68),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[4], {{7, g_bytes + 29}}},
        {&grpc_static_metadata_refcounts[100], {{4, g_bytes + 1175}}}, 69),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[1], {{7, g_bytes + 5}}},
        {&grpc_static_metadata_refcounts[101], {{3, g_bytes + 1179}}}, 70),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[16], {{15, g_bytes + 186}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 71),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[15], {{16, g_bytes + 170}}},
        {&grpc_static_metadata_refcounts[97], {{8, g_bytes + 1143}}}, 72),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[15], {{16, g_bytes + 170}}},
        {&grpc_static_metadata_refcounts[38], {{4, g_bytes + 597}}}, 73),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[21], {{8, g_bytes + 282}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 74),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[102], {{11, g_bytes + 1182}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}, 75),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[97], {{8, g_bytes + 1143}}}, 76),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[37], {{7, g_bytes + 590}}}, 77),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[103], {{16, g_bytes + 1193}}}, 78),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[38], {{4, g_bytes + 597}}}, 79),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[104], {{13, g_bytes + 1209}}}, 80),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[105], {{12, g_bytes + 1222}}}, 81),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[106], {{21, g_bytes + 1234}}}, 82),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[16], {{15, g_bytes + 186}}},
        {&grpc_static_metadata_refcounts[97], {{8, g_bytes + 1143}}}, 83),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[16], {{15, g_bytes + 186}}},
        {&grpc_static_metadata_refcounts[38], {{4, g_bytes + 597}}}, 84),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[16], {{15, g_bytes + 186}}},
        {&grpc_static_metadata_refcounts[104], {{13, g_bytes + 1209}}}, 85),
};
const uint8_t grpc_static_accept_encoding_metadata[8] = {0,  76, 77, 78,
                                                         79, 80, 81, 82};

const uint8_t grpc_static_accept_stream_encoding_metadata[4] = {0, 83, 84, 85};
