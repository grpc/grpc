// Copyright 2019 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// -----------------------------------------------------------------------------
// transform_args.h
// -----------------------------------------------------------------------------
//
// This file contains a higher-order macro that "transforms" each element of a
// a variadic argument by a provided secondary macro.

#ifndef ABSL_TYPES_INTERNAL_TRANSFORM_ARGS_H_
#define ABSL_TYPES_INTERNAL_TRANSFORM_ARGS_H_

//
// ABSL_INTERNAL_CAT(a, b)
//
// This macro takes two arguments and concatenates them together via ## after
// expansion.
//
// Example:
//
//   ABSL_INTERNAL_CAT(foo_, bar)
//
// Results in:
//
//   foo_bar
#define ABSL_INTERNAL_CAT(a, b) ABSL_INTERNAL_CAT_IMPL(a, b)
#define ABSL_INTERNAL_CAT_IMPL(a, b) a##b

//
// ABSL_INTERNAL_TRANSFORM_ARGS(m, ...)
//
// This macro takes another macro as an argument followed by a trailing series
// of additional parameters (up to 32 additional arguments). It invokes the
// passed-in macro once for each of the additional arguments, with the
// expansions separated by commas.
//
// Example:
//
//   ABSL_INTERNAL_TRANSFORM_ARGS(MY_MACRO, a, b, c)
//
// Results in:
//
//   MY_MACRO(a), MY_MACRO(b), MY_MACRO(c)
//
// TODO(calabrese) Handle no arguments as a special case.
#define ABSL_INTERNAL_TRANSFORM_ARGS(m, ...)             \
  ABSL_INTERNAL_CAT(ABSL_INTERNAL_TRANSFORM_ARGS,        \
                    ABSL_INTERNAL_NUM_ARGS(__VA_ARGS__)) \
  (m, __VA_ARGS__)

#define ABSL_INTERNAL_TRANSFORM_ARGS1(m, a0) m(a0)

#define ABSL_INTERNAL_TRANSFORM_ARGS2(m, a0, a1) m(a0), m(a1)

#define ABSL_INTERNAL_TRANSFORM_ARGS3(m, a0, a1, a2) m(a0), m(a1), m(a2)

#define ABSL_INTERNAL_TRANSFORM_ARGS4(m, a0, a1, a2, a3) \
  m(a0), m(a1), m(a2), m(a3)

#define ABSL_INTERNAL_TRANSFORM_ARGS5(m, a0, a1, a2, a3, a4) \
  m(a0), m(a1), m(a2), m(a3), m(a4)

#define ABSL_INTERNAL_TRANSFORM_ARGS6(m, a0, a1, a2, a3, a4, a5) \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5)

#define ABSL_INTERNAL_TRANSFORM_ARGS7(m, a0, a1, a2, a3, a4, a5, a6) \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6)

#define ABSL_INTERNAL_TRANSFORM_ARGS8(m, a0, a1, a2, a3, a4, a5, a6, a7) \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7)

#define ABSL_INTERNAL_TRANSFORM_ARGS9(m, a0, a1, a2, a3, a4, a5, a6, a7, a8) \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8)

#define ABSL_INTERNAL_TRANSFORM_ARGS10(m, a0, a1, a2, a3, a4, a5, a6, a7, a8, \
                                       a9)                                    \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8), m(a9)

#define ABSL_INTERNAL_TRANSFORM_ARGS11(m, a0, a1, a2, a3, a4, a5, a6, a7, a8, \
                                       a9, a10)                               \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8), m(a9), m(a10)

#define ABSL_INTERNAL_TRANSFORM_ARGS12(m, a0, a1, a2, a3, a4, a5, a6, a7, a8, \
                                       a9, a10, a11)                          \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8), m(a9),       \
      m(a10), m(a11)

#define ABSL_INTERNAL_TRANSFORM_ARGS13(m, a0, a1, a2, a3, a4, a5, a6, a7, a8, \
                                       a9, a10, a11, a12)                     \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8), m(a9),       \
      m(a10), m(a11), m(a12)

#define ABSL_INTERNAL_TRANSFORM_ARGS14(m, a0, a1, a2, a3, a4, a5, a6, a7, a8, \
                                       a9, a10, a11, a12, a13)                \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8), m(a9),       \
      m(a10), m(a11), m(a12), m(a13)

