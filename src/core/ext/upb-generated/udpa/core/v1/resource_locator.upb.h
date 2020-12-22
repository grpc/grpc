/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     udpa/core/v1/resource_locator.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef UDPA_CORE_V1_RESOURCE_LOCATOR_PROTO_UPB_H_
#define UDPA_CORE_V1_RESOURCE_LOCATOR_PROTO_UPB_H_

#include "upb/msg.h"
#include "upb/decode.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct udpa_core_v1_ResourceLocator;
struct udpa_core_v1_ResourceLocator_Directive;
typedef struct udpa_core_v1_ResourceLocator udpa_core_v1_ResourceLocator;
typedef struct udpa_core_v1_ResourceLocator_Directive udpa_core_v1_ResourceLocator_Directive;
extern const upb_msglayout udpa_core_v1_ResourceLocator_msginit;
extern const upb_msglayout udpa_core_v1_ResourceLocator_Directive_msginit;
struct udpa_core_v1_ContextParams;
extern const upb_msglayout udpa_core_v1_ContextParams_msginit;

typedef enum {
  udpa_core_v1_ResourceLocator_UDPA = 0,
  udpa_core_v1_ResourceLocator_HTTP = 1,
  udpa_core_v1_ResourceLocator_FILE = 2
} udpa_core_v1_ResourceLocator_Scheme;


/* udpa.core.v1.ResourceLocator */

UPB_INLINE udpa_core_v1_ResourceLocator *udpa_core_v1_ResourceLocator_new(upb_arena *arena) {
  return (udpa_core_v1_ResourceLocator *)_upb_msg_new(&udpa_core_v1_ResourceLocator_msginit, arena);
}
UPB_INLINE udpa_core_v1_ResourceLocator *udpa_core_v1_ResourceLocator_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  udpa_core_v1_ResourceLocator *ret = udpa_core_v1_ResourceLocator_new(arena);
  return (ret && upb_decode(buf, size, ret, &udpa_core_v1_ResourceLocator_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *udpa_core_v1_ResourceLocator_serialize(const udpa_core_v1_ResourceLocator *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &udpa_core_v1_ResourceLocator_msginit, arena, len);
}

typedef enum {
  udpa_core_v1_ResourceLocator_context_param_specifier_exact_context = 5,
  udpa_core_v1_ResourceLocator_context_param_specifier_NOT_SET = 0
} udpa_core_v1_ResourceLocator_context_param_specifier_oneofcases;
UPB_INLINE udpa_core_v1_ResourceLocator_context_param_specifier_oneofcases udpa_core_v1_ResourceLocator_context_param_specifier_case(const udpa_core_v1_ResourceLocator* msg) { return (udpa_core_v1_ResourceLocator_context_param_specifier_oneofcases)*UPB_PTR_AT(msg, UPB_SIZE(36, 64), int32_t); }

UPB_INLINE int32_t udpa_core_v1_ResourceLocator_scheme(const udpa_core_v1_ResourceLocator *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), int32_t); }
UPB_INLINE upb_strview const* udpa_core_v1_ResourceLocator_id(const udpa_core_v1_ResourceLocator *msg, size_t *len) { return (upb_strview const*)_upb_array_accessor(msg, UPB_SIZE(24, 40), len); }
UPB_INLINE upb_strview udpa_core_v1_ResourceLocator_authority(const udpa_core_v1_ResourceLocator *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(8, 8), upb_strview); }
UPB_INLINE upb_strview udpa_core_v1_ResourceLocator_resource_type(const udpa_core_v1_ResourceLocator *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(16, 24), upb_strview); }
UPB_INLINE bool udpa_core_v1_ResourceLocator_has_exact_context(const udpa_core_v1_ResourceLocator *msg) { return _upb_getoneofcase(msg, UPB_SIZE(36, 64)) == 5; }
UPB_INLINE const struct udpa_core_v1_ContextParams* udpa_core_v1_ResourceLocator_exact_context(const udpa_core_v1_ResourceLocator *msg) { return UPB_READ_ONEOF(msg, const struct udpa_core_v1_ContextParams*, UPB_SIZE(32, 56), UPB_SIZE(36, 64), 5, NULL); }
UPB_INLINE bool udpa_core_v1_ResourceLocator_has_directives(const udpa_core_v1_ResourceLocator *msg) { return _upb_has_submsg_nohasbit(msg, UPB_SIZE(28, 48)); }
UPB_INLINE const udpa_core_v1_ResourceLocator_Directive* const* udpa_core_v1_ResourceLocator_directives(const udpa_core_v1_ResourceLocator *msg, size_t *len) { return (const udpa_core_v1_ResourceLocator_Directive* const*)_upb_array_accessor(msg, UPB_SIZE(28, 48), len); }

