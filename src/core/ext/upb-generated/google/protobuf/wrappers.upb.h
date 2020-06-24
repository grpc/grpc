/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     google/protobuf/wrappers.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef GOOGLE_PROTOBUF_WRAPPERS_PROTO_UPB_H_
#define GOOGLE_PROTOBUF_WRAPPERS_PROTO_UPB_H_

#include "upb/msg.h"
#include "upb/decode.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct google_protobuf_DoubleValue;
struct google_protobuf_FloatValue;
struct google_protobuf_Int64Value;
struct google_protobuf_UInt64Value;
struct google_protobuf_Int32Value;
struct google_protobuf_UInt32Value;
struct google_protobuf_BoolValue;
struct google_protobuf_StringValue;
struct google_protobuf_BytesValue;
typedef struct google_protobuf_DoubleValue google_protobuf_DoubleValue;
typedef struct google_protobuf_FloatValue google_protobuf_FloatValue;
typedef struct google_protobuf_Int64Value google_protobuf_Int64Value;
typedef struct google_protobuf_UInt64Value google_protobuf_UInt64Value;
typedef struct google_protobuf_Int32Value google_protobuf_Int32Value;
typedef struct google_protobuf_UInt32Value google_protobuf_UInt32Value;
typedef struct google_protobuf_BoolValue google_protobuf_BoolValue;
typedef struct google_protobuf_StringValue google_protobuf_StringValue;
typedef struct google_protobuf_BytesValue google_protobuf_BytesValue;
extern const upb_msglayout google_protobuf_DoubleValue_msginit;
extern const upb_msglayout google_protobuf_FloatValue_msginit;
extern const upb_msglayout google_protobuf_Int64Value_msginit;
extern const upb_msglayout google_protobuf_UInt64Value_msginit;
extern const upb_msglayout google_protobuf_Int32Value_msginit;
extern const upb_msglayout google_protobuf_UInt32Value_msginit;
extern const upb_msglayout google_protobuf_BoolValue_msginit;
extern const upb_msglayout google_protobuf_StringValue_msginit;
extern const upb_msglayout google_protobuf_BytesValue_msginit;


/* google.protobuf.DoubleValue */

UPB_INLINE google_protobuf_DoubleValue *google_protobuf_DoubleValue_new(upb_arena *arena) {
  return (google_protobuf_DoubleValue *)_upb_msg_new(&google_protobuf_DoubleValue_msginit, arena);
}
UPB_INLINE google_protobuf_DoubleValue *google_protobuf_DoubleValue_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  google_protobuf_DoubleValue *ret = google_protobuf_DoubleValue_new(arena);
  return (ret && upb_decode(buf, size, ret, &google_protobuf_DoubleValue_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *google_protobuf_DoubleValue_serialize(const google_protobuf_DoubleValue *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &google_protobuf_DoubleValue_msginit, arena, len);
}

UPB_INLINE double google_protobuf_DoubleValue_value(const google_protobuf_DoubleValue *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), double); }

UPB_INLINE void google_protobuf_DoubleValue_set_value(google_protobuf_DoubleValue *msg, double value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), double) = value;
}

/* google.protobuf.FloatValue */

UPB_INLINE google_protobuf_FloatValue *google_protobuf_FloatValue_new(upb_arena *arena) {
  return (google_protobuf_FloatValue *)_upb_msg_new(&google_protobuf_FloatValue_msginit, arena);
}
UPB_INLINE google_protobuf_FloatValue *google_protobuf_FloatValue_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  google_protobuf_FloatValue *ret = google_protobuf_FloatValue_new(arena);
  return (ret && upb_decode(buf, size, ret, &google_protobuf_FloatValue_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *google_protobuf_FloatValue_serialize(const google_protobuf_FloatValue *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &google_protobuf_FloatValue_msginit, arena, len);
}

UPB_INLINE float google_protobuf_FloatValue_value(const google_protobuf_FloatValue *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), float); }

UPB_INLINE void google_protobuf_FloatValue_set_value(google_protobuf_FloatValue *msg, float value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), float) = value;
}

/* google.protobuf.Int64Value */

UPB_INLINE google_protobuf_Int64Value *google_protobuf_Int64Value_new(upb_arena *arena) {
  return (google_protobuf_Int64Value *)_upb_msg_new(&google_protobuf_Int64Value_msginit, arena);
}
UPB_INLINE google_protobuf_Int64Value *google_protobuf_Int64Value_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  google_protobuf_Int64Value *ret = google_protobuf_Int64Value_new(arena);
  return (ret && upb_decode(buf, size, ret, &google_protobuf_Int64Value_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *google_protobuf_Int64Value_serialize(const google_protobuf_Int64Value *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &google_protobuf_Int64Value_msginit, arena, len);
}

UPB_INLINE int64_t google_protobuf_Int64Value_value(const google_protobuf_Int64Value *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), int64_t); }

UPB_INLINE void google_protobuf_Int64Value_set_value(google_protobuf_Int64Value *msg, int64_t value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), int64_t) = value;
}

/* google.protobuf.UInt64Value */

