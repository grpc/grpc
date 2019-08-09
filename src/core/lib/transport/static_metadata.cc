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
    99,  45,  112, 114, 101, 118, 105, 111, 117, 115, 45,  114, 112, 99,  45,
    97,  116, 116, 101, 109, 112, 116, 115, 103, 114, 112, 99,  45,  114, 101,
    116, 114, 121, 45,  112, 117, 115, 104, 98,  97,  99,  107, 45,  109, 115,
    103, 114, 112, 99,  45,  116, 105, 109, 101, 111, 117, 116, 49,  50,  51,
    52,  103, 114, 112, 99,  46,  119, 97,  105, 116, 95,  102, 111, 114, 95,
    114, 101, 97,  100, 121, 103, 114, 112, 99,  46,  116, 105, 109, 101, 111,
    117, 116, 103, 114, 112, 99,  46,  109, 97,  120, 95,  114, 101, 113, 117,
    101, 115, 116, 95,  109, 101, 115, 115, 97,  103, 101, 95,  98,  121, 116,
    101, 115, 103, 114, 112, 99,  46,  109, 97,  120, 95,  114, 101, 115, 112,
    111, 110, 115, 101, 95,  109, 101, 115, 115, 97,  103, 101, 95,  98,  121,
    116, 101, 115, 47,  103, 114, 112, 99,  46,  108, 98,  46,  118, 49,  46,
    76,  111, 97,  100, 66,  97,  108, 97,  110, 99,  101, 114, 47,  66,  97,
    108, 97,  110, 99,  101, 76,  111, 97,  100, 47,  101, 110, 118, 111, 121,
    46,  115, 101, 114, 118, 105, 99,  101, 46,  108, 111, 97,  100, 95,  115,
    116, 97,  116, 115, 46,  118, 50,  46,  76,  111, 97,  100, 82,  101, 112,
    111, 114, 116, 105, 110, 103, 83,  101, 114, 118, 105, 99,  101, 47,  83,
    116, 114, 101, 97,  109, 76,  111, 97,  100, 83,  116, 97,  116, 115, 47,
    101, 110, 118, 111, 121, 46,  97,  112, 105, 46,  118, 50,  46,  69,  110,
    100, 112, 111, 105, 110, 116, 68,  105, 115, 99,  111, 118, 101, 114, 121,
    83,  101, 114, 118, 105, 99,  101, 47,  83,  116, 114, 101, 97,  109, 69,
    110, 100, 112, 111, 105, 110, 116, 115, 47,  103, 114, 112, 99,  46,  104,
    101, 97,  108, 116, 104, 46,  118, 49,  46,  72,  101, 97,  108, 116, 104,
    47,  87,  97,  116, 99,  104, 47,  101, 110, 118, 111, 121, 46,  115, 101,
    114, 118, 105, 99,  101, 46,  100, 105, 115, 99,  111, 118, 101, 114, 121,
    46,  118, 50,  46,  65,  103, 103, 114, 101, 103, 97,  116, 101, 100, 68,
    105, 115, 99,  111, 118, 101, 114, 121, 83,  101, 114, 118, 105, 99,  101,
    47,  83,  116, 114, 101, 97,  109, 65,  103, 103, 114, 101, 103, 97,  116,
    101, 100, 82,  101, 115, 111, 117, 114, 99,  101, 115, 100, 101, 102, 108,
    97,  116, 101, 103, 122, 105, 112, 115, 116, 114, 101, 97,  109, 47,  103,
    122, 105, 112, 71,  69,  84,  80,  79,  83,  84,  47,  47,  105, 110, 100,
    101, 120, 46,  104, 116, 109, 108, 104, 116, 116, 112, 104, 116, 116, 112,
    115, 50,  48,  48,  50,  48,  52,  50,  48,  54,  51,  48,  52,  52,  48,
    48,  52,  48,  52,  53,  48,  48,  97,  99,  99,  101, 112, 116, 45,  99,
    104, 97,  114, 115, 101, 116, 103, 122, 105, 112, 44,  32,  100, 101, 102,
    108, 97,  116, 101, 97,  99,  99,  101, 112, 116, 45,  108, 97,  110, 103,
    117, 97,  103, 101, 97,  99,  99,  101, 112, 116, 45,  114, 97,  110, 103,
    101, 115, 97,  99,  99,  101, 112, 116, 97,  99,  99,  101, 115, 115, 45,
    99,  111, 110, 116, 114, 111, 108, 45,  97,  108, 108, 111, 119, 45,  111,
    114, 105, 103, 105, 110, 97,  103, 101, 97,  108, 108, 111, 119, 97,  117,
    116, 104, 111, 114, 105, 122, 97,  116, 105, 111, 110, 99,  97,  99,  104,
    101, 45,  99,  111, 110, 116, 114, 111, 108, 99,  111, 110, 116, 101, 110,
    116, 45,  100, 105, 115, 112, 111, 115, 105, 116, 105, 111, 110, 99,  111,
    110, 116, 101, 110, 116, 45,  108, 97,  110, 103, 117, 97,  103, 101, 99,
    111, 110, 116, 101, 110, 116, 45,  108, 101, 110, 103, 116, 104, 99,  111,
    110, 116, 101, 110, 116, 45,  108, 111, 99,  97,  116, 105, 111, 110, 99,
    111, 110, 116, 101, 110, 116, 45,  114, 97,  110, 103, 101, 99,  111, 111,
    107, 105, 101, 100, 97,  116, 101, 101, 116, 97,  103, 101, 120, 112, 101,
    99,  116, 101, 120, 112, 105, 114, 101, 115, 102, 114, 111, 109, 105, 102,
    45,  109, 97,  116, 99,  104, 105, 102, 45,  109, 111, 100, 105, 102, 105,
    101, 100, 45,  115, 105, 110, 99,  101, 105, 102, 45,  110, 111, 110, 101,
    45,  109, 97,  116, 99,  104, 105, 102, 45,  114, 97,  110, 103, 101, 105,
    102, 45,  117, 110, 109, 111, 100, 105, 102, 105, 101, 100, 45,  115, 105,
    110, 99,  101, 108, 97,  115, 116, 45,  109, 111, 100, 105, 102, 105, 101,
    100, 108, 105, 110, 107, 108, 111, 99,  97,  116, 105, 111, 110, 109, 97,
    120, 45,  102, 111, 114, 119, 97,  114, 100, 115, 112, 114, 111, 120, 121,
    45,  97,  117, 116, 104, 101, 110, 116, 105, 99,  97,  116, 101, 112, 114,
    111, 120, 121, 45,  97,  117, 116, 104, 111, 114, 105, 122, 97,  116, 105,
    111, 110, 114, 97,  110, 103, 101, 114, 101, 102, 101, 114, 101, 114, 114,
    101, 102, 114, 101, 115, 104, 114, 101, 116, 114, 121, 45,  97,  102, 116,
    101, 114, 115, 101, 114, 118, 101, 114, 115, 101, 116, 45,  99,  111, 111,
    107, 105, 101, 115, 116, 114, 105, 99,  116, 45,  116, 114, 97,  110, 115,
    112, 111, 114, 116, 45,  115, 101, 99,  117, 114, 105, 116, 121, 116, 114,
    97,  110, 115, 102, 101, 114, 45,  101, 110, 99,  111, 100, 105, 110, 103,
    118, 97,  114, 121, 118, 105, 97,  119, 119, 119, 45,  97,  117, 116, 104,
    101, 110, 116, 105, 99,  97,  116, 101, 48,  105, 100, 101, 110, 116, 105,
    116, 121, 116, 114, 97,  105, 108, 101, 114, 115, 97,  112, 112, 108, 105,
    99,  97,  116, 105, 111, 110, 47,  103, 114, 112, 99,  103, 114, 112, 99,
    80,  85,  84,  108, 98,  45,  99,  111, 115, 116, 45,  98,  105, 110, 105,
    100, 101, 110, 116, 105, 116, 121, 44,  100, 101, 102, 108, 97,  116, 101,
    105, 100, 101, 110, 116, 105, 116, 121, 44,  103, 122, 105, 112, 100, 101,
    102, 108, 97,  116, 101, 44,  103, 122, 105, 112, 105, 100, 101, 110, 116,
    105, 116, 121, 44,  100, 101, 102, 108, 97,  116, 101, 44,  103, 122, 105,
    112};