UPB_INLINE void udpa_core_v1_ResourceLocator_set_scheme(udpa_core_v1_ResourceLocator *msg, int32_t value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), int32_t) = value;
}
UPB_INLINE upb_strview* udpa_core_v1_ResourceLocator_mutable_id(udpa_core_v1_ResourceLocator *msg, size_t *len) {
  return (upb_strview*)_upb_array_mutable_accessor(msg, UPB_SIZE(24, 40), len);
}
UPB_INLINE upb_strview* udpa_core_v1_ResourceLocator_resize_id(udpa_core_v1_ResourceLocator *msg, size_t len, upb_arena *arena) {
  return (upb_strview*)_upb_array_resize_accessor(msg, UPB_SIZE(24, 40), len, UPB_TYPE_STRING, arena);
}
UPB_INLINE bool udpa_core_v1_ResourceLocator_add_id(udpa_core_v1_ResourceLocator *msg, upb_strview val, upb_arena *arena) {
  return _upb_array_append_accessor(msg, UPB_SIZE(24, 40), UPB_SIZE(8, 16), UPB_TYPE_STRING, &val,
      arena);
}
UPB_INLINE void udpa_core_v1_ResourceLocator_set_authority(udpa_core_v1_ResourceLocator *msg, upb_strview value) {
  *UPB_PTR_AT(msg, UPB_SIZE(8, 8), upb_strview) = value;
}
UPB_INLINE void udpa_core_v1_ResourceLocator_set_resource_type(udpa_core_v1_ResourceLocator *msg, upb_strview value) {
  *UPB_PTR_AT(msg, UPB_SIZE(16, 24), upb_strview) = value;
}
UPB_INLINE void udpa_core_v1_ResourceLocator_set_exact_context(udpa_core_v1_ResourceLocator *msg, struct udpa_core_v1_ContextParams* value) {
  UPB_WRITE_ONEOF(msg, struct udpa_core_v1_ContextParams*, UPB_SIZE(32, 56), value, UPB_SIZE(36, 64), 5);
}
UPB_INLINE struct udpa_core_v1_ContextParams* udpa_core_v1_ResourceLocator_mutable_exact_context(udpa_core_v1_ResourceLocator *msg, upb_arena *arena) {
  struct udpa_core_v1_ContextParams* sub = (struct udpa_core_v1_ContextParams*)udpa_core_v1_ResourceLocator_exact_context(msg);
  if (sub == NULL) {
    sub = (struct udpa_core_v1_ContextParams*)_upb_msg_new(&udpa_core_v1_ContextParams_msginit, arena);
    if (!sub) return NULL;
    udpa_core_v1_ResourceLocator_set_exact_context(msg, sub);
  }
  return sub;
}
UPB_INLINE udpa_core_v1_ResourceLocator_Directive** udpa_core_v1_ResourceLocator_mutable_directives(udpa_core_v1_ResourceLocator *msg, size_t *len) {
  return (udpa_core_v1_ResourceLocator_Directive**)_upb_array_mutable_accessor(msg, UPB_SIZE(28, 48), len);
}
UPB_INLINE udpa_core_v1_ResourceLocator_Directive** udpa_core_v1_ResourceLocator_resize_directives(udpa_core_v1_ResourceLocator *msg, size_t len, upb_arena *arena) {
  return (udpa_core_v1_ResourceLocator_Directive**)_upb_array_resize_accessor(msg, UPB_SIZE(28, 48), len, UPB_TYPE_MESSAGE, arena);
}
UPB_INLINE struct udpa_core_v1_ResourceLocator_Directive* udpa_core_v1_ResourceLocator_add_directives(udpa_core_v1_ResourceLocator *msg, upb_arena *arena) {
  struct udpa_core_v1_ResourceLocator_Directive* sub = (struct udpa_core_v1_ResourceLocator_Directive*)_upb_msg_new(&udpa_core_v1_ResourceLocator_Directive_msginit, arena);
  bool ok = _upb_array_append_accessor(
      msg, UPB_SIZE(28, 48), UPB_SIZE(4, 8), UPB_TYPE_MESSAGE, &sub, arena);
  if (!ok) return NULL;
  return sub;
}

