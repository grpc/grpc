/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     xds/service/orca/v3/orca.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef XDS_SERVICE_ORCA_V3_ORCA_PROTO_UPB_H_
#define XDS_SERVICE_ORCA_V3_ORCA_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct xds_service_orca_v3_OrcaLoadReportRequest;
typedef struct xds_service_orca_v3_OrcaLoadReportRequest xds_service_orca_v3_OrcaLoadReportRequest;
extern const upb_MiniTable xds_service_orca_v3_OrcaLoadReportRequest_msginit;
struct google_protobuf_Duration;
extern const upb_MiniTable google_protobuf_Duration_msginit;



/* xds.service.orca.v3.OrcaLoadReportRequest */

UPB_INLINE xds_service_orca_v3_OrcaLoadReportRequest* xds_service_orca_v3_OrcaLoadReportRequest_new(upb_Arena* arena) {
  return (xds_service_orca_v3_OrcaLoadReportRequest*)_upb_Message_New(&xds_service_orca_v3_OrcaLoadReportRequest_msginit, arena);
}
UPB_INLINE xds_service_orca_v3_OrcaLoadReportRequest* xds_service_orca_v3_OrcaLoadReportRequest_parse(const char* buf, size_t size, upb_Arena* arena) {
  xds_service_orca_v3_OrcaLoadReportRequest* ret = xds_service_orca_v3_OrcaLoadReportRequest_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_service_orca_v3_OrcaLoadReportRequest_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE xds_service_orca_v3_OrcaLoadReportRequest* xds_service_orca_v3_OrcaLoadReportRequest_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  xds_service_orca_v3_OrcaLoadReportRequest* ret = xds_service_orca_v3_OrcaLoadReportRequest_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_service_orca_v3_OrcaLoadReportRequest_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* xds_service_orca_v3_OrcaLoadReportRequest_serialize(const xds_service_orca_v3_OrcaLoadReportRequest* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_service_orca_v3_OrcaLoadReportRequest_msginit, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* xds_service_orca_v3_OrcaLoadReportRequest_serialize_ex(const xds_service_orca_v3_OrcaLoadReportRequest* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_service_orca_v3_OrcaLoadReportRequest_msginit, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE bool xds_service_orca_v3_OrcaLoadReportRequest_has_report_interval(const xds_service_orca_v3_OrcaLoadReportRequest* msg) {
  return _upb_hasbit(msg, 1);
}
UPB_INLINE void xds_service_orca_v3_OrcaLoadReportRequest_clear_report_interval(const xds_service_orca_v3_OrcaLoadReportRequest* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), const upb_Message*) = NULL;
}
UPB_INLINE const struct google_protobuf_Duration* xds_service_orca_v3_OrcaLoadReportRequest_report_interval(const xds_service_orca_v3_OrcaLoadReportRequest* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), const struct google_protobuf_Duration*);
}
UPB_INLINE void xds_service_orca_v3_OrcaLoadReportRequest_clear_request_cost_names(const xds_service_orca_v3_OrcaLoadReportRequest* msg) {
  _upb_array_detach(msg, UPB_SIZE(8, 16));
}
UPB_INLINE upb_StringView const* xds_service_orca_v3_OrcaLoadReportRequest_request_cost_names(const xds_service_orca_v3_OrcaLoadReportRequest* msg, size_t* len) {
  return (upb_StringView const*)_upb_array_accessor(msg, UPB_SIZE(8, 16), len);
}

UPB_INLINE void xds_service_orca_v3_OrcaLoadReportRequest_set_report_interval(xds_service_orca_v3_OrcaLoadReportRequest *msg, struct google_protobuf_Duration* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), struct google_protobuf_Duration*) = value;
}
UPB_INLINE struct google_protobuf_Duration* xds_service_orca_v3_OrcaLoadReportRequest_mutable_report_interval(xds_service_orca_v3_OrcaLoadReportRequest* msg, upb_Arena* arena) {
  struct google_protobuf_Duration* sub = (struct google_protobuf_Duration*)xds_service_orca_v3_OrcaLoadReportRequest_report_interval(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Duration*)_upb_Message_New(&google_protobuf_Duration_msginit, arena);
    if (!sub) return NULL;
    xds_service_orca_v3_OrcaLoadReportRequest_set_report_interval(msg, sub);
  }
  return sub;
}
UPB_INLINE upb_StringView* xds_service_orca_v3_OrcaLoadReportRequest_mutable_request_cost_names(xds_service_orca_v3_OrcaLoadReportRequest* msg, size_t* len) {
  return (upb_StringView*)_upb_array_mutable_accessor(msg, UPB_SIZE(8, 16), len);
}
UPB_INLINE upb_StringView* xds_service_orca_v3_OrcaLoadReportRequest_resize_request_cost_names(xds_service_orca_v3_OrcaLoadReportRequest* msg, size_t len, upb_Arena* arena) {
  return (upb_StringView*)_upb_Array_Resize_accessor2(msg, UPB_SIZE(8, 16), len, UPB_SIZE(3, 4), arena);
}
UPB_INLINE bool xds_service_orca_v3_OrcaLoadReportRequest_add_request_cost_names(xds_service_orca_v3_OrcaLoadReportRequest* msg, upb_StringView val, upb_Arena* arena) {
  return _upb_Array_Append_accessor2(msg, UPB_SIZE(8, 16), UPB_SIZE(3, 4), &val, arena);
}

extern const upb_MiniTable_File xds_service_orca_v3_orca_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* XDS_SERVICE_ORCA_V3_ORCA_PROTO_UPB_H_ */