grpc_slice_refcount grpc_core::StaticSliceRefcount::kStaticSubRefcount;
grpc_core::StaticSliceRefcount
    grpc_static_metadata_refcounts[GRPC_STATIC_MDSTR_COUNT] = {
        grpc_core::StaticSliceRefcount(0),
        grpc_core::StaticSliceRefcount(1),
        grpc_core::StaticSliceRefcount(2),
        grpc_core::StaticSliceRefcount(3),
        grpc_core::StaticSliceRefcount(4),
        grpc_core::StaticSliceRefcount(5),
        grpc_core::StaticSliceRefcount(6),
        grpc_core::StaticSliceRefcount(7),
        grpc_core::StaticSliceRefcount(8),
        grpc_core::StaticSliceRefcount(9),
        grpc_core::StaticSliceRefcount(10),
        grpc_core::StaticSliceRefcount(11),
        grpc_core::StaticSliceRefcount(12),
        grpc_core::StaticSliceRefcount(13),
        grpc_core::StaticSliceRefcount(14),
        grpc_core::StaticSliceRefcount(15),
        grpc_core::StaticSliceRefcount(16),
        grpc_core::StaticSliceRefcount(17),
        grpc_core::StaticSliceRefcount(18),
        grpc_core::StaticSliceRefcount(19),
        grpc_core::StaticSliceRefcount(20),
        grpc_core::StaticSliceRefcount(21),
        grpc_core::StaticSliceRefcount(22),
        grpc_core::StaticSliceRefcount(23),
        grpc_core::StaticSliceRefcount(24),
        grpc_core::StaticSliceRefcount(25),
        grpc_core::StaticSliceRefcount(26),
        grpc_core::StaticSliceRefcount(27),
        grpc_core::StaticSliceRefcount(28),
        grpc_core::StaticSliceRefcount(29),
        grpc_core::StaticSliceRefcount(30),
        grpc_core::StaticSliceRefcount(31),
        grpc_core::StaticSliceRefcount(32),
        grpc_core::StaticSliceRefcount(33),
        grpc_core::StaticSliceRefcount(34),
        grpc_core::StaticSliceRefcount(35),
        grpc_core::StaticSliceRefcount(36),
        grpc_core::StaticSliceRefcount(37),
        grpc_core::StaticSliceRefcount(38),
        grpc_core::StaticSliceRefcount(39),
        grpc_core::StaticSliceRefcount(40),
        grpc_core::StaticSliceRefcount(41),
        grpc_core::StaticSliceRefcount(42),
        grpc_core::StaticSliceRefcount(43),
        grpc_core::StaticSliceRefcount(44),
        grpc_core::StaticSliceRefcount(45),
        grpc_core::StaticSliceRefcount(46),
        grpc_core::StaticSliceRefcount(47),
        grpc_core::StaticSliceRefcount(48),
        grpc_core::StaticSliceRefcount(49),
        grpc_core::StaticSliceRefcount(50),
        grpc_core::StaticSliceRefcount(51),
        grpc_core::StaticSliceRefcount(52),
        grpc_core::StaticSliceRefcount(53),
        grpc_core::StaticSliceRefcount(54),
        grpc_core::StaticSliceRefcount(55),
        grpc_core::StaticSliceRefcount(56),
        grpc_core::StaticSliceRefcount(57),
        grpc_core::StaticSliceRefcount(58),
        grpc_core::StaticSliceRefcount(59),
        grpc_core::StaticSliceRefcount(60),
        grpc_core::StaticSliceRefcount(61),
        grpc_core::StaticSliceRefcount(62),
        grpc_core::StaticSliceRefcount(63),
        grpc_core::StaticSliceRefcount(64),
        grpc_core::StaticSliceRefcount(65),
        grpc_core::StaticSliceRefcount(66),
        grpc_core::StaticSliceRefcount(67),
        grpc_core::StaticSliceRefcount(68),
        grpc_core::StaticSliceRefcount(69),
        grpc_core::StaticSliceRefcount(70),
        grpc_core::StaticSliceRefcount(71),
        grpc_core::StaticSliceRefcount(72),
        grpc_core::StaticSliceRefcount(73),
        grpc_core::StaticSliceRefcount(74),
        grpc_core::StaticSliceRefcount(75),
        grpc_core::StaticSliceRefcount(76),
        grpc_core::StaticSliceRefcount(77),
        grpc_core::StaticSliceRefcount(78),
        grpc_core::StaticSliceRefcount(79),
        grpc_core::StaticSliceRefcount(80),
        grpc_core::StaticSliceRefcount(81),
        grpc_core::StaticSliceRefcount(82),
        grpc_core::StaticSliceRefcount(83),
        grpc_core::StaticSliceRefcount(84),
        grpc_core::StaticSliceRefcount(85),
        grpc_core::StaticSliceRefcount(86),
        grpc_core::StaticSliceRefcount(87),
        grpc_core::StaticSliceRefcount(88),
        grpc_core::StaticSliceRefcount(89),
        grpc_core::StaticSliceRefcount(90),
        grpc_core::StaticSliceRefcount(91),
        grpc_core::StaticSliceRefcount(92),
        grpc_core::StaticSliceRefcount(93),
        grpc_core::StaticSliceRefcount(94),
        grpc_core::StaticSliceRefcount(95),
        grpc_core::StaticSliceRefcount(96),
        grpc_core::StaticSliceRefcount(97),
        grpc_core::StaticSliceRefcount(98),
        grpc_core::StaticSliceRefcount(99),
        grpc_core::StaticSliceRefcount(100),
        grpc_core::StaticSliceRefcount(101),
        grpc_core::StaticSliceRefcount(102),
        grpc_core::StaticSliceRefcount(103),
        grpc_core::StaticSliceRefcount(104),
        grpc_core::StaticSliceRefcount(105),
        grpc_core::StaticSliceRefcount(106),
        grpc_core::StaticSliceRefcount(107),
};

