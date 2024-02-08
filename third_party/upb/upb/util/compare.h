// Protocol Buffers - Google's data interchange format
// Copyright 2023 Google LLC.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef UPB_UTIL_COMPARE_H_
#define UPB_UTIL_COMPARE_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns true if unknown fields from the two messages are equal when sorted
// and varints are made canonical.
//
// This function is discouraged, as the comparison is inherently lossy without
// schema data:
//
//  1. We don't know whether delimited fields are sub-messages. Unknown
//     sub-messages will therefore not have their fields sorted and varints
//     canonicalized.
//  2. We don't know about oneof/non-repeated fields, which should semantically
//     discard every value except the last.

typedef enum {
  kUpb_UnknownCompareResult_Equal = 0,
  kUpb_UnknownCompareResult_NotEqual = 1,
  kUpb_UnknownCompareResult_OutOfMemory = 2,
  kUpb_UnknownCompareResult_MaxDepthExceeded = 3,
} upb_UnknownCompareResult;

upb_UnknownCompareResult upb_Message_UnknownFieldsAreEqual(const char* buf1,
                                                           size_t size1,
                                                           const char* buf2,
                                                           size_t size2,
                                                           int max_depth);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* UPB_UTIL_COMPARE_H_ */
