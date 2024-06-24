// Protocol Buffers - Google's data interchange format
// Copyright 2023 Google LLC.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef UPB_MINI_TABLE_INTERNAL_EXTENSION_H_
#define UPB_MINI_TABLE_INTERNAL_EXTENSION_H_

#include <stdint.h>

#include "upb/mini_table/internal/field.h"
#include "upb/mini_table/internal/sub.h"

// Must be last.
#include "upb/port/def.inc"

struct upb_MiniTableExtension {
  // Do not move this field. We need to be able to alias pointers.
  struct upb_MiniTableField UPB_PRIVATE(field);

  const struct upb_MiniTable* UPB_PRIVATE(extendee);
  union upb_MiniTableSub UPB_PRIVATE(sub);  // NULL unless submsg or proto2 enum
};

#ifdef __cplusplus
extern "C" {
#endif

UPB_INLINE upb_CType UPB_PRIVATE(_upb_MiniTableExtension_CType)(
    const struct upb_MiniTableExtension* e) {
  return UPB_PRIVATE(_upb_MiniTableField_CType)(&e->UPB_PRIVATE(field));
}

UPB_INLINE uint32_t UPB_PRIVATE(_upb_MiniTableExtension_Number)(
    const struct upb_MiniTableExtension* e) {
  return e->UPB_PRIVATE(field).UPB_ONLYBITS(number);
}

UPB_INLINE const struct upb_MiniTable* UPB_PRIVATE(
    _upb_MiniTableExtension_GetSubMessage)(
    const struct upb_MiniTableExtension* e) {
  return UPB_PRIVATE(_upb_MiniTableSub_Message)(e->UPB_PRIVATE(sub));
}

UPB_INLINE void UPB_PRIVATE(_upb_MiniTableExtension_SetSubMessage)(
    struct upb_MiniTableExtension* e, const struct upb_MiniTable* m) {
  e->UPB_PRIVATE(sub).UPB_PRIVATE(submsg) = m;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif /* UPB_MINI_TABLE_INTERNAL_EXTENSION_H_ */
