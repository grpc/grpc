/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     udpa/type/v1/typed_struct.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef UDPA_TYPE_V1_TYPED_STRUCT_PROTO_UPBDEFS_H_
#define UDPA_TYPE_V1_TYPED_STRUCT_PROTO_UPBDEFS_H_

#include "upb/def.h"
#include "upb/port_def.inc"
#ifdef __cplusplus
extern "C" {
#endif

#include "upb/def.h"

#include "upb/port_def.inc"

extern upb_def_init udpa_type_v1_typed_struct_proto_upbdefinit;

UPB_INLINE const upb_msgdef *udpa_type_v1_TypedStruct_getmsgdef(upb_symtab *s) {
  _upb_symtab_loaddefinit(s, &udpa_type_v1_typed_struct_proto_upbdefinit);
  return upb_symtab_lookupmsg(s, "udpa.type.v1.TypedStruct");
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* UDPA_TYPE_V1_TYPED_STRUCT_PROTO_UPBDEFS_H_ */
