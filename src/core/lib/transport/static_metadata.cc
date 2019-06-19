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
    97,  100, 47,  103, 114, 112, 99,  46,  108, 98,  46,  118, 50,  46,  69,
    110, 100, 112, 111, 105, 110, 116, 68,  105, 115, 99,  111, 118, 101, 114,
    121, 83,  101, 114, 118, 105, 99,  101, 47,  83,  116, 114, 101, 97,  109,
    69,  110, 100, 112, 111, 105, 110, 116, 115, 47,  103, 114, 112, 99,  46,
    108, 98,  46,  118, 50,  46,  76,  111, 97,  100, 82,  101, 112, 111, 114,
    116, 105, 110, 103, 83,  101, 114, 118, 105, 99,  101, 47,  83,  116, 114,
    101, 97,  109, 76,  111, 97,  100, 83,  116, 97,  116, 115, 47,  103, 114,
    112, 99,  46,  104, 101, 97,  108, 116, 104, 46,  118, 49,  46,  72,  101,
    97,  108, 116, 104, 47,  87,  97,  116, 99,  104, 47,  101, 110, 118, 111,
    121, 46,  115, 101, 114, 118, 105, 99,  101, 46,  100, 105, 115, 99,  111,
    118, 101, 114, 121, 46,  118, 50,  46,  65,  103, 103, 114, 101, 103, 97,
    116, 101, 100, 68,  105, 115, 99,  111, 118, 101, 114, 121, 83,  101, 114,
    118, 105, 99,  101, 47,  83,  116, 114, 101, 97,  109, 65,  103, 103, 114,
    101, 103, 97,  116, 101, 100, 82,  101, 115, 111, 117, 114, 99,  101, 115,
    100, 101, 102, 108, 97,  116, 101, 103, 122, 105, 112, 115, 116, 114, 101,
    97,  109, 47,  103, 122, 105, 112, 71,  69,  84,  80,  79,  83,  84,  47,
    47,  105, 110, 100, 101, 120, 46,  104, 116, 109, 108, 104, 116, 116, 112,
    104, 116, 116, 112, 115, 50,  48,  48,  50,  48,  52,  50,  48,  54,  51,
    48,  52,  52,  48,  48,  52,  48,  52,  53,  48,  48,  97,  99,  99,  101,
    112, 116, 45,  99,  104, 97,  114, 115, 101, 116, 103, 122, 105, 112, 44,
    32,  100, 101, 102, 108, 97,  116, 101, 97,  99,  99,  101, 112, 116, 45,
    108, 97,  110, 103, 117, 97,  103, 101, 97,  99,  99,  101, 112, 116, 45,
    114, 97,  110, 103, 101, 115, 97,  99,  99,  101, 112, 116, 97,  99,  99,
    101, 115, 115, 45,  99,  111, 110, 116, 114, 111, 108, 45,  97,  108, 108,
    111, 119, 45,  111, 114, 105, 103, 105, 110, 97,  103, 101, 97,  108, 108,
    111, 119, 97,  117, 116, 104, 111, 114, 105, 122, 97,  116, 105, 111, 110,
    99,  97,  99,  104, 101, 45,  99,  111, 110, 116, 114, 111, 108, 99,  111,
    110, 116, 101, 110, 116, 45,  100, 105, 115, 112, 111, 115, 105, 116, 105,
    111, 110, 99,  111, 110, 116, 101, 110, 116, 45,  108, 97,  110, 103, 117,
    97,  103, 101, 99,  111, 110, 116, 101, 110, 116, 45,  108, 101, 110, 103,
    116, 104, 99,  111, 110, 116, 101, 110, 116, 45,  108, 111, 99,  97,  116,
    105, 111, 110, 99,  111, 110, 116, 101, 110, 116, 45,  114, 97,  110, 103,
    101, 99,  111, 111, 107, 105, 101, 100, 97,  116, 101, 101, 116, 97,  103,
    101, 120, 112, 101, 99,  116, 101, 120, 112, 105, 114, 101, 115, 102, 114,
    111, 109, 105, 102, 45,  109, 97,  116, 99,  104, 105, 102, 45,  109, 111,
    100, 105, 102, 105, 101, 100, 45,  115, 105, 110, 99,  101, 105, 102, 45,
    110, 111, 110, 101, 45,  109, 97,  116, 99,  104, 105, 102, 45,  114, 97,
    110, 103, 101, 105, 102, 45,  117, 110, 109, 111, 100, 105, 102, 105, 101,
    100, 45,  115, 105, 110, 99,  101, 108, 97,  115, 116, 45,  109, 111, 100,
    105, 102, 105, 101, 100, 108, 105, 110, 107, 108, 111, 99,  97,  116, 105,
    111, 110, 109, 97,  120, 45,  102, 111, 114, 119, 97,  114, 100, 115, 112,
    114, 111, 120, 121, 45,  97,  117, 116, 104, 101, 110, 116, 105, 99,  97,
    116, 101, 112, 114, 111, 120, 121, 45,  97,  117, 116, 104, 111, 114, 105,
    122, 97,  116, 105, 111, 110, 114, 97,  110, 103, 101, 114, 101, 102, 101,
    114, 101, 114, 114, 101, 102, 114, 101, 115, 104, 114, 101, 116, 114, 121,
    45,  97,  102, 116, 101, 114, 115, 101, 114, 118, 101, 114, 115, 101, 116,
    45,  99,  111, 111, 107, 105, 101, 115, 116, 114, 105, 99,  116, 45,  116,
    114, 97,  110, 115, 112, 111, 114, 116, 45,  115, 101, 99,  117, 114, 105,
    116, 121, 116, 114, 97,  110, 115, 102, 101, 114, 45,  101, 110, 99,  111,
    100, 105, 110, 103, 118, 97,  114, 121, 118, 105, 97,  119, 119, 119, 45,
    97,  117, 116, 104, 101, 110, 116, 105, 99,  97,  116, 101, 48,  105, 100,
    101, 110, 116, 105, 116, 121, 116, 114, 97,  105, 108, 101, 114, 115, 97,
    112, 112, 108, 105, 99,  97,  116, 105, 111, 110, 47,  103, 114, 112, 99,
    103, 114, 112, 99,  80,  85,  84,  108, 98,  45,  99,  111, 115, 116, 45,
    98,  105, 110, 108, 111, 99,  97,  108, 105, 116, 121, 45,  115, 116, 97,
    116, 115, 105, 100, 101, 110, 116, 105, 116, 121, 44,  100, 101, 102, 108,
    97,  116, 101, 105, 100, 101, 110, 116, 105, 116, 121, 44,  103, 122, 105,
    112, 100, 101, 102, 108, 97,  116, 101, 44,  103, 122, 105, 112, 105, 100,
    101, 110, 116, 105, 116, 121, 44,  100, 101, 102, 108, 97,  116, 101, 44,
    103, 122, 105, 112};

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
    {&grpc_static_metadata_refcounts[35], {{52, g_bytes + 482}}},
    {&grpc_static_metadata_refcounts[36], {{48, g_bytes + 534}}},
    {&grpc_static_metadata_refcounts[37], {{28, g_bytes + 582}}},
    {&grpc_static_metadata_refcounts[38], {{80, g_bytes + 610}}},
    {&grpc_static_metadata_refcounts[39], {{7, g_bytes + 690}}},
    {&grpc_static_metadata_refcounts[40], {{4, g_bytes + 697}}},
    {&grpc_static_metadata_refcounts[41], {{11, g_bytes + 701}}},
    {&grpc_static_metadata_refcounts[42], {{3, g_bytes + 712}}},
    {&grpc_static_metadata_refcounts[43], {{4, g_bytes + 715}}},
    {&grpc_static_metadata_refcounts[44], {{1, g_bytes + 719}}},
    {&grpc_static_metadata_refcounts[45], {{11, g_bytes + 720}}},
    {&grpc_static_metadata_refcounts[46], {{4, g_bytes + 731}}},
    {&grpc_static_metadata_refcounts[47], {{5, g_bytes + 735}}},
    {&grpc_static_metadata_refcounts[48], {{3, g_bytes + 740}}},
    {&grpc_static_metadata_refcounts[49], {{3, g_bytes + 743}}},
    {&grpc_static_metadata_refcounts[50], {{3, g_bytes + 746}}},
    {&grpc_static_metadata_refcounts[51], {{3, g_bytes + 749}}},
    {&grpc_static_metadata_refcounts[52], {{3, g_bytes + 752}}},
    {&grpc_static_metadata_refcounts[53], {{3, g_bytes + 755}}},
    {&grpc_static_metadata_refcounts[54], {{3, g_bytes + 758}}},
    {&grpc_static_metadata_refcounts[55], {{14, g_bytes + 761}}},
    {&grpc_static_metadata_refcounts[56], {{13, g_bytes + 775}}},
    {&grpc_static_metadata_refcounts[57], {{15, g_bytes + 788}}},
    {&grpc_static_metadata_refcounts[58], {{13, g_bytes + 803}}},
    {&grpc_static_metadata_refcounts[59], {{6, g_bytes + 816}}},
    {&grpc_static_metadata_refcounts[60], {{27, g_bytes + 822}}},
    {&grpc_static_metadata_refcounts[61], {{3, g_bytes + 849}}},
    {&grpc_static_metadata_refcounts[62], {{5, g_bytes + 852}}},
    {&grpc_static_metadata_refcounts[63], {{13, g_bytes + 857}}},
    {&grpc_static_metadata_refcounts[64], {{13, g_bytes + 870}}},
    {&grpc_static_metadata_refcounts[65], {{19, g_bytes + 883}}},
    {&grpc_static_metadata_refcounts[66], {{16, g_bytes + 902}}},
    {&grpc_static_metadata_refcounts[67], {{14, g_bytes + 918}}},
    {&grpc_static_metadata_refcounts[68], {{16, g_bytes + 932}}},
    {&grpc_static_metadata_refcounts[69], {{13, g_bytes + 948}}},
    {&grpc_static_metadata_refcounts[70], {{6, g_bytes + 961}}},
    {&grpc_static_metadata_refcounts[71], {{4, g_bytes + 967}}},
    {&grpc_static_metadata_refcounts[72], {{4, g_bytes + 971}}},
    {&grpc_static_metadata_refcounts[73], {{6, g_bytes + 975}}},
    {&grpc_static_metadata_refcounts[74], {{7, g_bytes + 981}}},
    {&grpc_static_metadata_refcounts[75], {{4, g_bytes + 988}}},
    {&grpc_static_metadata_refcounts[76], {{8, g_bytes + 992}}},
    {&grpc_static_metadata_refcounts[77], {{17, g_bytes + 1000}}},
    {&grpc_static_metadata_refcounts[78], {{13, g_bytes + 1017}}},
    {&grpc_static_metadata_refcounts[79], {{8, g_bytes + 1030}}},
    {&grpc_static_metadata_refcounts[80], {{19, g_bytes + 1038}}},
    {&grpc_static_metadata_refcounts[81], {{13, g_bytes + 1057}}},
    {&grpc_static_metadata_refcounts[82], {{4, g_bytes + 1070}}},
    {&grpc_static_metadata_refcounts[83], {{8, g_bytes + 1074}}},
    {&grpc_static_metadata_refcounts[84], {{12, g_bytes + 1082}}},
    {&grpc_static_metadata_refcounts[85], {{18, g_bytes + 1094}}},
    {&grpc_static_metadata_refcounts[86], {{19, g_bytes + 1112}}},
    {&grpc_static_metadata_refcounts[87], {{5, g_bytes + 1131}}},
    {&grpc_static_metadata_refcounts[88], {{7, g_bytes + 1136}}},
    {&grpc_static_metadata_refcounts[89], {{7, g_bytes + 1143}}},
    {&grpc_static_metadata_refcounts[90], {{11, g_bytes + 1150}}},
    {&grpc_static_metadata_refcounts[91], {{6, g_bytes + 1161}}},
    {&grpc_static_metadata_refcounts[92], {{10, g_bytes + 1167}}},
    {&grpc_static_metadata_refcounts[93], {{25, g_bytes + 1177}}},
    {&grpc_static_metadata_refcounts[94], {{17, g_bytes + 1202}}},
    {&grpc_static_metadata_refcounts[95], {{4, g_bytes + 1219}}},
    {&grpc_static_metadata_refcounts[96], {{3, g_bytes + 1223}}},
    {&grpc_static_metadata_refcounts[97], {{16, g_bytes + 1226}}},
    {&grpc_static_metadata_refcounts[98], {{1, g_bytes + 1242}}},
    {&grpc_static_metadata_refcounts[99], {{8, g_bytes + 1243}}},
    {&grpc_static_metadata_refcounts[100], {{8, g_bytes + 1251}}},
    {&grpc_static_metadata_refcounts[101], {{16, g_bytes + 1259}}},
    {&grpc_static_metadata_refcounts[102], {{4, g_bytes + 1275}}},
    {&grpc_static_metadata_refcounts[103], {{3, g_bytes + 1279}}},
    {&grpc_static_metadata_refcounts[104], {{11, g_bytes + 1282}}},
    {&grpc_static_metadata_refcounts[105], {{14, g_bytes + 1293}}},
    {&grpc_static_metadata_refcounts[106], {{16, g_bytes + 1307}}},
    {&grpc_static_metadata_refcounts[107], {{13, g_bytes + 1323}}},
    {&grpc_static_metadata_refcounts[108], {{12, g_bytes + 1336}}},
    {&grpc_static_metadata_refcounts[109], {{21, g_bytes + 1348}}},
};