UPB_INLINE google_protobuf_UInt64Value *google_protobuf_UInt64Value_new(upb_arena *arena) {
  return (google_protobuf_UInt64Value *)_upb_msg_new(&google_protobuf_UInt64Value_msginit, arena);
}
UPB_INLINE google_protobuf_UInt64Value *google_protobuf_UInt64Value_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  google_protobuf_UInt64Value *ret = google_protobuf_UInt64Value_new(arena);
  return (ret && upb_decode(buf, size, ret, &google_protobuf_UInt64Value_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *google_protobuf_UInt64Value_serialize(const google_protobuf_UInt64Value *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &google_protobuf_UInt64Value_msginit, arena, len);
}

UPB_INLINE uint64_t google_protobuf_UInt64Value_value(const google_protobuf_UInt64Value *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), uint64_t); }

UPB_INLINE void google_protobuf_UInt64Value_set_value(google_protobuf_UInt64Value *msg, uint64_t value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), uint64_t) = value;
}

/* google.protobuf.Int32Value */

UPB_INLINE google_protobuf_Int32Value *google_protobuf_Int32Value_new(upb_arena *arena) {
  return (google_protobuf_Int32Value *)_upb_msg_new(&google_protobuf_Int32Value_msginit, arena);
}
UPB_INLINE google_protobuf_Int32Value *google_protobuf_Int32Value_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  google_protobuf_Int32Value *ret = google_protobuf_Int32Value_new(arena);
  return (ret && upb_decode(buf, size, ret, &google_protobuf_Int32Value_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *google_protobuf_Int32Value_serialize(const google_protobuf_Int32Value *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &google_protobuf_Int32Value_msginit, arena, len);
}

UPB_INLINE int32_t google_protobuf_Int32Value_value(const google_protobuf_Int32Value *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), int32_t); }

UPB_INLINE void google_protobuf_Int32Value_set_value(google_protobuf_Int32Value *msg, int32_t value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), int32_t) = value;
}

/* google.protobuf.UInt32Value */

UPB_INLINE google_protobuf_UInt32Value *google_protobuf_UInt32Value_new(upb_arena *arena) {
  return (google_protobuf_UInt32Value *)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
}
UPB_INLINE google_protobuf_UInt32Value *google_protobuf_UInt32Value_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  google_protobuf_UInt32Value *ret = google_protobuf_UInt32Value_new(arena);
  return (ret && upb_decode(buf, size, ret, &google_protobuf_UInt32Value_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *google_protobuf_UInt32Value_serialize(const google_protobuf_UInt32Value *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &google_protobuf_UInt32Value_msginit, arena, len);
}

UPB_INLINE uint32_t google_protobuf_UInt32Value_value(const google_protobuf_UInt32Value *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), uint32_t); }

UPB_INLINE void google_protobuf_UInt32Value_set_value(google_protobuf_UInt32Value *msg, uint32_t value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), uint32_t) = value;
}

/* google.protobuf.BoolValue */

UPB_INLINE google_protobuf_BoolValue *google_protobuf_BoolValue_new(upb_arena *arena) {
  return (google_protobuf_BoolValue *)_upb_msg_new(&google_protobuf_BoolValue_msginit, arena);
}
UPB_INLINE google_protobuf_BoolValue *google_protobuf_BoolValue_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  google_protobuf_BoolValue *ret = google_protobuf_BoolValue_new(arena);
  return (ret && upb_decode(buf, size, ret, &google_protobuf_BoolValue_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *google_protobuf_BoolValue_serialize(const google_protobuf_BoolValue *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &google_protobuf_BoolValue_msginit, arena, len);
}

UPB_INLINE bool google_protobuf_BoolValue_value(const google_protobuf_BoolValue *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), bool); }

UPB_INLINE void google_protobuf_BoolValue_set_value(google_protobuf_BoolValue *msg, bool value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), bool) = value;
}

/* google.protobuf.StringValue */

UPB_INLINE google_protobuf_StringValue *google_protobuf_StringValue_new(upb_arena *arena) {
  return (google_protobuf_StringValue *)_upb_msg_new(&google_protobuf_StringValue_msginit, arena);
}
UPB_INLINE google_protobuf_StringValue *google_protobuf_StringValue_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  google_protobuf_StringValue *ret = google_protobuf_StringValue_new(arena);
  return (ret && upb_decode(buf, size, ret, &google_protobuf_StringValue_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *google_protobuf_StringValue_serialize(const google_protobuf_StringValue *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &google_protobuf_StringValue_msginit, arena, len);
}

UPB_INLINE upb_strview google_protobuf_StringValue_value(const google_protobuf_StringValue *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_strview); }

UPB_INLINE void google_protobuf_StringValue_set_value(google_protobuf_StringValue *msg, upb_strview value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_strview) = value;
}

/* google.protobuf.BytesValue */

UPB_INLINE google_protobuf_BytesValue *google_protobuf_BytesValue_new(upb_arena *arena) {
  return (google_protobuf_BytesValue *)_upb_msg_new(&google_protobuf_BytesValue_msginit, arena);
}
UPB_INLINE google_protobuf_BytesValue *google_protobuf_BytesValue_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  google_protobuf_BytesValue *ret = google_protobuf_BytesValue_new(arena);
  return (ret && upb_decode(buf, size, ret, &google_protobuf_BytesValue_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *google_protobuf_BytesValue_serialize(const google_protobuf_BytesValue *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &google_protobuf_BytesValue_msginit, arena, len);
}

UPB_INLINE upb_strview google_protobuf_BytesValue_value(const google_protobuf_BytesValue *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_strview); }

UPB_INLINE void google_protobuf_BytesValue_set_value(google_protobuf_BytesValue *msg, upb_strview value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_strview) = value;
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* GOOGLE_PROTOBUF_WRAPPERS_PROTO_UPB_H_ */