const grpc_core::StaticMetadataSlice
    grpc_static_slice_table[GRPC_STATIC_MDSTR_COUNT] = {
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[0].base,
                                       5, g_bytes + 0),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[1].base,
                                       7, g_bytes + 5),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[2].base,
                                       7, g_bytes + 12),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[3].base,
                                       10, g_bytes + 19),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[4].base,
                                       7, g_bytes + 29),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[5].base,
                                       2, g_bytes + 36),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[6].base,
                                       12, g_bytes + 38),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[7].base,
                                       11, g_bytes + 50),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[8].base,
                                       16, g_bytes + 61),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[9].base,
                                       13, g_bytes + 77),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[10].base,
                                       20, g_bytes + 90),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[11].base,
                                       21, g_bytes + 110),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[12].base,
                                       13, g_bytes + 131),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[13].base,
                                       14, g_bytes + 144),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[14].base,
                                       12, g_bytes + 158),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[15].base,
                                       16, g_bytes + 170),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[16].base,
                                       15, g_bytes + 186),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[17].base,
                                       30, g_bytes + 201),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[18].base,
                                       37, g_bytes + 231),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[19].base,
                                       10, g_bytes + 268),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[20].base,
                                       4, g_bytes + 278),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[21].base,
                                       26, g_bytes + 282),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[22].base,
                                       22, g_bytes + 308),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[23].base,
                                       12, g_bytes + 330),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[24].base,
                                       1, g_bytes + 342),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[25].base,
                                       1, g_bytes + 343),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[26].base,
                                       1, g_bytes + 344),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[27].base,
                                       1, g_bytes + 345),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[29].base,
                                       19, g_bytes + 346),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[30].base,
                                       12, g_bytes + 365),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[31].base,
                                       30, g_bytes + 377),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[32].base,
                                       31, g_bytes + 407),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[33].base,
                                       36, g_bytes + 438),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[34].base,
                                       65, g_bytes + 474),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[35].base,
                                       54, g_bytes + 539),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[36].base,
                                       28, g_bytes + 593),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[37].base,
                                       80, g_bytes + 621),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[38].base,
                                       7, g_bytes + 701),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[39].base,
                                       4, g_bytes + 708),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[40].base,
                                       11, g_bytes + 712),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[41].base,
                                       3, g_bytes + 723),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[42].base,
                                       4, g_bytes + 726),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[43].base,
                                       1, g_bytes + 730),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[44].base,
                                       11, g_bytes + 731),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[45].base,
                                       4, g_bytes + 742),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[46].base,
                                       5, g_bytes + 746),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[47].base,
                                       3, g_bytes + 751),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[48].base,
                                       3, g_bytes + 754),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[49].base,
                                       3, g_bytes + 757),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[50].base,
                                       3, g_bytes + 760),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[51].base,
                                       3, g_bytes + 763),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[52].base,
                                       3, g_bytes + 766),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[53].base,
                                       3, g_bytes + 769),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[54].base,
                                       14, g_bytes + 772),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[55].base,
                                       13, g_bytes + 786),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[56].base,
                                       15, g_bytes + 799),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[57].base,
                                       13, g_bytes + 814),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[58].base,
                                       6, g_bytes + 827),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[59].base,
                                       27, g_bytes + 833),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[60].base,
                                       3, g_bytes + 860),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[61].base,
                                       5, g_bytes + 863),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[62].base,
                                       13, g_bytes + 868),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[63].base,
                                       13, g_bytes + 881),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[64].base,
                                       19, g_bytes + 894),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[65].base,
                                       16, g_bytes + 913),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[66].base,
                                       14, g_bytes + 929),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[67].base,
                                       16, g_bytes + 943),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[68].base,
                                       13, g_bytes + 959),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[69].base,
                                       6, g_bytes + 972),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[70].base,
                                       4, g_bytes + 978),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[71].base,
                                       4, g_bytes + 982),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[72].base,
                                       6, g_bytes + 986),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[73].base,
                                       7, g_bytes + 992),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[74].base,
                                       4, g_bytes + 999),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[75].base,
                                       8, g_bytes + 1003),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[76].base,
                                       17, g_bytes + 1011),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[77].base,
                                       13, g_bytes + 1028),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[78].base,
                                       8, g_bytes + 1041),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[79].base,
                                       19, g_bytes + 1049),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[80].base,
                                       13, g_bytes + 1068),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[81].base,
                                       4, g_bytes + 1081),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[82].base,
                                       8, g_bytes + 1085),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[83].base,
                                       12, g_bytes + 1093),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[84].base,
                                       18, g_bytes + 1105),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[85].base,
                                       19, g_bytes + 1123),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[86].base,
                                       5, g_bytes + 1142),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[87].base,
                                       7, g_bytes + 1147),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[88].base,
                                       7, g_bytes + 1154),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[89].base,
                                       11, g_bytes + 1161),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[90].base,
                                       6, g_bytes + 1172),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[91].base,
                                       10, g_bytes + 1178),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[92].base,
                                       25, g_bytes + 1188),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[93].base,
                                       17, g_bytes + 1213),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[94].base,
                                       4, g_bytes + 1230),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[95].base,
                                       3, g_bytes + 1234),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[96].base,
                                       16, g_bytes + 1237),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[97].base,
                                       1, g_bytes + 1253),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[98].base,
                                       8, g_bytes + 1254),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[99].base,
                                       8, g_bytes + 1262),
        grpc_core::StaticMetadataSlice(
            &grpc_static_metadata_refcounts[100].base, 16, g_bytes + 1270),
        grpc_core::StaticMetadataSlice(
            &grpc_static_metadata_refcounts[101].base, 4, g_bytes + 1286),
        grpc_core::StaticMetadataSlice(
            &grpc_static_metadata_refcounts[102].base, 3, g_bytes + 1290),
        grpc_core::StaticMetadataSlice(
            &grpc_static_metadata_refcounts[103].base, 11, g_bytes + 1293),
        grpc_core::StaticMetadataSlice(
            &grpc_static_metadata_refcounts[104].base, 16, g_bytes + 1304),
        grpc_core::StaticMetadataSlice(
            &grpc_static_metadata_refcounts[105].base, 13, g_bytes + 1320),
        grpc_core::StaticMetadataSlice(
            &grpc_static_metadata_refcounts[106].base, 12, g_bytes + 1333),
        grpc_core::StaticMetadataSlice(
            &grpc_static_metadata_refcounts[107].base, 21, g_bytes + 1345),
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
    /* GRPC_MDELEM_LB_COST_BIN_EMPTY: 
     "lb-cost-bin": "" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[74].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY: 
     "grpc-accept-encoding": "identity" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[75].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_GRPC_ACCEPT_ENCODING_DEFLATE: 
     "grpc-accept-encoding": "deflate" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[76].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_DEFLATE: 
     "grpc-accept-encoding": "identity,deflate" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[77].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_GRPC_ACCEPT_ENCODING_GZIP: 
     "grpc-accept-encoding": "gzip" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[78].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_GZIP: 
     "grpc-accept-encoding": "identity,gzip" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[79].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_GRPC_ACCEPT_ENCODING_DEFLATE_COMMA_GZIP: 
     "grpc-accept-encoding": "deflate,gzip" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[80].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_DEFLATE_COMMA_GZIP: 
     "grpc-accept-encoding": "identity,deflate,gzip" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[81].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_ACCEPT_ENCODING_IDENTITY: 
     "accept-encoding": "identity" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[82].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_ACCEPT_ENCODING_GZIP: 
     "accept-encoding": "gzip" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[83].data(),
        GRPC_MDELEM_STORAGE_STATIC),
    /* GRPC_MDELEM_ACCEPT_ENCODING_IDENTITY_COMMA_GZIP: 
     "accept-encoding": "identity,gzip" */
    GRPC_MAKE_MDELEM(
        &grpc_static_mdelem_table[84].data(),
        GRPC_MDELEM_STORAGE_STATIC)
    // clang-format on
};
uintptr_t grpc_static_mdelem_user_data[GRPC_STATIC_MDELEM_COUNT] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 4, 4, 6, 6, 8, 8, 2, 4, 4};

