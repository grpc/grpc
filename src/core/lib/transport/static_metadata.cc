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
    116, 111, 107, 101, 110, 103, 114, 112, 99,  108, 98,  95,  99,  108, 105,
    101, 110, 116, 95,  115, 116, 97,  116, 115, 103, 114, 112, 99,  45,  112,
    114, 101, 118, 105, 111, 117, 115, 45,  114, 112, 99,  45,  97,  116, 116,
    101, 109, 112, 116, 115, 103, 114, 112, 99,  45,  114, 101, 116, 114, 121,
    45,  112, 117, 115, 104, 98,  97,  99,  107, 45,  109, 115, 103, 114, 112,
    99,  45,  116, 105, 109, 101, 111, 117, 116, 49,  50,  51,  52,  103, 114,
    112, 99,  46,  119, 97,  105, 116, 95,  102, 111, 114, 95,  114, 101, 97,
    100, 121, 103, 114, 112, 99,  46,  116, 105, 109, 101, 111, 117, 116, 103,
    114, 112, 99,  46,  109, 97,  120, 95,  114, 101, 113, 117, 101, 115, 116,
    95,  109, 101, 115, 115, 97,  103, 101, 95,  98,  121, 116, 101, 115, 103,
    114, 112, 99,  46,  109, 97,  120, 95,  114, 101, 115, 112, 111, 110, 115,
    101, 95,  109, 101, 115, 115, 97,  103, 101, 95,  98,  121, 116, 101, 115,
    47,  103, 114, 112, 99,  46,  108, 98,  46,  118, 49,  46,  76,  111, 97,
    100, 66,  97,  108, 97,  110, 99,  101, 114, 47,  66,  97,  108, 97,  110,
    99,  101, 76,  111, 97,  100, 47,  103, 114, 112, 99,  46,  104, 101, 97,
    108, 116, 104, 46,  118, 49,  46,  72,  101, 97,  108, 116, 104, 47,  87,
    97,  116, 99,  104, 47,  101, 110, 118, 111, 121, 46,  115, 101, 114, 118,
    105, 99,  101, 46,  100, 105, 115, 99,  111, 118, 101, 114, 121, 46,  118,
    50,  46,  65,  103, 103, 114, 101, 103, 97,  116, 101, 100, 68,  105, 115,
    99,  111, 118, 101, 114, 121, 83,  101, 114, 118, 105, 99,  101, 47,  83,
    116, 114, 101, 97,  109, 65,  103, 103, 114, 101, 103, 97,  116, 101, 100,
    82,  101, 115, 111, 117, 114, 99,  101, 115, 100, 101, 102, 108, 97,  116,
    101, 103, 122, 105, 112, 115, 116, 114, 101, 97,  109, 47,  103, 122, 105,
    112, 71,  69,  84,  80,  79,  83,  84,  47,  47,  105, 110, 100, 101, 120,
    46,  104, 116, 109, 108, 104, 116, 116, 112, 104, 116, 116, 112, 115, 50,
    48,  48,  50,  48,  52,  50,  48,  54,  51,  48,  52,  52,  48,  48,  52,
    48,  52,  53,  48,  48,  97,  99,  99,  101, 112, 116, 45,  99,  104, 97,
    114, 115, 101, 116, 103, 122, 105, 112, 44,  32,  100, 101, 102, 108, 97,
    116, 101, 97,  99,  99,  101, 112, 116, 45,  108, 97,  110, 103, 117, 97,
    103, 101, 97,  99,  99,  101, 112, 116, 45,  114, 97,  110, 103, 101, 115,
    97,  99,  99,  101, 112, 116, 97,  99,  99,  101, 115, 115, 45,  99,  111,
    110, 116, 114, 111, 108, 45,  97,  108, 108, 111, 119, 45,  111, 114, 105,
    103, 105, 110, 97,  103, 101, 97,  108, 108, 111, 119, 97,  117, 116, 104,
    111, 114, 105, 122, 97,  116, 105, 111, 110, 99,  97,  99,  104, 101, 45,
    99,  111, 110, 116, 114, 111, 108, 99,  111, 110, 116, 101, 110, 116, 45,
    100, 105, 115, 112, 111, 115, 105, 116, 105, 111, 110, 99,  111, 110, 116,
    101, 110, 116, 45,  108, 97,  110, 103, 117, 97,  103, 101, 99,  111, 110,
    116, 101, 110, 116, 45,  108, 101, 110, 103, 116, 104, 99,  111, 110, 116,
    101, 110, 116, 45,  108, 111, 99,  97,  116, 105, 111, 110, 99,  111, 110,
    116, 101, 110, 116, 45,  114, 97,  110, 103, 101, 99,  111, 111, 107, 105,
    101, 100, 97,  116, 101, 101, 116, 97,  103, 101, 120, 112, 101, 99,  116,
    101, 120, 112, 105, 114, 101, 115, 102, 114, 111, 109, 105, 102, 45,  109,
    97,  116, 99,  104, 105, 102, 45,  109, 111, 100, 105, 102, 105, 101, 100,
    45,  115, 105, 110, 99,  101, 105, 102, 45,  110, 111, 110, 101, 45,  109,
    97,  116, 99,  104, 105, 102, 45,  114, 97,  110, 103, 101, 105, 102, 45,
    117, 110, 109, 111, 100, 105, 102, 105, 101, 100, 45,  115, 105, 110, 99,
    101, 108, 97,  115, 116, 45,  109, 111, 100, 105, 102, 105, 101, 100, 108,
    105, 110, 107, 108, 111, 99,  97,  116, 105, 111, 110, 109, 97,  120, 45,
    102, 111, 114, 119, 97,  114, 100, 115, 112, 114, 111, 120, 121, 45,  97,
    117, 116, 104, 101, 110, 116, 105, 99,  97,  116, 101, 112, 114, 111, 120,
    121, 45,  97,  117, 116, 104, 111, 114, 105, 122, 97,  116, 105, 111, 110,
    114, 97,  110, 103, 101, 114, 101, 102, 101, 114, 101, 114, 114, 101, 102,
    114, 101, 115, 104, 114, 101, 116, 114, 121, 45,  97,  102, 116, 101, 114,
    115, 101, 114, 118, 101, 114, 115, 101, 116, 45,  99,  111, 111, 107, 105,
    101, 115, 116, 114, 105, 99,  116, 45,  116, 114, 97,  110, 115, 112, 111,
    114, 116, 45,  115, 101, 99,  117, 114, 105, 116, 121, 116, 114, 97,  110,
    115, 102, 101, 114, 45,  101, 110, 99,  111, 100, 105, 110, 103, 118, 97,
    114, 121, 118, 105, 97,  119, 119, 119, 45,  97,  117, 116, 104, 101, 110,
    116, 105, 99,  97,  116, 101, 48,  105, 100, 101, 110, 116, 105, 116, 121,
    116, 114, 97,  105, 108, 101, 114, 115, 97,  112, 112, 108, 105, 99,  97,
    116, 105, 111, 110, 47,  103, 114, 112, 99,  103, 114, 112, 99,  80,  85,
    84,  108, 98,  45,  99,  111, 115, 116, 45,  98,  105, 110, 105, 100, 101,
    110, 116, 105, 116, 121, 44,  100, 101, 102, 108, 97,  116, 101, 105, 100,
    101, 110, 116, 105, 116, 121, 44,  103, 122, 105, 112, 100, 101, 102, 108,
    97,  116, 101, 44,  103, 122, 105, 112, 105, 100, 101, 110, 116, 105, 116,
    121, 44,  100, 101, 102, 108, 97,  116, 101, 44,  103, 122, 105, 112};

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
    {&grpc_static_metadata_refcounts[22], {{19, g_bytes + 290}}},
    {&grpc_static_metadata_refcounts[23], {{26, g_bytes + 309}}},
    {&grpc_static_metadata_refcounts[24], {{22, g_bytes + 335}}},
    {&grpc_static_metadata_refcounts[25], {{12, g_bytes + 357}}},
    {&grpc_static_metadata_refcounts[26], {{1, g_bytes + 369}}},
    {&grpc_static_metadata_refcounts[27], {{1, g_bytes + 370}}},
    {&grpc_static_metadata_refcounts[28], {{1, g_bytes + 371}}},
    {&grpc_static_metadata_refcounts[29], {{1, g_bytes + 372}}},
    {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}},
    {&grpc_static_metadata_refcounts[31], {{19, g_bytes + 373}}},
    {&grpc_static_metadata_refcounts[32], {{12, g_bytes + 392}}},
    {&grpc_static_metadata_refcounts[33], {{30, g_bytes + 404}}},
    {&grpc_static_metadata_refcounts[34], {{31, g_bytes + 434}}},
    {&grpc_static_metadata_refcounts[35], {{36, g_bytes + 465}}},
    {&grpc_static_metadata_refcounts[36], {{28, g_bytes + 501}}},
    {&grpc_static_metadata_refcounts[37], {{80, g_bytes + 529}}},
    {&grpc_static_metadata_refcounts[38], {{7, g_bytes + 609}}},
    {&grpc_static_metadata_refcounts[39], {{4, g_bytes + 616}}},
    {&grpc_static_metadata_refcounts[40], {{11, g_bytes + 620}}},
    {&grpc_static_metadata_refcounts[41], {{3, g_bytes + 631}}},
    {&grpc_static_metadata_refcounts[42], {{4, g_bytes + 634}}},
    {&grpc_static_metadata_refcounts[43], {{1, g_bytes + 638}}},
    {&grpc_static_metadata_refcounts[44], {{11, g_bytes + 639}}},
    {&grpc_static_metadata_refcounts[45], {{4, g_bytes + 650}}},
    {&grpc_static_metadata_refcounts[46], {{5, g_bytes + 654}}},
    {&grpc_static_metadata_refcounts[47], {{3, g_bytes + 659}}},
    {&grpc_static_metadata_refcounts[48], {{3, g_bytes + 662}}},
    {&grpc_static_metadata_refcounts[49], {{3, g_bytes + 665}}},
    {&grpc_static_metadata_refcounts[50], {{3, g_bytes + 668}}},
    {&grpc_static_metadata_refcounts[51], {{3, g_bytes + 671}}},
    {&grpc_static_metadata_refcounts[52], {{3, g_bytes + 674}}},
    {&grpc_static_metadata_refcounts[53], {{3, g_bytes + 677}}},
    {&grpc_static_metadata_refcounts[54], {{14, g_bytes + 680}}},
    {&grpc_static_metadata_refcounts[55], {{13, g_bytes + 694}}},
    {&grpc_static_metadata_refcounts[56], {{15, g_bytes + 707}}},
    {&grpc_static_metadata_refcounts[57], {{13, g_bytes + 722}}},
    {&grpc_static_metadata_refcounts[58], {{6, g_bytes + 735}}},
    {&grpc_static_metadata_refcounts[59], {{27, g_bytes + 741}}},
    {&grpc_static_metadata_refcounts[60], {{3, g_bytes + 768}}},
    {&grpc_static_metadata_refcounts[61], {{5, g_bytes + 771}}},
    {&grpc_static_metadata_refcounts[62], {{13, g_bytes + 776}}},
    {&grpc_static_metadata_refcounts[63], {{13, g_bytes + 789}}},
    {&grpc_static_metadata_refcounts[64], {{19, g_bytes + 802}}},
    {&grpc_static_metadata_refcounts[65], {{16, g_bytes + 821}}},
    {&grpc_static_metadata_refcounts[66], {{14, g_bytes + 837}}},
    {&grpc_static_metadata_refcounts[67], {{16, g_bytes + 851}}},
    {&grpc_static_metadata_refcounts[68], {{13, g_bytes + 867}}},
    {&grpc_static_metadata_refcounts[69], {{6, g_bytes + 880}}},
    {&grpc_static_metadata_refcounts[70], {{4, g_bytes + 886}}},
    {&grpc_static_metadata_refcounts[71], {{4, g_bytes + 890}}},
    {&grpc_static_metadata_refcounts[72], {{6, g_bytes + 894}}},
    {&grpc_static_metadata_refcounts[73], {{7, g_bytes + 900}}},
    {&grpc_static_metadata_refcounts[74], {{4, g_bytes + 907}}},
    {&grpc_static_metadata_refcounts[75], {{8, g_bytes + 911}}},
    {&grpc_static_metadata_refcounts[76], {{17, g_bytes + 919}}},
    {&grpc_static_metadata_refcounts[77], {{13, g_bytes + 936}}},
    {&grpc_static_metadata_refcounts[78], {{8, g_bytes + 949}}},
    {&grpc_static_metadata_refcounts[79], {{19, g_bytes + 957}}},
    {&grpc_static_metadata_refcounts[80], {{13, g_bytes + 976}}},
    {&grpc_static_metadata_refcounts[81], {{4, g_bytes + 989}}},
    {&grpc_static_metadata_refcounts[82], {{8, g_bytes + 993}}},
    {&grpc_static_metadata_refcounts[83], {{12, g_bytes + 1001}}},
    {&grpc_static_metadata_refcounts[84], {{18, g_bytes + 1013}}},
    {&grpc_static_metadata_refcounts[85], {{19, g_bytes + 1031}}},
    {&grpc_static_metadata_refcounts[86], {{5, g_bytes + 1050}}},
    {&grpc_static_metadata_refcounts[87], {{7, g_bytes + 1055}}},
    {&grpc_static_metadata_refcounts[88], {{7, g_bytes + 1062}}},
    {&grpc_static_metadata_refcounts[89], {{11, g_bytes + 1069}}},
    {&grpc_static_metadata_refcounts[90], {{6, g_bytes + 1080}}},
    {&grpc_static_metadata_refcounts[91], {{10, g_bytes + 1086}}},
    {&grpc_static_metadata_refcounts[92], {{25, g_bytes + 1096}}},
    {&grpc_static_metadata_refcounts[93], {{17, g_bytes + 1121}}},
    {&grpc_static_metadata_refcounts[94], {{4, g_bytes + 1138}}},
    {&grpc_static_metadata_refcounts[95], {{3, g_bytes + 1142}}},
    {&grpc_static_metadata_refcounts[96], {{16, g_bytes + 1145}}},
    {&grpc_static_metadata_refcounts[97], {{1, g_bytes + 1161}}},
    {&grpc_static_metadata_refcounts[98], {{8, g_bytes + 1162}}},
    {&grpc_static_metadata_refcounts[99], {{8, g_bytes + 1170}}},
    {&grpc_static_metadata_refcounts[100], {{16, g_bytes + 1178}}},
    {&grpc_static_metadata_refcounts[101], {{4, g_bytes + 1194}}},
    {&grpc_static_metadata_refcounts[102], {{3, g_bytes + 1198}}},
    {&grpc_static_metadata_refcounts[103], {{11, g_bytes + 1201}}},
    {&grpc_static_metadata_refcounts[104], {{16, g_bytes + 1212}}},
    {&grpc_static_metadata_refcounts[105], {{13, g_bytes + 1228}}},
    {&grpc_static_metadata_refcounts[106], {{12, g_bytes + 1241}}},
    {&grpc_static_metadata_refcounts[107], {{21, g_bytes + 1253}}},
};

