/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.upb.h"
#include "envoy/config/accesslog/v3/accesslog.upb.h"
#include "envoy/config/core/v3/address.upb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/config/core/v3/config_source.upb.h"
#include "envoy/config/core/v3/extension.upb.h"
#include "envoy/config/core/v3/protocol.upb.h"
#include "envoy/config/core/v3/substitution_format_string.upb.h"
#include "envoy/config/route/v3/route.upb.h"
#include "envoy/config/route/v3/scoped_route.upb.h"
#include "envoy/config/trace/v3/http_tracer.upb.h"
#include "envoy/type/http/v3/path_transformation.upb.h"
#include "envoy/type/tracing/v3/custom_tag.upb.h"
#include "envoy/type/v3/percent.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "udpa/annotations/migrate.upb.h"
#include "udpa/annotations/security.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

#include "upb/port_def.inc"

static const upb_MiniTable_Sub envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_submsgs[32] = {
  {.submsg = &envoy_extensions_filters_network_http_connection_manager_v3_Rds_msginit},
  {.submsg = &envoy_config_route_v3_RouteConfiguration_msginit},
  {.submsg = &envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter_msginit},
  {.submsg = &google_protobuf_BoolValue_msginit},
  {.submsg = &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_Tracing_msginit},
  {.submsg = &envoy_config_core_v3_Http1ProtocolOptions_msginit},
  {.submsg = &envoy_config_core_v3_Http2ProtocolOptions_msginit},
  {.submsg = &google_protobuf_Duration_msginit},
  {.submsg = &envoy_config_accesslog_v3_AccessLog_msginit},
  {.submsg = &google_protobuf_BoolValue_msginit},
  {.submsg = &google_protobuf_BoolValue_msginit},
  {.submsg = &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_SetCurrentClientCertDetails_msginit},
  {.submsg = &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_UpgradeConfig_msginit},
  {.submsg = &google_protobuf_Duration_msginit},
  {.submsg = &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_InternalAddressConfig_msginit},
  {.submsg = &google_protobuf_Duration_msginit},
  {.submsg = &google_protobuf_Duration_msginit},
  {.submsg = &google_protobuf_UInt32Value_msginit},
  {.submsg = &google_protobuf_BoolValue_msginit},
  {.submsg = &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_msginit},
  {.submsg = &envoy_config_core_v3_HttpProtocolOptions_msginit},
  {.submsg = &envoy_extensions_filters_network_http_connection_manager_v3_RequestIDExtension_msginit},
  {.submsg = &envoy_extensions_filters_network_http_connection_manager_v3_LocalReplyConfig_msginit},
  {.submsg = &google_protobuf_BoolValue_msginit},
  {.submsg = &google_protobuf_Duration_msginit},
  {.submsg = &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_PathNormalizationOptions_msginit},
  {.submsg = &envoy_config_core_v3_Http3ProtocolOptions_msginit},
  {.submsg = &envoy_config_core_v3_TypedExtensionConfig_msginit},
  {.submsg = &envoy_config_core_v3_SchemeHeaderTransformation_msginit},
  {.submsg = &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_ProxyStatusConfig_msginit},
  {.submsg = &envoy_config_core_v3_TypedExtensionConfig_msginit},
  {.submsg = &envoy_config_core_v3_TypedExtensionConfig_msginit},
};