static const int8_t elems_r[] = {
    15, 10, -8, 0,  2,  -43, -80, -44, 0,   4,   -8,  0,   0,   0,  6,  -1,
    -8, 0,  0,  3,  2,  0,   0,   0,   0,   0,   0,   0,   0,   0,  0,  0,
    0,  0,  0,  0,  0,  0,   0,   0,   0,   0,   0,   0,   0,   0,  0,  0,
    0,  0,  0,  0,  0,  0,   -65, 0,   -68, -69, -50, -72, -73, 0,  32, 31,
    30, 30, 29, 28, 27, 26,  25,  24,  23,  22,  21,  20,  19,  18, 17, 17,
    16, 15, 14, 13, 12, 11,  10,  9,   8,   7,   6,   5,   4,   3,  4,  3,
    3,  7,  0,  0,  0,  0,   0,   0,   -5,  0};
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
    263,   264,  265,  266,  267,  268,  269,   1118, 1119,  1756,  149,   150,
    477,   478,  1648, 43,   44,   1010, 1011,  1540, 1767,  780,   781,   639,
    853,   1659, 2080, 2188, 5860, 6076, 6184,  6400, 6508,  6616,  6724,  6832,
    1783,  6940, 7048, 7156, 7264, 7372, 7480,  7588, 7696,  7804,  7912,  8020,
    8128,  8236, 8344, 6292, 8452, 8560, 8668,  8776, 8884,  8992,  9100,  9208,
    9316,  9424, 9532, 9640, 9748, 9856, 9964,  1178, 533,   10072, 10180, 210,
    10288, 1184, 1185, 1186, 1187, 1070, 10396, 1826, 11152, 0,     0,     0,
    1718,  0,    1833, 0,    0,    352,  0,     1612, 0,     0,     0,     0,
    0,     0,    0,    0,    0,    0,    0,     0,    0,     0,     0,     0,
    0,     0,    0,    0,    0,    0,    0,     0,    0,     0,     0,     0,
    0,     0,    0,    0,    0,    0,    0,     0,    0,     0,     0,     0,
    0,     0,    0,    0,    0};