uintptr_t grpc_static_mdelem_user_data[GRPC_STATIC_MDELEM_COUNT] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 4, 4, 6, 6, 8, 8, 2, 4, 4};

static const int8_t elems_r[] = {
    15,  10, -8, 0,  2,  -42, -82, -43, 0,   6,   -8,  0,   0,  0,  2,  -3,
    -10, 0,  0,  1,  0,  -1,  0,   0,   0,   0,   0,   0,   0,  0,  0,  0,
    0,   0,  0,  0,  0,  0,   0,   0,   0,   0,   0,   0,   0,  0,  0,  0,
    0,   0,  0,  0,  0,  0,   -66, 0,   -69, -70, -71, -72, 0,  34, 33, 32,
    31,  30, 29, 28, 27, 26,  25,  24,  23,  22,  21,  20,  19, 18, 17, 16,
    15,  14, 13, 12, 11, 10,  9,   8,   7,   6,   5,   4,   3,  4,  3,  3,
    7,   7,  0,  0,  0,  0,   0,   0,   -6,  0};
static uint32_t elems_phash(uint32_t i) {
  i -= 43;
  uint32_t x = i % 106;
  uint32_t y = i / 106;
  uint32_t h = x;
  if (y < GPR_ARRAY_SIZE(elems_r)) {
    uint32_t delta = (uint32_t)elems_r[y];
    h += delta;
  }
  return h;
}