static const upb_MiniTable_Field envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager__fields[50] = {
  {1, UPB_SIZE(4, 4), UPB_SIZE(0, 0), kUpb_NoSub, 5, kUpb_FieldMode_Scalar | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(52, 56), UPB_SIZE(0, 0), kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(192, 336), UPB_SIZE(-25, -25), 0, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(192, 336), UPB_SIZE(-25, -25), 1, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(60, 72), UPB_SIZE(0, 0), 2, 11, kUpb_FieldMode_Array | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(64, 80), UPB_SIZE(1, 1), 3, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(68, 88), UPB_SIZE(2, 2), 4, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(72, 96), UPB_SIZE(3, 3), 5, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(76, 104), UPB_SIZE(4, 4), 6, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {10, UPB_SIZE(80, 112), UPB_SIZE(0, 0), kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {12, UPB_SIZE(88, 128), UPB_SIZE(5, 5), 7, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {13, UPB_SIZE(92, 136), UPB_SIZE(0, 0), 8, 11, kUpb_FieldMode_Array | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {14, UPB_SIZE(96, 144), UPB_SIZE(6, 6), 9, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {15, UPB_SIZE(100, 152), UPB_SIZE(7, 7), 10, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {16, UPB_SIZE(8, 8), UPB_SIZE(0, 0), kUpb_NoSub, 5, kUpb_FieldMode_Scalar | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {17, UPB_SIZE(104, 160), UPB_SIZE(8, 8), 11, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {18, UPB_SIZE(12, 12), UPB_SIZE(0, 0), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {19, UPB_SIZE(16, 16), UPB_SIZE(0, 0), kUpb_NoSub, 13, kUpb_FieldMode_Scalar | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {20, UPB_SIZE(20, 20), UPB_SIZE(0, 0), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {21, UPB_SIZE(21, 21), UPB_SIZE(0, 0), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {22, UPB_SIZE(108, 168), UPB_SIZE(0, 0), kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {23, UPB_SIZE(116, 184), UPB_SIZE(0, 0), 12, 11, kUpb_FieldMode_Array | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {24, UPB_SIZE(120, 192), UPB_SIZE(9, 9), 13, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {25, UPB_SIZE(124, 200), UPB_SIZE(10, 10), 14, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {26, UPB_SIZE(128, 208), UPB_SIZE(11, 11), 15, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {28, UPB_SIZE(132, 216), UPB_SIZE(12, 12), 16, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {29, UPB_SIZE(136, 224), UPB_SIZE(13, 13), 17, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {30, UPB_SIZE(140, 232), UPB_SIZE(14, 14), 18, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {31, UPB_SIZE(192, 336), UPB_SIZE(-25, -25), 19, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {32, UPB_SIZE(28, 28), UPB_SIZE(0, 0), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {33, UPB_SIZE(29, 29), UPB_SIZE(0, 0), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {34, UPB_SIZE(32, 32), UPB_SIZE(0, 0), kUpb_NoSub, 5, kUpb_FieldMode_Scalar | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {35, UPB_SIZE(144, 240), UPB_SIZE(15, 15), 20, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {36, UPB_SIZE(148, 248), UPB_SIZE(16, 16), 21, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {37, UPB_SIZE(36, 36), UPB_SIZE(0, 0), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {38, UPB_SIZE(152, 256), UPB_SIZE(17, 17), 22, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {39, UPB_SIZE(37, 37), UPB_SIZE(0, 0), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {40, UPB_SIZE(156, 264), UPB_SIZE(18, 18), 23, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {41, UPB_SIZE(160, 272), UPB_SIZE(19, 19), 24, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {42, UPB_SIZE(196, 344), UPB_SIZE(-41, -41), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {43, UPB_SIZE(164, 280), UPB_SIZE(20, 20), 25, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {44, UPB_SIZE(168, 288), UPB_SIZE(21, 21), 26, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {45, UPB_SIZE(44, 44), UPB_SIZE(0, 0), kUpb_NoSub, 5, kUpb_FieldMode_Scalar | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {46, UPB_SIZE(172, 296), UPB_SIZE(0, 0), 27, 11, kUpb_FieldMode_Array | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {47, UPB_SIZE(48, 48), UPB_SIZE(0, 0), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {48, UPB_SIZE(176, 304), UPB_SIZE(22, 22), 28, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {49, UPB_SIZE(180, 312), UPB_SIZE(23, 23), 29, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {50, UPB_SIZE(184, 320), UPB_SIZE(24, 24), 30, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {51, UPB_SIZE(49, 49), UPB_SIZE(0, 0), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {52, UPB_SIZE(188, 328), UPB_SIZE(0, 0), 31, 11, kUpb_FieldMode_Array | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_msginit = {
  &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_submsgs[0],
  &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager__fields[0],
  UPB_SIZE(200, 352), 50, kUpb_ExtMode_NonExtendable, 10, 255, 0,
};

static const upb_MiniTable_Sub envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_Tracing_submsgs[6] = {
  {.submsg = &envoy_type_v3_Percent_msginit},
  {.submsg = &envoy_type_v3_Percent_msginit},
  {.submsg = &envoy_type_v3_Percent_msginit},
  {.submsg = &google_protobuf_UInt32Value_msginit},
  {.submsg = &envoy_type_tracing_v3_CustomTag_msginit},
  {.submsg = &envoy_config_trace_v3_Tracing_Http_msginit},
};

static const upb_MiniTable_Field envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_Tracing__fields[7] = {
  {3, UPB_SIZE(4, 8), UPB_SIZE(1, 1), 0, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(8, 16), UPB_SIZE(2, 2), 1, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(12, 24), UPB_SIZE(3, 3), 2, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(1, 1), UPB_SIZE(0, 0), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(16, 32), UPB_SIZE(4, 4), 3, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(20, 40), UPB_SIZE(0, 0), 4, 11, kUpb_FieldMode_Array | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(24, 48), UPB_SIZE(5, 5), 5, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_Tracing_msginit = {
  &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_Tracing_submsgs[0],
  &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_Tracing__fields[0],
  UPB_SIZE(32, 56), 7, kUpb_ExtMode_NonExtendable, 0, 255, 0,
};

static const upb_MiniTable_Sub envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_InternalAddressConfig_submsgs[1] = {
  {.submsg = &envoy_config_core_v3_CidrRange_msginit},
};

static const upb_MiniTable_Field envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_InternalAddressConfig__fields[2] = {
  {1, UPB_SIZE(0, 0), UPB_SIZE(0, 0), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), UPB_SIZE(0, 0), 0, 11, kUpb_FieldMode_Array | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_InternalAddressConfig_msginit = {
  &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_InternalAddressConfig_submsgs[0],
  &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_InternalAddressConfig__fields[0],
  UPB_SIZE(8, 16), 2, kUpb_ExtMode_NonExtendable, 2, 255, 0,
};

static const upb_MiniTable_Sub envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_SetCurrentClientCertDetails_submsgs[1] = {
  {.submsg = &google_protobuf_BoolValue_msginit},
};

static const upb_MiniTable_Field envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_SetCurrentClientCertDetails__fields[5] = {
  {1, UPB_SIZE(8, 8), UPB_SIZE(1, 1), 0, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(1, 1), UPB_SIZE(0, 0), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(2, 2), UPB_SIZE(0, 0), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(3, 3), UPB_SIZE(0, 0), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(4, 4), UPB_SIZE(0, 0), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_SetCurrentClientCertDetails_msginit = {
  &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_SetCurrentClientCertDetails_submsgs[0],
  &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_SetCurrentClientCertDetails__fields[0],
  UPB_SIZE(16, 16), 5, kUpb_ExtMode_NonExtendable, 1, 255, 0,
};

static const upb_MiniTable_Sub envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_UpgradeConfig_submsgs[2] = {
  {.submsg = &envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter_msginit},
  {.submsg = &google_protobuf_BoolValue_msginit},
};

static const upb_MiniTable_Field envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_UpgradeConfig__fields[3] = {
  {1, UPB_SIZE(4, 8), UPB_SIZE(0, 0), kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 24), UPB_SIZE(0, 0), 0, 11, kUpb_FieldMode_Array | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(16, 32), UPB_SIZE(1, 1), 1, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_UpgradeConfig_msginit = {
  &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_UpgradeConfig_submsgs[0],
  &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_UpgradeConfig__fields[0],
  UPB_SIZE(24, 40), 3, kUpb_ExtMode_NonExtendable, 3, 255, 0,
};

static const upb_MiniTable_Sub envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_PathNormalizationOptions_submsgs[2] = {
  {.submsg = &envoy_type_http_v3_PathTransformation_msginit},
  {.submsg = &envoy_type_http_v3_PathTransformation_msginit},
};

static const upb_MiniTable_Field envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_PathNormalizationOptions__fields[2] = {
  {1, UPB_SIZE(4, 8), UPB_SIZE(1, 1), 0, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), UPB_SIZE(2, 2), 1, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_PathNormalizationOptions_msginit = {
  &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_PathNormalizationOptions_submsgs[0],
  &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_PathNormalizationOptions__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, 255, 0,
};

static const upb_MiniTable_Field envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_ProxyStatusConfig__fields[6] = {
  {1, UPB_SIZE(0, 0), UPB_SIZE(0, 0), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(1, 1), UPB_SIZE(0, 0), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(2, 2), UPB_SIZE(0, 0), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(3, 3), UPB_SIZE(0, 0), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(8, 8), UPB_SIZE(-5, -5), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(8, 8), UPB_SIZE(-5, -5), kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_ProxyStatusConfig_msginit = {
  NULL,
  &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_ProxyStatusConfig__fields[0],
  UPB_SIZE(16, 24), 6, kUpb_ExtMode_NonExtendable, 6, 255, 0,
};

static const upb_MiniTable_Sub envoy_extensions_filters_network_http_connection_manager_v3_LocalReplyConfig_submsgs[2] = {
  {.submsg = &envoy_extensions_filters_network_http_connection_manager_v3_ResponseMapper_msginit},
  {.submsg = &envoy_config_core_v3_SubstitutionFormatString_msginit},
};

static const upb_MiniTable_Field envoy_extensions_filters_network_http_connection_manager_v3_LocalReplyConfig__fields[2] = {
  {1, UPB_SIZE(4, 8), UPB_SIZE(0, 0), 0, 11, kUpb_FieldMode_Array | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), UPB_SIZE(1, 1), 1, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_filters_network_http_connection_manager_v3_LocalReplyConfig_msginit = {
  &envoy_extensions_filters_network_http_connection_manager_v3_LocalReplyConfig_submsgs[0],
  &envoy_extensions_filters_network_http_connection_manager_v3_LocalReplyConfig__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, 255, 0,
};

static const upb_MiniTable_Sub envoy_extensions_filters_network_http_connection_manager_v3_ResponseMapper_submsgs[5] = {
  {.submsg = &envoy_config_accesslog_v3_AccessLogFilter_msginit},
  {.submsg = &google_protobuf_UInt32Value_msginit},
  {.submsg = &envoy_config_core_v3_DataSource_msginit},
  {.submsg = &envoy_config_core_v3_SubstitutionFormatString_msginit},
  {.submsg = &envoy_config_core_v3_HeaderValueOption_msginit},
};

static const upb_MiniTable_Field envoy_extensions_filters_network_http_connection_manager_v3_ResponseMapper__fields[5] = {
  {1, UPB_SIZE(4, 8), UPB_SIZE(1, 1), 0, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), UPB_SIZE(2, 2), 1, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(12, 24), UPB_SIZE(3, 3), 2, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(16, 32), UPB_SIZE(4, 4), 3, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(20, 40), UPB_SIZE(0, 0), 4, 11, kUpb_FieldMode_Array | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_filters_network_http_connection_manager_v3_ResponseMapper_msginit = {
  &envoy_extensions_filters_network_http_connection_manager_v3_ResponseMapper_submsgs[0],
  &envoy_extensions_filters_network_http_connection_manager_v3_ResponseMapper__fields[0],
  UPB_SIZE(24, 48), 5, kUpb_ExtMode_NonExtendable, 5, 255, 0,
};

static const upb_MiniTable_Sub envoy_extensions_filters_network_http_connection_manager_v3_Rds_submsgs[1] = {
  {.submsg = &envoy_config_core_v3_ConfigSource_msginit},
};

static const upb_MiniTable_Field envoy_extensions_filters_network_http_connection_manager_v3_Rds__fields[2] = {
  {1, UPB_SIZE(4, 8), UPB_SIZE(1, 1), 0, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), UPB_SIZE(0, 0), kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_filters_network_http_connection_manager_v3_Rds_msginit = {
  &envoy_extensions_filters_network_http_connection_manager_v3_Rds_submsgs[0],
  &envoy_extensions_filters_network_http_connection_manager_v3_Rds__fields[0],
  UPB_SIZE(16, 32), 2, kUpb_ExtMode_NonExtendable, 2, 255, 0,
};

static const upb_MiniTable_Sub envoy_extensions_filters_network_http_connection_manager_v3_ScopedRouteConfigurationsList_submsgs[1] = {
  {.submsg = &envoy_config_route_v3_ScopedRouteConfiguration_msginit},
};

static const upb_MiniTable_Field envoy_extensions_filters_network_http_connection_manager_v3_ScopedRouteConfigurationsList__fields[1] = {
  {1, UPB_SIZE(0, 0), UPB_SIZE(0, 0), 0, 11, kUpb_FieldMode_Array | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_filters_network_http_connection_manager_v3_ScopedRouteConfigurationsList_msginit = {
  &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRouteConfigurationsList_submsgs[0],
  &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRouteConfigurationsList__fields[0],
  UPB_SIZE(8, 8), 1, kUpb_ExtMode_NonExtendable, 1, 255, 0,
};

static const upb_MiniTable_Sub envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_submsgs[4] = {
  {.submsg = &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_msginit},
  {.submsg = &envoy_config_core_v3_ConfigSource_msginit},
  {.submsg = &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRouteConfigurationsList_msginit},
  {.submsg = &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRds_msginit},
};

static const upb_MiniTable_Field envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes__fields[5] = {
  {1, UPB_SIZE(8, 8), UPB_SIZE(0, 0), kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), UPB_SIZE(1, 1), 0, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(20, 32), UPB_SIZE(2, 2), 1, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(24, 40), UPB_SIZE(-5, -5), 2, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(24, 40), UPB_SIZE(-5, -5), 3, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_msginit = {
  &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_submsgs[0],
  &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes__fields[0],
  UPB_SIZE(32, 48), 5, kUpb_ExtMode_NonExtendable, 5, 255, 0,
};

static const upb_MiniTable_Sub envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_submsgs[1] = {
  {.submsg = &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_FragmentBuilder_msginit},
};

static const upb_MiniTable_Field envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder__fields[1] = {
  {1, UPB_SIZE(0, 0), UPB_SIZE(0, 0), 0, 11, kUpb_FieldMode_Array | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_msginit = {
  &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_submsgs[0],
  &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder__fields[0],
  UPB_SIZE(8, 8), 1, kUpb_ExtMode_NonExtendable, 1, 255, 0,
};

static const upb_MiniTable_Sub envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_FragmentBuilder_submsgs[1] = {
  {.submsg = &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_FragmentBuilder_HeaderValueExtractor_msginit},
};

static const upb_MiniTable_Field envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_FragmentBuilder__fields[1] = {
  {1, UPB_SIZE(4, 8), UPB_SIZE(-1, -1), 0, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_FragmentBuilder_msginit = {
  &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_FragmentBuilder_submsgs[0],
  &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_FragmentBuilder__fields[0],
  UPB_SIZE(8, 16), 1, kUpb_ExtMode_NonExtendable, 1, 255, 0,
};

static const upb_MiniTable_Sub envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_FragmentBuilder_HeaderValueExtractor_submsgs[1] = {
  {.submsg = &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_FragmentBuilder_HeaderValueExtractor_KvElement_msginit},
};

static const upb_MiniTable_Field envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_FragmentBuilder_HeaderValueExtractor__fields[4] = {
  {1, UPB_SIZE(8, 8), UPB_SIZE(0, 0), kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), UPB_SIZE(0, 0), kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 40), UPB_SIZE(-1, -1), kUpb_NoSub, 13, kUpb_FieldMode_Scalar | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(4, 40), UPB_SIZE(-1, -1), 0, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_FragmentBuilder_HeaderValueExtractor_msginit = {
  &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_FragmentBuilder_HeaderValueExtractor_submsgs[0],
  &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_FragmentBuilder_HeaderValueExtractor__fields[0],
  UPB_SIZE(24, 48), 4, kUpb_ExtMode_NonExtendable, 4, 255, 0,
};

static const upb_MiniTable_Field envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_FragmentBuilder_HeaderValueExtractor_KvElement__fields[2] = {
  {1, UPB_SIZE(0, 0), UPB_SIZE(0, 0), kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), UPB_SIZE(0, 0), kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_FragmentBuilder_HeaderValueExtractor_KvElement_msginit = {
  NULL,
  &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_FragmentBuilder_HeaderValueExtractor_KvElement__fields[0],
  UPB_SIZE(16, 32), 2, kUpb_ExtMode_NonExtendable, 2, 255, 0,
};

static const upb_MiniTable_Sub envoy_extensions_filters_network_http_connection_manager_v3_ScopedRds_submsgs[1] = {
  {.submsg = &envoy_config_core_v3_ConfigSource_msginit},
};

static const upb_MiniTable_Field envoy_extensions_filters_network_http_connection_manager_v3_ScopedRds__fields[2] = {
  {1, UPB_SIZE(4, 8), UPB_SIZE(1, 1), 0, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), UPB_SIZE(0, 0), kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_filters_network_http_connection_manager_v3_ScopedRds_msginit = {
  &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRds_submsgs[0],
  &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRds__fields[0],
  UPB_SIZE(16, 32), 2, kUpb_ExtMode_NonExtendable, 2, 255, 0,
};

static const upb_MiniTable_Sub envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter_submsgs[2] = {
  {.submsg = &google_protobuf_Any_msginit},
  {.submsg = &envoy_config_core_v3_ExtensionConfigSource_msginit},
};

static const upb_MiniTable_Field envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter__fields[4] = {
  {1, UPB_SIZE(8, 8), UPB_SIZE(0, 0), kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(16, 24), UPB_SIZE(-1, -1), 0, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(16, 24), UPB_SIZE(-1, -1), 1, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(4, 4), UPB_SIZE(0, 0), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter_msginit = {
  &envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter_submsgs[0],
  &envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter__fields[0],
  UPB_SIZE(24, 32), 4, kUpb_ExtMode_NonExtendable, 1, 255, 0,
};

static const upb_MiniTable_Sub envoy_extensions_filters_network_http_connection_manager_v3_RequestIDExtension_submsgs[1] = {
  {.submsg = &google_protobuf_Any_msginit},
};

static const upb_MiniTable_Field envoy_extensions_filters_network_http_connection_manager_v3_RequestIDExtension__fields[1] = {
  {1, UPB_SIZE(4, 8), UPB_SIZE(1, 1), 0, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_filters_network_http_connection_manager_v3_RequestIDExtension_msginit = {
  &envoy_extensions_filters_network_http_connection_manager_v3_RequestIDExtension_submsgs[0],
  &envoy_extensions_filters_network_http_connection_manager_v3_RequestIDExtension__fields[0],
  UPB_SIZE(8, 16), 1, kUpb_ExtMode_NonExtendable, 1, 255, 0,
};

static const upb_MiniTable_Sub envoy_extensions_filters_network_http_connection_manager_v3_EnvoyMobileHttpConnectionManager_submsgs[1] = {
  {.submsg = &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_msginit},
};

static const upb_MiniTable_Field envoy_extensions_filters_network_http_connection_manager_v3_EnvoyMobileHttpConnectionManager__fields[1] = {
  {1, UPB_SIZE(4, 8), UPB_SIZE(1, 1), 0, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_filters_network_http_connection_manager_v3_EnvoyMobileHttpConnectionManager_msginit = {
  &envoy_extensions_filters_network_http_connection_manager_v3_EnvoyMobileHttpConnectionManager_submsgs[0],
  &envoy_extensions_filters_network_http_connection_manager_v3_EnvoyMobileHttpConnectionManager__fields[0],
  UPB_SIZE(8, 16), 1, kUpb_ExtMode_NonExtendable, 1, 255, 0,
};

static const upb_MiniTable *messages_layout[20] = {
  &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_msginit,
  &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_Tracing_msginit,
  &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_InternalAddressConfig_msginit,
  &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_SetCurrentClientCertDetails_msginit,
  &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_UpgradeConfig_msginit,
  &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_PathNormalizationOptions_msginit,
  &envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_ProxyStatusConfig_msginit,
  &envoy_extensions_filters_network_http_connection_manager_v3_LocalReplyConfig_msginit,
  &envoy_extensions_filters_network_http_connection_manager_v3_ResponseMapper_msginit,
  &envoy_extensions_filters_network_http_connection_manager_v3_Rds_msginit,
  &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRouteConfigurationsList_msginit,
  &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_msginit,
  &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_msginit,
  &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_FragmentBuilder_msginit,
  &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_FragmentBuilder_HeaderValueExtractor_msginit,
  &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRoutes_ScopeKeyBuilder_FragmentBuilder_HeaderValueExtractor_KvElement_msginit,
  &envoy_extensions_filters_network_http_connection_manager_v3_ScopedRds_msginit,
  &envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter_msginit,
  &envoy_extensions_filters_network_http_connection_manager_v3_RequestIDExtension_msginit,
  &envoy_extensions_filters_network_http_connection_manager_v3_EnvoyMobileHttpConnectionManager_msginit,
};

const upb_MiniTable_File envoy_extensions_filters_network_http_connection_manager_v3_http_connection_manager_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  20,
  0,
  0,
};

#include "upb/port_undef.inc"