uintptr_t grpc_static_mdelem_user_data[GRPC_STATIC_MDELEM_COUNT] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 4, 4, 6, 6, 8, 8, 2, 4, 4};

static const int8_t elems_r[] = {
    15, 10, -8, 0,  2,  -43, -82, -44, 0,  5,   -8,  0,   0,   0,   4,  -1,
    -8, 0,  0,  3,  2,  1,   0,   0,   0,  0,   0,   0,   0,   0,   0,  0,
    0,  0,  0,  0,  0,  0,   0,   0,   0,  0,   0,   0,   0,   0,   0,  0,
    0,  0,  0,  0,  0,  0,   0,   -66, 0,  -50, -71, -55, -74, -75, 0,  32,
    31, 30, 30, 29, 40, 26,  25,  24,  23, 22,  21,  20,  19,  18,  19, 19,
    18, 17, 16, 15, 14, 13,  12,  11,  10, 9,   8,   7,   6,   5,   6,  5,
    5,  4,  9,  0,  0,  0,   0,   0,   0,  -4,  -5,  0};
static uint32_t elems_phash(uint32_t i) {
  i -= 44;
  uint32_t x = i % 108;
  uint32_t y = i / 108;
  uint32_t h = x;
  if (y < GPR_ARRAY_SIZE(elems_r)) {
    uint32_t delta = (uint32_t)elems_r[y];
    h += delta;
  }
  return h;
}