static const uint16_t elem_keys[] = {
    263,   264,  265,  266,  267,  268,  269,   1118, 1119,  1758,  149,   150,
    477,   478,  1650, 43,   44,   1542, 1767,  1010, 1011,  782,   783,   1659,
    639,   853,  2082, 2190, 2298, 5862, 6078,  6186, 6294,  6402,  1783,  6510,
    6618,  6726, 6834, 6942, 7050, 7158, 7266,  7374, 7482,  7590,  7698,  7806,
    7914,  8022, 8130, 8238, 8346, 8454, 8562,  8670, 8778,  8886,  8994,  9102,
    9210,  9318, 9426, 9534, 9642, 9750, 9858,  1178, 533,   9966,  10074, 210,
    10182, 1184, 1185, 1186, 1187, 1826, 10290, 1070, 10398, 11154, 1718,  0,
    1833,  0,    0,    1612, 0,    0,    0,     354,  0,     0,     0,     0,
    0,     0,    0,    0,    0,    0,    0,     0,    0,     0,     0,     0,
    0,     0,    0,    0,    0,    0,    0,     0,    0,     0,     0,     0,
    0,     0,    0,    0,    0,    0,    0,     0,    0,     0,     0,     0,
    0,     0,    0,    0,    0,    0,    0};
