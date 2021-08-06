/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/type/v3/range.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_TYPE_V3_RANGE_PROTO_UPB_H_
#define ENVOY_TYPE_V3_RANGE_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_type_v3_Int64Range;
struct envoy_type_v3_Int32Range;
struct envoy_type_v3_DoubleRange;
typedef struct envoy_type_v3_Int64Range envoy_type_v3_Int64Range;
typedef struct envoy_type_v3_Int32Range envoy_type_v3_Int32Range;
typedef struct envoy_type_v3_DoubleRange envoy_type_v3_DoubleRange;
extern const upb_msglayout envoy_type_v3_Int64Range_msginit;
extern const upb_msglayout envoy_type_v3_Int32Range_msginit;
extern const upb_msglayout envoy_type_v3_DoubleRange_msginit;


/* envoy.type.v3.Int64Range */

UPB_INLINE envoy_type_v3_Int64Range *envoy_type_v3_Int64Range_new(upb_arena *arena) {
  return (envoy_type_v3_Int64Range *)_upb_msg_new(&envoy_type_v3_Int64Range_msginit, arena);
}
UPB_INLINE envoy_type_v3_Int64Range *envoy_type_v3_Int64Range_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_type_v3_Int64Range *ret = envoy_type_v3_Int64Range_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_type_v3_Int64Range_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_type_v3_Int64Range *envoy_type_v3_Int64Range_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_type_v3_Int64Range *ret = envoy_type_v3_Int64Range_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_type_v3_Int64Range_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_type_v3_Int64Range_serialize(const envoy_type_v3_Int64Range *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_type_v3_Int64Range_msginit, arena, len);
}

UPB_INLINE int64_t envoy_type_v3_Int64Range_start(const envoy_type_v3_Int64Range *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), int64_t); }
UPB_INLINE int64_t envoy_type_v3_Int64Range_end(const envoy_type_v3_Int64Range *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(8, 8), int64_t); }

UPB_INLINE void envoy_type_v3_Int64Range_set_start(envoy_type_v3_Int64Range *msg, int64_t value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), int64_t) = value;
}
UPB_INLINE void envoy_type_v3_Int64Range_set_end(envoy_type_v3_Int64Range *msg, int64_t value) {
  *UPB_PTR_AT(msg, UPB_SIZE(8, 8), int64_t) = value;
}

/* envoy.type.v3.Int32Range */

UPB_INLINE envoy_type_v3_Int32Range *envoy_type_v3_Int32Range_new(upb_arena *arena) {
  return (envoy_type_v3_Int32Range *)_upb_msg_new(&envoy_type_v3_Int32Range_msginit, arena);
}
UPB_INLINE envoy_type_v3_Int32Range *envoy_type_v3_Int32Range_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_type_v3_Int32Range *ret = envoy_type_v3_Int32Range_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_type_v3_Int32Range_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_type_v3_Int32Range *envoy_type_v3_Int32Range_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_type_v3_Int32Range *ret = envoy_type_v3_Int32Range_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_type_v3_Int32Range_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_type_v3_Int32Range_serialize(const envoy_type_v3_Int32Range *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_type_v3_Int32Range_msginit, arena, len);
}

UPB_INLINE int32_t envoy_type_v3_Int32Range_start(const envoy_type_v3_Int32Range *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), int32_t); }
UPB_INLINE int32_t envoy_type_v3_Int32Range_end(const envoy_type_v3_Int32Range *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(4, 4), int32_t); }

UPB_INLINE void envoy_type_v3_Int32Range_set_start(envoy_type_v3_Int32Range *msg, int32_t value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), int32_t) = value;
}
UPB_INLINE void envoy_type_v3_Int32Range_set_end(envoy_type_v3_Int32Range *msg, int32_t value) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 4), int32_t) = value;
}

/* envoy.type.v3.DoubleRange */

UPB_INLINE envoy_type_v3_DoubleRange *envoy_type_v3_DoubleRange_new(upb_arena *arena) {
  return (envoy_type_v3_DoubleRange *)_upb_msg_new(&envoy_type_v3_DoubleRange_msginit, arena);
}
UPB_INLINE envoy_type_v3_DoubleRange *envoy_type_v3_DoubleRange_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_type_v3_DoubleRange *ret = envoy_type_v3_DoubleRange_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_type_v3_DoubleRange_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_type_v3_DoubleRange *envoy_type_v3_DoubleRange_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_type_v3_DoubleRange *ret = envoy_type_v3_DoubleRange_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_type_v3_DoubleRange_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_type_v3_DoubleRange_serialize(const envoy_type_v3_DoubleRange *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_type_v3_DoubleRange_msginit, arena, len);
}

UPB_INLINE double envoy_type_v3_DoubleRange_start(const envoy_type_v3_DoubleRange *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), double); }
UPB_INLINE double envoy_type_v3_DoubleRange_end(const envoy_type_v3_DoubleRange *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(8, 8), double); }

UPB_INLINE void envoy_type_v3_DoubleRange_set_start(envoy_type_v3_DoubleRange *msg, double value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), double) = value;
}
UPB_INLINE void envoy_type_v3_DoubleRange_set_end(envoy_type_v3_DoubleRange *msg, double value) {
  *UPB_PTR_AT(msg, UPB_SIZE(8, 8), double) = value;
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_TYPE_V3_RANGE_PROTO_UPB_H_ */