#define ABSL_INTERNAL_TRANSFORM_ARGS15(m, a0, a1, a2, a3, a4, a5, a6, a7, a8, \
                                       a9, a10, a11, a12, a13, a14)           \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8), m(a9),       \
      m(a10), m(a11), m(a12), m(a13), m(a14)

#define ABSL_INTERNAL_TRANSFORM_ARGS16(m, a0, a1, a2, a3, a4, a5, a6, a7, a8, \
                                       a9, a10, a11, a12, a13, a14, a15)      \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8), m(a9),       \
      m(a10), m(a11), m(a12), m(a13), m(a14), m(a15)

#define ABSL_INTERNAL_TRANSFORM_ARGS17(m, a0, a1, a2, a3, a4, a5, a6, a7, a8, \
                                       a9, a10, a11, a12, a13, a14, a15, a16) \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8), m(a9),       \
      m(a10), m(a11), m(a12), m(a13), m(a14), m(a15), m(a16)

#define ABSL_INTERNAL_TRANSFORM_ARGS18(m, a0, a1, a2, a3, a4, a5, a6, a7, a8, \
                                       a9, a10, a11, a12, a13, a14, a15, a16, \
                                       a17)                                   \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8), m(a9),       \
      m(a10), m(a11), m(a12), m(a13), m(a14), m(a15), m(a16), m(a17)

#define ABSL_INTERNAL_TRANSFORM_ARGS19(m, a0, a1, a2, a3, a4, a5, a6, a7, a8, \
                                       a9, a10, a11, a12, a13, a14, a15, a16, \
                                       a17, a18)                              \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8), m(a9),       \
      m(a10), m(a11), m(a12), m(a13), m(a14), m(a15), m(a16), m(a17), m(a18)

#define ABSL_INTERNAL_TRANSFORM_ARGS20(m, a0, a1, a2, a3, a4, a5, a6, a7, a8, \
                                       a9, a10, a11, a12, a13, a14, a15, a16, \
                                       a17, a18, a19)                         \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8), m(a9),       \
      m(a10), m(a11), m(a12), m(a13), m(a14), m(a15), m(a16), m(a17), m(a18), \
      m(a19)

#define ABSL_INTERNAL_TRANSFORM_ARGS21(m, a0, a1, a2, a3, a4, a5, a6, a7, a8, \
                                       a9, a10, a11, a12, a13, a14, a15, a16, \
                                       a17, a18, a19, a20)                    \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8), m(a9),       \
      m(a10), m(a11), m(a12), m(a13), m(a14), m(a15), m(a16), m(a17), m(a18), \
      m(a19), m(a20)

#define ABSL_INTERNAL_TRANSFORM_ARGS22(m, a0, a1, a2, a3, a4, a5, a6, a7, a8, \
                                       a9, a10, a11, a12, a13, a14, a15, a16, \
                                       a17, a18, a19, a20, a21)               \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8), m(a9),       \
      m(a10), m(a11), m(a12), m(a13), m(a14), m(a15), m(a16), m(a17), m(a18), \
      m(a19), m(a20), m(a21)

#define ABSL_INTERNAL_TRANSFORM_ARGS23(m, a0, a1, a2, a3, a4, a5, a6, a7, a8, \
                                       a9, a10, a11, a12, a13, a14, a15, a16, \
                                       a17, a18, a19, a20, a21, a22)          \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8), m(a9),       \
      m(a10), m(a11), m(a12), m(a13), m(a14), m(a15), m(a16), m(a17), m(a18), \
      m(a19), m(a20), m(a21), m(a22)

#define ABSL_INTERNAL_TRANSFORM_ARGS24(m, a0, a1, a2, a3, a4, a5, a6, a7, a8, \
                                       a9, a10, a11, a12, a13, a14, a15, a16, \
                                       a17, a18, a19, a20, a21, a22, a23)     \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8), m(a9),       \
      m(a10), m(a11), m(a12), m(a13), m(a14), m(a15), m(a16), m(a17), m(a18), \
      m(a19), m(a20), m(a21), m(a22), m(a23)