static const uint8_t elem_idxs[] = {
    7,  8,   9,   10,  11, 12,  13, 76,  78,  71, 1,   2,  5,  6,  25, 3,
    4,  66,  65,  30,  83, 62,  63, 67,  61,  73, 57,  37, 14, 16, 17, 19,
    20, 21,  22,  23,  15, 24,  26, 27,  28,  29, 31,  32, 33, 34, 35, 36,
    38, 39,  40,  18,  41, 42,  43, 44,  45,  46, 47,  48, 49, 50, 51, 52,
    53, 54,  55,  75,  69, 56,  58, 70,  59,  77, 79,  80, 81, 64, 60, 82,
    74, 255, 255, 255, 72, 255, 84, 255, 255, 0,  255, 68};

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
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[3].base,
                                       10, g_bytes + 19),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        0),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[1].base,
                                       7, g_bytes + 5),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[41].base,
                                       3, g_bytes + 723),
        1),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[1].base,
                                       7, g_bytes + 5),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[42].base,
                                       4, g_bytes + 726),
        2),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[0].base,
                                       5, g_bytes + 0),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[43].base,
                                       1, g_bytes + 730),
        3),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[0].base,
                                       5, g_bytes + 0),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[44].base,
                                       11, g_bytes + 731),
        4),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[4].base,
                                       7, g_bytes + 29),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[45].base,
                                       4, g_bytes + 742),
        5),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[4].base,
                                       7, g_bytes + 29),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[46].base,
                                       5, g_bytes + 746),
        6),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[2].base,
                                       7, g_bytes + 12),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[47].base,
                                       3, g_bytes + 751),
        7),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[2].base,
                                       7, g_bytes + 12),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[48].base,
                                       3, g_bytes + 754),
        8),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[2].base,
                                       7, g_bytes + 12),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[49].base,
                                       3, g_bytes + 757),
        9),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[2].base,
                                       7, g_bytes + 12),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[50].base,
                                       3, g_bytes + 760),
        10),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[2].base,
                                       7, g_bytes + 12),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[51].base,
                                       3, g_bytes + 763),
        11),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[2].base,
                                       7, g_bytes + 12),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[52].base,
                                       3, g_bytes + 766),
        12),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[2].base,
                                       7, g_bytes + 12),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[53].base,
                                       3, g_bytes + 769),
        13),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[54].base,
                                       14, g_bytes + 772),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        14),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[16].base,
                                       15, g_bytes + 186),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[55].base,
                                       13, g_bytes + 786),
        15),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[56].base,
                                       15, g_bytes + 799),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        16),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[57].base,
                                       13, g_bytes + 814),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        17),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[58].base,
                                       6, g_bytes + 827),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        18),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[59].base,
                                       27, g_bytes + 833),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        19),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[60].base,
                                       3, g_bytes + 860),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        20),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[61].base,
                                       5, g_bytes + 863),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        21),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[62].base,
                                       13, g_bytes + 868),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        22),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[63].base,
                                       13, g_bytes + 881),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        23),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[64].base,
                                       19, g_bytes + 894),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        24),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[15].base,
                                       16, g_bytes + 170),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        25),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[65].base,
                                       16, g_bytes + 913),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        26),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[66].base,
                                       14, g_bytes + 929),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        27),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[67].base,
                                       16, g_bytes + 943),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        28),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[68].base,
                                       13, g_bytes + 959),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        29),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[14].base,
                                       12, g_bytes + 158),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        30),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[69].base,
                                       6, g_bytes + 972),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        31),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[70].base,
                                       4, g_bytes + 978),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        32),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[71].base,
                                       4, g_bytes + 982),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        33),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[72].base,
                                       6, g_bytes + 986),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        34),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[73].base,
                                       7, g_bytes + 992),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        35),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[74].base,
                                       4, g_bytes + 999),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        36),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[20].base,
                                       4, g_bytes + 278),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        37),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[75].base,
                                       8, g_bytes + 1003),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        38),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[76].base,
                                       17, g_bytes + 1011),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        39),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[77].base,
                                       13, g_bytes + 1028),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        40),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[78].base,
                                       8, g_bytes + 1041),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        41),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[79].base,
                                       19, g_bytes + 1049),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        42),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[80].base,
                                       13, g_bytes + 1068),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        43),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[81].base,
                                       4, g_bytes + 1081),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        44),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[82].base,
                                       8, g_bytes + 1085),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        45),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[83].base,
                                       12, g_bytes + 1093),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        46),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[84].base,
                                       18, g_bytes + 1105),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        47),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[85].base,
                                       19, g_bytes + 1123),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        48),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[86].base,
                                       5, g_bytes + 1142),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        49),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[87].base,
                                       7, g_bytes + 1147),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        50),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[88].base,
                                       7, g_bytes + 1154),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        51),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[89].base,
                                       11, g_bytes + 1161),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        52),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[90].base,
                                       6, g_bytes + 1172),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        53),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[91].base,
                                       10, g_bytes + 1178),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        54),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[92].base,
                                       25, g_bytes + 1188),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        55),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[93].base,
                                       17, g_bytes + 1213),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        56),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[19].base,
                                       10, g_bytes + 268),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        57),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[94].base,
                                       4, g_bytes + 1230),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        58),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[95].base,
                                       3, g_bytes + 1234),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        59),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[96].base,
                                       16, g_bytes + 1237),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        60),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[7].base,
                                       11, g_bytes + 50),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[97].base,
                                       1, g_bytes + 1253),
        61),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[7].base,
                                       11, g_bytes + 50),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[24].base,
                                       1, g_bytes + 342),
        62),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[7].base,
                                       11, g_bytes + 50),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[25].base,
                                       1, g_bytes + 343),
        63),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[9].base,
                                       13, g_bytes + 77),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[98].base,
                                       8, g_bytes + 1254),
        64),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[9].base,
                                       13, g_bytes + 77),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[39].base,
                                       4, g_bytes + 708),
        65),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[9].base,
                                       13, g_bytes + 77),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[38].base,
                                       7, g_bytes + 701),
        66),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[5].base,
                                       2, g_bytes + 36),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[99].base,
                                       8, g_bytes + 1262),
        67),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[14].base,
                                       12, g_bytes + 158),
        grpc_core::StaticMetadataSlice(
            &grpc_static_metadata_refcounts[100].base, 16, g_bytes + 1270),
        68),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[4].base,
                                       7, g_bytes + 29),
        grpc_core::StaticMetadataSlice(
            &grpc_static_metadata_refcounts[101].base, 4, g_bytes + 1286),
        69),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[1].base,
                                       7, g_bytes + 5),
        grpc_core::StaticMetadataSlice(
            &grpc_static_metadata_refcounts[102].base, 3, g_bytes + 1290),
        70),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[16].base,
                                       15, g_bytes + 186),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        71),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[15].base,
                                       16, g_bytes + 170),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[98].base,
                                       8, g_bytes + 1254),
        72),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[15].base,
                                       16, g_bytes + 170),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[39].base,
                                       4, g_bytes + 708),
        73),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(
            &grpc_static_metadata_refcounts[103].base, 11, g_bytes + 1293),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[28].base,
                                       0, g_bytes + 346),
        74),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[10].base,
                                       20, g_bytes + 90),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[98].base,
                                       8, g_bytes + 1254),
        75),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[10].base,
                                       20, g_bytes + 90),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[38].base,
                                       7, g_bytes + 701),
        76),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[10].base,
                                       20, g_bytes + 90),
        grpc_core::StaticMetadataSlice(
            &grpc_static_metadata_refcounts[104].base, 16, g_bytes + 1304),
        77),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[10].base,
                                       20, g_bytes + 90),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[39].base,
                                       4, g_bytes + 708),
        78),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[10].base,
                                       20, g_bytes + 90),
        grpc_core::StaticMetadataSlice(
            &grpc_static_metadata_refcounts[105].base, 13, g_bytes + 1320),
        79),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[10].base,
                                       20, g_bytes + 90),
        grpc_core::StaticMetadataSlice(
            &grpc_static_metadata_refcounts[106].base, 12, g_bytes + 1333),
        80),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[10].base,
                                       20, g_bytes + 90),
        grpc_core::StaticMetadataSlice(
            &grpc_static_metadata_refcounts[107].base, 21, g_bytes + 1345),
        81),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[16].base,
                                       15, g_bytes + 186),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[98].base,
                                       8, g_bytes + 1254),
        82),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[16].base,
                                       15, g_bytes + 186),
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[39].base,
                                       4, g_bytes + 708),
        83),
    grpc_core::StaticMetadata(
        grpc_core::StaticMetadataSlice(&grpc_static_metadata_refcounts[16].base,
                                       15, g_bytes + 186),
        grpc_core::StaticMetadataSlice(
            &grpc_static_metadata_refcounts[105].base, 13, g_bytes + 1320),
        84),
};
const uint8_t grpc_static_accept_encoding_metadata[8] = {0,  75, 76, 77,
                                                         78, 79, 80, 81};

const uint8_t grpc_static_accept_stream_encoding_metadata[4] = {0, 82, 83, 84};