static const uint16_t elem_keys[] = {
    268,   269,  270,  271,   272,   273,   274,   1139,  1140, 1789, 152,
    153,   486,  487,  1679,  44,    45,    1569,  1029,  1030, 1800, 795,
    796,   650,  868,  1690,  2119,  2229,  2339,  6079,  6409, 6629, 6739,
    6849,  6959, 7069, 1816,  7179,  7289,  7509,  7619,  7729, 7839, 7949,
    8059,  8169, 8279, 8389,  6519,  6299,  8499,  7399,  8609, 8719, 8829,
    8939,  9049, 9159, 9269,  9379,  9489,  9599,  9709,  9819, 9929, 10039,
    10149, 1199, 542,  10259, 10369, 213,   10479, 10589, 1206, 1207, 1208,
    1209,  1089, 1859, 10699, 11469, 11579, 0,     1749,  0,    0,    1867,
    0,     1641, 0,    359,   0,     0,     0,     0,     0,    0,    0,
    0,     0,    0,    0,     0,     0,     0,     0,     0,    0,    0,
    0,     0,    0,    0,     0,     0,     0,     0,     0,    0,    0,
    0,     0,    0,    0,     0,     0,     0,     0,     0,    0,    0,
    0,     0,    0,    0,     0,     0,     0,     0,     0,    0,    0,
    0,     0,    0,    0};