static const uint8_t elem_idxs[] = {
    7,  8,  9,  10,  11, 12,  13,  77, 79,  71,  1,   2,  5,  6,  25, 3,
    4,  30, 84, 66,  65, 62,  63,  73, 67,  61,  57,  37, 74, 14, 16, 17,
    18, 19, 15, 20,  21, 22,  23,  24, 26,  27,  28,  29, 31, 32, 33, 34,
    35, 36, 38, 39,  40, 41,  42,  43, 44,  45,  46,  47, 48, 49, 50, 51,
    52, 53, 54, 76,  69, 55,  56,  70, 58,  78,  80,  81, 82, 83, 59, 64,
    60, 75, 72, 255, 85, 255, 255, 68, 255, 255, 255, 0};

grpc_mdelem grpc_static_mdelem_for_static_strings(intptr_t a, intptr_t b) {
  if (a == -1 || b == -1) return GRPC_MDNULL;
  uint32_t k = static_cast<uint32_t>(a * 108 + b);
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
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 0),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[1], {{7, g_bytes + 5}}},
        {&grpc_static_metadata_refcounts[41], {{3, g_bytes + 631}}}, 1),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[1], {{7, g_bytes + 5}}},
        {&grpc_static_metadata_refcounts[42], {{4, g_bytes + 634}}}, 2),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[0], {{5, g_bytes + 0}}},
        {&grpc_static_metadata_refcounts[43], {{1, g_bytes + 638}}}, 3),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[0], {{5, g_bytes + 0}}},
        {&grpc_static_metadata_refcounts[44], {{11, g_bytes + 639}}}, 4),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[4], {{7, g_bytes + 29}}},
        {&grpc_static_metadata_refcounts[45], {{4, g_bytes + 650}}}, 5),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[4], {{7, g_bytes + 29}}},
        {&grpc_static_metadata_refcounts[46], {{5, g_bytes + 654}}}, 6),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[47], {{3, g_bytes + 659}}}, 7),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[48], {{3, g_bytes + 662}}}, 8),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[49], {{3, g_bytes + 665}}}, 9),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[50], {{3, g_bytes + 668}}}, 10),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[51], {{3, g_bytes + 671}}}, 11),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[52], {{3, g_bytes + 674}}}, 12),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[53], {{3, g_bytes + 677}}}, 13),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[54], {{14, g_bytes + 680}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 14),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[16], {{15, g_bytes + 186}}},
        {&grpc_static_metadata_refcounts[55], {{13, g_bytes + 694}}}, 15),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[56], {{15, g_bytes + 707}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 16),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[57], {{13, g_bytes + 722}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 17),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[58], {{6, g_bytes + 735}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 18),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[59], {{27, g_bytes + 741}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 19),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[60], {{3, g_bytes + 768}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 20),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[61], {{5, g_bytes + 771}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 21),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[62], {{13, g_bytes + 776}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 22),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[63], {{13, g_bytes + 789}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 23),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[64], {{19, g_bytes + 802}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 24),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[15], {{16, g_bytes + 170}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 25),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[65], {{16, g_bytes + 821}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 26),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[66], {{14, g_bytes + 837}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 27),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[67], {{16, g_bytes + 851}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 28),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[68], {{13, g_bytes + 867}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 29),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[14], {{12, g_bytes + 158}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 30),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[69], {{6, g_bytes + 880}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 31),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[70], {{4, g_bytes + 886}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 32),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[71], {{4, g_bytes + 890}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 33),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[72], {{6, g_bytes + 894}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 34),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[73], {{7, g_bytes + 900}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 35),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[74], {{4, g_bytes + 907}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 36),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[20], {{4, g_bytes + 278}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 37),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[75], {{8, g_bytes + 911}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 38),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[76], {{17, g_bytes + 919}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 39),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[77], {{13, g_bytes + 936}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 40),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[78], {{8, g_bytes + 949}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 41),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[79], {{19, g_bytes + 957}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 42),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[80], {{13, g_bytes + 976}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 43),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[81], {{4, g_bytes + 989}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 44),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[82], {{8, g_bytes + 993}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 45),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[83], {{12, g_bytes + 1001}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 46),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[84], {{18, g_bytes + 1013}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 47),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[85], {{19, g_bytes + 1031}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 48),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[86], {{5, g_bytes + 1050}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 49),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[87], {{7, g_bytes + 1055}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 50),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[88], {{7, g_bytes + 1062}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 51),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[89], {{11, g_bytes + 1069}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 52),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[90], {{6, g_bytes + 1080}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 53),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[91], {{10, g_bytes + 1086}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 54),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[92], {{25, g_bytes + 1096}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 55),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[93], {{17, g_bytes + 1121}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 56),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[19], {{10, g_bytes + 268}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 57),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[94], {{4, g_bytes + 1138}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 58),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[95], {{3, g_bytes + 1142}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 59),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[96], {{16, g_bytes + 1145}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 60),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[7], {{11, g_bytes + 50}}},
        {&grpc_static_metadata_refcounts[97], {{1, g_bytes + 1161}}}, 61),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[7], {{11, g_bytes + 50}}},
        {&grpc_static_metadata_refcounts[26], {{1, g_bytes + 369}}}, 62),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[7], {{11, g_bytes + 50}}},
        {&grpc_static_metadata_refcounts[27], {{1, g_bytes + 370}}}, 63),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[9], {{13, g_bytes + 77}}},
        {&grpc_static_metadata_refcounts[98], {{8, g_bytes + 1162}}}, 64),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[9], {{13, g_bytes + 77}}},
        {&grpc_static_metadata_refcounts[39], {{4, g_bytes + 616}}}, 65),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[9], {{13, g_bytes + 77}}},
        {&grpc_static_metadata_refcounts[38], {{7, g_bytes + 609}}}, 66),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[5], {{2, g_bytes + 36}}},
        {&grpc_static_metadata_refcounts[99], {{8, g_bytes + 1170}}}, 67),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[14], {{12, g_bytes + 158}}},
        {&grpc_static_metadata_refcounts[100], {{16, g_bytes + 1178}}}, 68),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[4], {{7, g_bytes + 29}}},
        {&grpc_static_metadata_refcounts[101], {{4, g_bytes + 1194}}}, 69),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[1], {{7, g_bytes + 5}}},
        {&grpc_static_metadata_refcounts[102], {{3, g_bytes + 1198}}}, 70),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[16], {{15, g_bytes + 186}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 71),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[15], {{16, g_bytes + 170}}},
        {&grpc_static_metadata_refcounts[98], {{8, g_bytes + 1162}}}, 72),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[15], {{16, g_bytes + 170}}},
        {&grpc_static_metadata_refcounts[39], {{4, g_bytes + 616}}}, 73),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[21], {{8, g_bytes + 282}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 74),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[103], {{11, g_bytes + 1201}}},
        {&grpc_static_metadata_refcounts[30], {{0, g_bytes + 373}}}, 75),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[98], {{8, g_bytes + 1162}}}, 76),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[38], {{7, g_bytes + 609}}}, 77),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[104], {{16, g_bytes + 1212}}}, 78),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[39], {{4, g_bytes + 616}}}, 79),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[105], {{13, g_bytes + 1228}}}, 80),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[106], {{12, g_bytes + 1241}}}, 81),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[107], {{21, g_bytes + 1253}}}, 82),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[16], {{15, g_bytes + 186}}},
        {&grpc_static_metadata_refcounts[98], {{8, g_bytes + 1162}}}, 83),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[16], {{15, g_bytes + 186}}},
        {&grpc_static_metadata_refcounts[39], {{4, g_bytes + 616}}}, 84),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[16], {{15, g_bytes + 186}}},
        {&grpc_static_metadata_refcounts[105], {{13, g_bytes + 1228}}}, 85),
};
const uint8_t grpc_static_accept_encoding_metadata[8] = {0,  76, 77, 78,
                                                         79, 80, 81, 82};

const uint8_t grpc_static_accept_stream_encoding_metadata[4] = {0, 83, 84, 85};