#define ABSL_INTERNAL_TRANSFORM_ARGS25(m, a0, a1, a2, a3, a4, a5, a6, a7, a8,  \
                                       a9, a10, a11, a12, a13, a14, a15, a16,  \
                                       a17, a18, a19, a20, a21, a22, a23, a24) \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8), m(a9),        \
      m(a10), m(a11), m(a12), m(a13), m(a14), m(a15), m(a16), m(a17), m(a18),  \
      m(a19), m(a20), m(a21), m(a22), m(a23), m(a24)

#define ABSL_INTERNAL_TRANSFORM_ARGS26(                                       \
    m, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,  \
    a16, a17, a18, a19, a20, a21, a22, a23, a24, a25)                         \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8), m(a9),       \
      m(a10), m(a11), m(a12), m(a13), m(a14), m(a15), m(a16), m(a17), m(a18), \
      m(a19), m(a20), m(a21), m(a22), m(a23), m(a24), m(a25)

#define ABSL_INTERNAL_TRANSFORM_ARGS27(                                       \
    m, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,  \
    a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26)                    \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8), m(a9),       \
      m(a10), m(a11), m(a12), m(a13), m(a14), m(a15), m(a16), m(a17), m(a18), \
      m(a19), m(a20), m(a21), m(a22), m(a23), m(a24), m(a25), m(a26)

#define ABSL_INTERNAL_TRANSFORM_ARGS28(                                       \
    m, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,  \
    a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27)               \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8), m(a9),       \
      m(a10), m(a11), m(a12), m(a13), m(a14), m(a15), m(a16), m(a17), m(a18), \
      m(a19), m(a20), m(a21), m(a22), m(a23), m(a24), m(a25), m(a26), m(a27)

#define ABSL_INTERNAL_TRANSFORM_ARGS29(                                       \
    m, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,  \
    a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28)          \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8), m(a9),       \
      m(a10), m(a11), m(a12), m(a13), m(a14), m(a15), m(a16), m(a17), m(a18), \
      m(a19), m(a20), m(a21), m(a22), m(a23), m(a24), m(a25), m(a26), m(a27), \
      m(a28)

#define ABSL_INTERNAL_TRANSFORM_ARGS30(                                       \
    m, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,  \
    a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29)     \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8), m(a9),       \
      m(a10), m(a11), m(a12), m(a13), m(a14), m(a15), m(a16), m(a17), m(a18), \
      m(a19), m(a20), m(a21), m(a22), m(a23), m(a24), m(a25), m(a26), m(a27), \
      m(a28), m(a29)

#define ABSL_INTERNAL_TRANSFORM_ARGS31(                                        \
    m, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15,   \
    a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30) \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8), m(a9),        \
      m(a10), m(a11), m(a12), m(a13), m(a14), m(a15), m(a16), m(a17), m(a18),  \
      m(a19), m(a20), m(a21), m(a22), m(a23), m(a24), m(a25), m(a26), m(a27),  \
      m(a28), m(a29), m(a30)

#define ABSL_INTERNAL_TRANSFORM_ARGS32(m, a0, a1, a2, a3, a4, a5, a6, a7, a8,  \
                                       a9, a10, a11, a12, a13, a14, a15, a16,  \
                                       a17, a18, a19, a20, a21, a22, a23, a24, \
                                       a25, a26, a27, a28, a29, a30, a31)      \
  m(a0), m(a1), m(a2), m(a3), m(a4), m(a5), m(a6), m(a7), m(a8), m(a9),        \
      m(a10), m(a11), m(a12), m(a13), m(a14), m(a15), m(a16), m(a17), m(a18),  \
      m(a19), m(a20), m(a21), m(a22), m(a23), m(a24), m(a25), m(a26), m(a27),  \
      m(a28), m(a29), m(a30), m(a31)

#define ABSL_INTERNAL_NUM_ARGS_IMPL(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9,    \
                                    a10, a11, a12, a13, a14, a15, a16, a17,    \
                                    a18, a19, a20, a21, a22, a23, a24, a25,    \
                                    a26, a27, a28, a29, a30, a31, result, ...) \
  result

#define ABSL_INTERNAL_FORCE_EXPANSION(...) __VA_ARGS__

#define ABSL_INTERNAL_NUM_ARGS(...)                                            \
  ABSL_INTERNAL_FORCE_EXPANSION(ABSL_INTERNAL_NUM_ARGS_IMPL(                   \
      __VA_ARGS__, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, \
      17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, ))

#endif  // ABSL_TYPES_INTERNAL_TRANSFORM_ARGS_H_
