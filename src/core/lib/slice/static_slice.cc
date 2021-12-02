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

#include "src/core/lib/slice/static_slice.h"

namespace grpc_core {
const uint8_t g_static_metadata_bytes[] = {
    103, 114, 112, 99,  45,  116, 105, 109, 101, 111, 117, 116, 47,  103, 114,
    112, 99,  46,  108, 98,  46,  118, 49,  46,  76,  111, 97,  100, 66,  97,
    108, 97,  110, 99,  101, 114, 47,  66,  97,  108, 97,  110, 99,  101, 76,
    111, 97,  100, 47,  101, 110, 118, 111, 121, 46,  115, 101, 114, 118, 105,
    99,  101, 46,  108, 111, 97,  100, 95,  115, 116, 97,  116, 115, 46,  118,
    50,  46,  76,  111, 97,  100, 82,  101, 112, 111, 114, 116, 105, 110, 103,
    83,  101, 114, 118, 105, 99,  101, 47,  83,  116, 114, 101, 97,  109, 76,
    111, 97,  100, 83,  116, 97,  116, 115, 47,  101, 110, 118, 111, 121, 46,
    115, 101, 114, 118, 105, 99,  101, 46,  108, 111, 97,  100, 95,  115, 116,
    97,  116, 115, 46,  118, 51,  46,  76,  111, 97,  100, 82,  101, 112, 111,
    114, 116, 105, 110, 103, 83,  101, 114, 118, 105, 99,  101, 47,  83,  116,
    114, 101, 97,  109, 76,  111, 97,  100, 83,  116, 97,  116, 115, 47,  103,
    114, 112, 99,  46,  104, 101, 97,  108, 116, 104, 46,  118, 49,  46,  72,
    101, 97,  108, 116, 104, 47,  87,  97,  116, 99,  104, 47,  101, 110, 118,
    111, 121, 46,  115, 101, 114, 118, 105, 99,  101, 46,  100, 105, 115, 99,
    111, 118, 101, 114, 121, 46,  118, 50,  46,  65,  103, 103, 114, 101, 103,
    97,  116, 101, 100, 68,  105, 115, 99,  111, 118, 101, 114, 121, 83,  101,
    114, 118, 105, 99,  101, 47,  83,  116, 114, 101, 97,  109, 65,  103, 103,
    114, 101, 103, 97,  116, 101, 100, 82,  101, 115, 111, 117, 114, 99,  101,
    115, 47,  101, 110, 118, 111, 121, 46,  115, 101, 114, 118, 105, 99,  101,
    46,  100, 105, 115, 99,  111, 118, 101, 114, 121, 46,  118, 51,  46,  65,
    103, 103, 114, 101, 103, 97,  116, 101, 100, 68,  105, 115, 99,  111, 118,
    101, 114, 121, 83,  101, 114, 118, 105, 99,  101, 47,  83,  116, 114, 101,
    97,  109, 65,  103, 103, 114, 101, 103, 97,  116, 101, 100, 82,  101, 115,
    111, 117, 114, 99,  101, 115, 116, 101, 116, 114, 97,  105, 108, 101, 114,
    115};

grpc_slice_refcount StaticSliceRefcount::kStaticSubRefcount;

StaticSliceRefcount g_static_metadata_slice_refcounts[GRPC_STATIC_MDSTR_COUNT] =
    {

        StaticSliceRefcount(0), StaticSliceRefcount(1), StaticSliceRefcount(2),
        StaticSliceRefcount(3), StaticSliceRefcount(4), StaticSliceRefcount(5),
        StaticSliceRefcount(6), StaticSliceRefcount(7), StaticSliceRefcount(8),
        StaticSliceRefcount(9),
};

const StaticMetadataSlice
    g_static_metadata_slice_table[GRPC_STATIC_MDSTR_COUNT] = {

        StaticMetadataSlice(&g_static_metadata_slice_refcounts[0].base, 12,
                            g_static_metadata_bytes + 0),
        StaticMetadataSlice(&g_static_metadata_slice_refcounts[1].base, 0,
                            g_static_metadata_bytes + 12),
        StaticMetadataSlice(&g_static_metadata_slice_refcounts[2].base, 36,
                            g_static_metadata_bytes + 12),
        StaticMetadataSlice(&g_static_metadata_slice_refcounts[3].base, 65,
                            g_static_metadata_bytes + 48),
        StaticMetadataSlice(&g_static_metadata_slice_refcounts[4].base, 65,
                            g_static_metadata_bytes + 113),
        StaticMetadataSlice(&g_static_metadata_slice_refcounts[5].base, 28,
                            g_static_metadata_bytes + 178),
        StaticMetadataSlice(&g_static_metadata_slice_refcounts[6].base, 80,
                            g_static_metadata_bytes + 206),
        StaticMetadataSlice(&g_static_metadata_slice_refcounts[7].base, 80,
                            g_static_metadata_bytes + 286),
        StaticMetadataSlice(&g_static_metadata_slice_refcounts[8].base, 2,
                            g_static_metadata_bytes + 366),
        StaticMetadataSlice(&g_static_metadata_slice_refcounts[9].base, 8,
                            g_static_metadata_bytes + 368),
};