static const uint8_t elem_idxs[] = {
    7,  8,  9,  10,  11, 12,  13,  78, 80,  71, 1,   2,  5,  6,  25, 3,
    4,  30, 66, 65,  85, 62,  63,  67, 61,  73, 57,  37, 74, 14, 17, 19,
    20, 21, 22, 23,  15, 24,  26,  28, 29,  31, 32,  33, 34, 35, 36, 38,
    18, 16, 39, 27,  40, 41,  42,  43, 44,  45, 46,  47, 48, 49, 50, 51,
    52, 53, 54, 77,  69, 55,  56,  70, 58,  59, 79,  81, 82, 83, 64, 84,
    60, 75, 76, 255, 72, 255, 255, 86, 255, 68, 255, 0};

grpc_mdelem grpc_static_mdelem_for_static_strings(intptr_t a, intptr_t b) {
  if (a == -1 || b == -1) return GRPC_MDNULL;
  uint32_t k = static_cast<uint32_t>(a * 110 + b);
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
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[1], {{7, g_bytes + 5}}},
        {&grpc_static_metadata_refcounts[42], {{3, g_bytes + 712}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[1], {{7, g_bytes + 5}}},
        {&grpc_static_metadata_refcounts[43], {{4, g_bytes + 715}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[0], {{5, g_bytes + 0}}},
        {&grpc_static_metadata_refcounts[44], {{1, g_bytes + 719}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[0], {{5, g_bytes + 0}}},
        {&grpc_static_metadata_refcounts[45], {{11, g_bytes + 720}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[4], {{7, g_bytes + 29}}},
        {&grpc_static_metadata_refcounts[46], {{4, g_bytes + 731}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[4], {{7, g_bytes + 29}}},
        {&grpc_static_metadata_refcounts[47], {{5, g_bytes + 735}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[48], {{3, g_bytes + 740}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[49], {{3, g_bytes + 743}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[50], {{3, g_bytes + 746}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[51], {{3, g_bytes + 749}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[52], {{3, g_bytes + 752}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[53], {{3, g_bytes + 755}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[2], {{7, g_bytes + 12}}},
        {&grpc_static_metadata_refcounts[54], {{3, g_bytes + 758}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[55], {{14, g_bytes + 761}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[16], {{15, g_bytes + 186}}},
        {&grpc_static_metadata_refcounts[56], {{13, g_bytes + 775}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[57], {{15, g_bytes + 788}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[58], {{13, g_bytes + 803}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[59], {{6, g_bytes + 816}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[60], {{27, g_bytes + 822}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[61], {{3, g_bytes + 849}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[62], {{5, g_bytes + 852}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[63], {{13, g_bytes + 857}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[64], {{13, g_bytes + 870}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[65], {{19, g_bytes + 883}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[15], {{16, g_bytes + 170}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[66], {{16, g_bytes + 902}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[67], {{14, g_bytes + 918}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[68], {{16, g_bytes + 932}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[69], {{13, g_bytes + 948}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[14], {{12, g_bytes + 158}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[70], {{6, g_bytes + 961}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[71], {{4, g_bytes + 967}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[72], {{4, g_bytes + 971}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[73], {{6, g_bytes + 975}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[74], {{7, g_bytes + 981}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[75], {{4, g_bytes + 988}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[20], {{4, g_bytes + 278}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[76], {{8, g_bytes + 992}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[77], {{17, g_bytes + 1000}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[78], {{13, g_bytes + 1017}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[79], {{8, g_bytes + 1030}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[80], {{19, g_bytes + 1038}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[81], {{13, g_bytes + 1057}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[82], {{4, g_bytes + 1070}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[83], {{8, g_bytes + 1074}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[84], {{12, g_bytes + 1082}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[85], {{18, g_bytes + 1094}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[86], {{19, g_bytes + 1112}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[87], {{5, g_bytes + 1131}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[88], {{7, g_bytes + 1136}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[89], {{7, g_bytes + 1143}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[90], {{11, g_bytes + 1150}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[91], {{6, g_bytes + 1161}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[92], {{10, g_bytes + 1167}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[93], {{25, g_bytes + 1177}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[94], {{17, g_bytes + 1202}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[19], {{10, g_bytes + 268}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[95], {{4, g_bytes + 1219}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[96], {{3, g_bytes + 1223}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[97], {{16, g_bytes + 1226}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[7], {{11, g_bytes + 50}}},
        {&grpc_static_metadata_refcounts[98], {{1, g_bytes + 1242}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[7], {{11, g_bytes + 50}}},
        {&grpc_static_metadata_refcounts[25], {{1, g_bytes + 350}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[7], {{11, g_bytes + 50}}},
        {&grpc_static_metadata_refcounts[26], {{1, g_bytes + 351}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[9], {{13, g_bytes + 77}}},
        {&grpc_static_metadata_refcounts[99], {{8, g_bytes + 1243}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[9], {{13, g_bytes + 77}}},
        {&grpc_static_metadata_refcounts[40], {{4, g_bytes + 697}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[9], {{13, g_bytes + 77}}},
        {&grpc_static_metadata_refcounts[39], {{7, g_bytes + 690}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[5], {{2, g_bytes + 36}}},
        {&grpc_static_metadata_refcounts[100], {{8, g_bytes + 1251}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[14], {{12, g_bytes + 158}}},
        {&grpc_static_metadata_refcounts[101], {{16, g_bytes + 1259}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[4], {{7, g_bytes + 29}}},
        {&grpc_static_metadata_refcounts[102], {{4, g_bytes + 1275}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[1], {{7, g_bytes + 5}}},
        {&grpc_static_metadata_refcounts[103], {{3, g_bytes + 1279}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[16], {{15, g_bytes + 186}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[15], {{16, g_bytes + 170}}},
        {&grpc_static_metadata_refcounts[99], {{8, g_bytes + 1243}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[15], {{16, g_bytes + 170}}},
        {&grpc_static_metadata_refcounts[40], {{4, g_bytes + 697}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[21], {{8, g_bytes + 282}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[104], {{11, g_bytes + 1282}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[105], {{14, g_bytes + 1293}}},
        {&grpc_static_metadata_refcounts[29], {{0, g_bytes + 354}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[99], {{8, g_bytes + 1243}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[39], {{7, g_bytes + 690}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[106], {{16, g_bytes + 1307}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[40], {{4, g_bytes + 697}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[107], {{13, g_bytes + 1323}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[108], {{12, g_bytes + 1336}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[10], {{20, g_bytes + 90}}},
        {&grpc_static_metadata_refcounts[109], {{21, g_bytes + 1348}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[16], {{15, g_bytes + 186}}},
        {&grpc_static_metadata_refcounts[99], {{8, g_bytes + 1243}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[16], {{15, g_bytes + 186}}},
        {&grpc_static_metadata_refcounts[40], {{4, g_bytes + 697}}}),
    grpc_core::StaticMetadata(
        {&grpc_static_metadata_refcounts[16], {{15, g_bytes + 186}}},
        {&grpc_static_metadata_refcounts[107], {{13, g_bytes + 1323}}}),
};
const uint8_t grpc_static_accept_encoding_metadata[8] = {0,  77, 78, 79,
                                                         80, 81, 82, 83};

const uint8_t grpc_static_accept_stream_encoding_metadata[4] = {0, 84, 85, 86};