/* udpa.core.v1.ResourceLocator.Directive */

UPB_INLINE udpa_core_v1_ResourceLocator_Directive *udpa_core_v1_ResourceLocator_Directive_new(upb_arena *arena) {
  return (udpa_core_v1_ResourceLocator_Directive *)_upb_msg_new(&udpa_core_v1_ResourceLocator_Directive_msginit, arena);
}
UPB_INLINE udpa_core_v1_ResourceLocator_Directive *udpa_core_v1_ResourceLocator_Directive_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  udpa_core_v1_ResourceLocator_Directive *ret = udpa_core_v1_ResourceLocator_Directive_new(arena);
  return (ret && upb_decode(buf, size, ret, &udpa_core_v1_ResourceLocator_Directive_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *udpa_core_v1_ResourceLocator_Directive_serialize(const udpa_core_v1_ResourceLocator_Directive *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &udpa_core_v1_ResourceLocator_Directive_msginit, arena, len);
}

typedef enum {
  udpa_core_v1_ResourceLocator_Directive_directive_alt = 1,
  udpa_core_v1_ResourceLocator_Directive_directive_entry = 2,
  udpa_core_v1_ResourceLocator_Directive_directive_NOT_SET = 0
} udpa_core_v1_ResourceLocator_Directive_directive_oneofcases;
UPB_INLINE udpa_core_v1_ResourceLocator_Directive_directive_oneofcases udpa_core_v1_ResourceLocator_Directive_directive_case(const udpa_core_v1_ResourceLocator_Directive* msg) { return (udpa_core_v1_ResourceLocator_Directive_directive_oneofcases)*UPB_PTR_AT(msg, UPB_SIZE(8, 16), int32_t); }

UPB_INLINE bool udpa_core_v1_ResourceLocator_Directive_has_alt(const udpa_core_v1_ResourceLocator_Directive *msg) { return _upb_getoneofcase(msg, UPB_SIZE(8, 16)) == 1; }
UPB_INLINE const udpa_core_v1_ResourceLocator* udpa_core_v1_ResourceLocator_Directive_alt(const udpa_core_v1_ResourceLocator_Directive *msg) { return UPB_READ_ONEOF(msg, const udpa_core_v1_ResourceLocator*, UPB_SIZE(0, 0), UPB_SIZE(8, 16), 1, NULL); }
UPB_INLINE bool udpa_core_v1_ResourceLocator_Directive_has_entry(const udpa_core_v1_ResourceLocator_Directive *msg) { return _upb_getoneofcase(msg, UPB_SIZE(8, 16)) == 2; }
UPB_INLINE upb_strview udpa_core_v1_ResourceLocator_Directive_entry(const udpa_core_v1_ResourceLocator_Directive *msg) { return UPB_READ_ONEOF(msg, upb_strview, UPB_SIZE(0, 0), UPB_SIZE(8, 16), 2, upb_strview_make("", strlen(""))); }

UPB_INLINE void udpa_core_v1_ResourceLocator_Directive_set_alt(udpa_core_v1_ResourceLocator_Directive *msg, udpa_core_v1_ResourceLocator* value) {
  UPB_WRITE_ONEOF(msg, udpa_core_v1_ResourceLocator*, UPB_SIZE(0, 0), value, UPB_SIZE(8, 16), 1);
}
UPB_INLINE struct udpa_core_v1_ResourceLocator* udpa_core_v1_ResourceLocator_Directive_mutable_alt(udpa_core_v1_ResourceLocator_Directive *msg, upb_arena *arena) {
  struct udpa_core_v1_ResourceLocator* sub = (struct udpa_core_v1_ResourceLocator*)udpa_core_v1_ResourceLocator_Directive_alt(msg);
  if (sub == NULL) {
    sub = (struct udpa_core_v1_ResourceLocator*)_upb_msg_new(&udpa_core_v1_ResourceLocator_msginit, arena);
    if (!sub) return NULL;
    udpa_core_v1_ResourceLocator_Directive_set_alt(msg, sub);
  }
  return sub;
}
UPB_INLINE void udpa_core_v1_ResourceLocator_Directive_set_entry(udpa_core_v1_ResourceLocator_Directive *msg, upb_strview value) {
  UPB_WRITE_ONEOF(msg, upb_strview, UPB_SIZE(0, 0), value, UPB_SIZE(8, 16), 2);
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* UDPA_CORE_V1_RESOURCE_LOCATOR_PROTO_UPB_H_ */
