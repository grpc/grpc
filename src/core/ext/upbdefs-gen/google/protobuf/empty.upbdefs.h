/* This file was generated by upb_generator from the input file:
 *
 *     google/protobuf/empty.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated.
 * NO CHECKED-IN PROTOBUF GENCODE */

#ifndef GOOGLE_PROTOBUF_EMPTY_PROTO_UPB_H__UPBDEFS_H_
#define GOOGLE_PROTOBUF_EMPTY_PROTO_UPB_H__UPBDEFS_H_

#include "upb/reflection/def.h"
#include "upb/reflection/internal/def_pool.h"

#include "upb/port/def.inc" // Must be last.
#ifdef __cplusplus
extern "C" {
#endif

extern _upb_DefPool_Init google_protobuf_empty_proto_upbdefinit;

UPB_INLINE const upb_MessageDef *google_protobuf_Empty_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &google_protobuf_empty_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "google.protobuf.Empty");
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* GOOGLE_PROTOBUF_EMPTY_PROTO_UPB_H__UPBDEFS_H_ */
