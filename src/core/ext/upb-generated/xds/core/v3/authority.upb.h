/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     xds/core/v3/authority.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef XDS_CORE_V3_AUTHORITY_PROTO_UPB_H_
#define XDS_CORE_V3_AUTHORITY_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct xds_core_v3_Authority;
typedef struct xds_core_v3_Authority xds_core_v3_Authority;
extern const upb_msglayout xds_core_v3_Authority_msginit;


/* xds.core.v3.Authority */

UPB_INLINE xds_core_v3_Authority *xds_core_v3_Authority_new(upb_arena *arena) {
  return (xds_core_v3_Authority *)_upb_msg_new(&xds_core_v3_Authority_msginit, arena);
}
UPB_INLINE xds_core_v3_Authority *xds_core_v3_Authority_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  xds_core_v3_Authority *ret = xds_core_v3_Authority_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &xds_core_v3_Authority_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE xds_core_v3_Authority *xds_core_v3_Authority_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  xds_core_v3_Authority *ret = xds_core_v3_Authority_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &xds_core_v3_Authority_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *xds_core_v3_Authority_serialize(const xds_core_v3_Authority *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &xds_core_v3_Authority_msginit, arena, len);
}

UPB_INLINE upb_strview xds_core_v3_Authority_name(const xds_core_v3_Authority *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_strview); }

UPB_INLINE void xds_core_v3_Authority_set_name(xds_core_v3_Authority *msg, upb_strview value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_strview) = value;
}

extern const upb_msglayout_file xds_core_v3_authority_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* XDS_CORE_V3_AUTHORITY_PROTO_UPB_H_ */
