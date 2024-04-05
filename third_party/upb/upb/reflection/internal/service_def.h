// Protocol Buffers - Google's data interchange format
// Copyright 2023 Google LLC.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef UPB_REFLECTION_SERVICE_DEF_INTERNAL_H_
#define UPB_REFLECTION_SERVICE_DEF_INTERNAL_H_

#include "upb/reflection/service_def.h"

// Must be last.
#include "upb/port/def.inc"

#ifdef __cplusplus
extern "C" {
#endif

upb_ServiceDef* _upb_ServiceDef_At(const upb_ServiceDef* s, int i);

// Allocate and initialize an array of |n| service defs.
upb_ServiceDef* _upb_ServiceDefs_New(
    upb_DefBuilder* ctx, int n,
    const UPB_DESC(ServiceDescriptorProto) * const* protos);

#ifdef __cplusplus
} /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif /* UPB_REFLECTION_SERVICE_DEF_INTERNAL_H_ */
