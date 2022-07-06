/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/tap/v3/common.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "envoy/config/tap/v3/common.upb.h"
#include "envoy/config/common/matcher/v3/matcher.upb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/config/core/v3/grpc_service.upb.h"
#include "envoy/config/route/v3/route_components.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "envoy/annotations/deprecation.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

#include "upb/port_def.inc"

static const upb_MiniTable_Sub envoy_config_tap_v3_TapConfig_submsgs[4] = {
  {.submsg = &envoy_config_tap_v3_MatchPredicate_msginit},
  {.submsg = &envoy_config_tap_v3_OutputConfig_msginit},
  {.submsg = &envoy_config_core_v3_RuntimeFractionalPercent_msginit},
  {.submsg = &envoy_config_common_matcher_v3_MatchPredicate_msginit},
};

static const upb_MiniTable_Field envoy_config_tap_v3_TapConfig__fields[4] = {
  {1, UPB_SIZE(4, 8), UPB_SIZE(1, 1), 0, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), UPB_SIZE(2, 2), 1, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(12, 24), UPB_SIZE(3, 3), 2, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(16, 32), UPB_SIZE(4, 4), 3, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_tap_v3_TapConfig_msginit = {
  &envoy_config_tap_v3_TapConfig_submsgs[0],
  &envoy_config_tap_v3_TapConfig__fields[0],
  UPB_SIZE(24, 40), 4, kUpb_ExtMode_NonExtendable, 4, 255, 0,
};

static const upb_MiniTable_Sub envoy_config_tap_v3_MatchPredicate_submsgs[9] = {
  {.submsg = &envoy_config_tap_v3_MatchPredicate_MatchSet_msginit},
  {.submsg = &envoy_config_tap_v3_MatchPredicate_MatchSet_msginit},
  {.submsg = &envoy_config_tap_v3_MatchPredicate_msginit},
  {.submsg = &envoy_config_tap_v3_HttpHeadersMatch_msginit},
  {.submsg = &envoy_config_tap_v3_HttpHeadersMatch_msginit},
  {.submsg = &envoy_config_tap_v3_HttpHeadersMatch_msginit},
  {.submsg = &envoy_config_tap_v3_HttpHeadersMatch_msginit},
  {.submsg = &envoy_config_tap_v3_HttpGenericBodyMatch_msginit},
  {.submsg = &envoy_config_tap_v3_HttpGenericBodyMatch_msginit},
};

static const upb_MiniTable_Field envoy_config_tap_v3_MatchPredicate__fields[10] = {
  {1, UPB_SIZE(4, 8), UPB_SIZE(-1, -1), 0, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), UPB_SIZE(-1, -1), 1, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 8), UPB_SIZE(-1, -1), 2, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(4, 8), UPB_SIZE(-1, -1), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(4, 8), UPB_SIZE(-1, -1), 3, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(4, 8), UPB_SIZE(-1, -1), 4, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(4, 8), UPB_SIZE(-1, -1), 5, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(4, 8), UPB_SIZE(-1, -1), 6, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(4, 8), UPB_SIZE(-1, -1), 7, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {10, UPB_SIZE(4, 8), UPB_SIZE(-1, -1), 8, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_tap_v3_MatchPredicate_msginit = {
  &envoy_config_tap_v3_MatchPredicate_submsgs[0],
  &envoy_config_tap_v3_MatchPredicate__fields[0],
  UPB_SIZE(8, 16), 10, kUpb_ExtMode_NonExtendable, 10, 255, 0,
};

static const upb_MiniTable_Sub envoy_config_tap_v3_MatchPredicate_MatchSet_submsgs[1] = {
  {.submsg = &envoy_config_tap_v3_MatchPredicate_msginit},
};

static const upb_MiniTable_Field envoy_config_tap_v3_MatchPredicate_MatchSet__fields[1] = {
  {1, UPB_SIZE(0, 0), UPB_SIZE(0, 0), 0, 11, kUpb_FieldMode_Array | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_tap_v3_MatchPredicate_MatchSet_msginit = {
  &envoy_config_tap_v3_MatchPredicate_MatchSet_submsgs[0],
  &envoy_config_tap_v3_MatchPredicate_MatchSet__fields[0],
  UPB_SIZE(8, 8), 1, kUpb_ExtMode_NonExtendable, 1, 255, 0,
};

static const upb_MiniTable_Sub envoy_config_tap_v3_HttpHeadersMatch_submsgs[1] = {
  {.submsg = &envoy_config_route_v3_HeaderMatcher_msginit},
};

static const upb_MiniTable_Field envoy_config_tap_v3_HttpHeadersMatch__fields[1] = {
  {1, UPB_SIZE(0, 0), UPB_SIZE(0, 0), 0, 11, kUpb_FieldMode_Array | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_tap_v3_HttpHeadersMatch_msginit = {
  &envoy_config_tap_v3_HttpHeadersMatch_submsgs[0],
  &envoy_config_tap_v3_HttpHeadersMatch__fields[0],
  UPB_SIZE(8, 8), 1, kUpb_ExtMode_NonExtendable, 1, 255, 0,
};

static const upb_MiniTable_Sub envoy_config_tap_v3_HttpGenericBodyMatch_submsgs[1] = {
  {.submsg = &envoy_config_tap_v3_HttpGenericBodyMatch_GenericTextMatch_msginit},
};

static const upb_MiniTable_Field envoy_config_tap_v3_HttpGenericBodyMatch__fields[2] = {
  {1, UPB_SIZE(0, 0), UPB_SIZE(0, 0), kUpb_NoSub, 13, kUpb_FieldMode_Scalar | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), UPB_SIZE(0, 0), 0, 11, kUpb_FieldMode_Array | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_tap_v3_HttpGenericBodyMatch_msginit = {
  &envoy_config_tap_v3_HttpGenericBodyMatch_submsgs[0],
  &envoy_config_tap_v3_HttpGenericBodyMatch__fields[0],
  UPB_SIZE(8, 16), 2, kUpb_ExtMode_NonExtendable, 2, 255, 0,
};

static const upb_MiniTable_Field envoy_config_tap_v3_HttpGenericBodyMatch_GenericTextMatch__fields[2] = {
  {1, UPB_SIZE(4, 8), UPB_SIZE(-1, -1), kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), UPB_SIZE(-1, -1), kUpb_NoSub, 12, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_tap_v3_HttpGenericBodyMatch_GenericTextMatch_msginit = {
  NULL,
  &envoy_config_tap_v3_HttpGenericBodyMatch_GenericTextMatch__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, 255, 0,
};

static const upb_MiniTable_Sub envoy_config_tap_v3_OutputConfig_submsgs[3] = {
  {.submsg = &envoy_config_tap_v3_OutputSink_msginit},
  {.submsg = &google_protobuf_UInt32Value_msginit},
  {.submsg = &google_protobuf_UInt32Value_msginit},
};

static const upb_MiniTable_Field envoy_config_tap_v3_OutputConfig__fields[4] = {
  {1, UPB_SIZE(4, 8), UPB_SIZE(0, 0), 0, 11, kUpb_FieldMode_Array | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), UPB_SIZE(1, 1), 1, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(12, 24), UPB_SIZE(2, 2), 2, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(1, 1), UPB_SIZE(0, 0), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_tap_v3_OutputConfig_msginit = {
  &envoy_config_tap_v3_OutputConfig_submsgs[0],
  &envoy_config_tap_v3_OutputConfig__fields[0],
  UPB_SIZE(16, 32), 4, kUpb_ExtMode_NonExtendable, 4, 255, 0,
};

static const upb_MiniTable_Sub envoy_config_tap_v3_OutputSink_submsgs[3] = {
  {.submsg = &envoy_config_tap_v3_StreamingAdminSink_msginit},
  {.submsg = &envoy_config_tap_v3_FilePerTapSink_msginit},
  {.submsg = &envoy_config_tap_v3_StreamingGrpcSink_msginit},
};

static const upb_MiniTable_Field envoy_config_tap_v3_OutputSink__fields[4] = {
  {1, UPB_SIZE(0, 0), UPB_SIZE(0, 0), kUpb_NoSub, 5, kUpb_FieldMode_Scalar | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 8), UPB_SIZE(-5, -5), 0, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(8, 8), UPB_SIZE(-5, -5), 1, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(8, 8), UPB_SIZE(-5, -5), 2, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_tap_v3_OutputSink_msginit = {
  &envoy_config_tap_v3_OutputSink_submsgs[0],
  &envoy_config_tap_v3_OutputSink__fields[0],
  UPB_SIZE(16, 16), 4, kUpb_ExtMode_NonExtendable, 4, 255, 0,
};

const upb_MiniTable envoy_config_tap_v3_StreamingAdminSink_msginit = {
  NULL,
  NULL,
  UPB_SIZE(0, 0), 0, kUpb_ExtMode_NonExtendable, 0, 255, 0,
};

static const upb_MiniTable_Field envoy_config_tap_v3_FilePerTapSink__fields[1] = {
  {1, UPB_SIZE(0, 0), UPB_SIZE(0, 0), kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_tap_v3_FilePerTapSink_msginit = {
  NULL,
  &envoy_config_tap_v3_FilePerTapSink__fields[0],
  UPB_SIZE(8, 16), 1, kUpb_ExtMode_NonExtendable, 1, 255, 0,
};

static const upb_MiniTable_Sub envoy_config_tap_v3_StreamingGrpcSink_submsgs[1] = {
  {.submsg = &envoy_config_core_v3_GrpcService_msginit},
};

static const upb_MiniTable_Field envoy_config_tap_v3_StreamingGrpcSink__fields[2] = {
  {1, UPB_SIZE(4, 8), UPB_SIZE(0, 0), kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 24), UPB_SIZE(1, 1), 0, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_tap_v3_StreamingGrpcSink_msginit = {
  &envoy_config_tap_v3_StreamingGrpcSink_submsgs[0],
  &envoy_config_tap_v3_StreamingGrpcSink__fields[0],
  UPB_SIZE(16, 32), 2, kUpb_ExtMode_NonExtendable, 2, 255, 0,
};

static const upb_MiniTable *messages_layout[11] = {
  &envoy_config_tap_v3_TapConfig_msginit,
  &envoy_config_tap_v3_MatchPredicate_msginit,
  &envoy_config_tap_v3_MatchPredicate_MatchSet_msginit,
  &envoy_config_tap_v3_HttpHeadersMatch_msginit,
  &envoy_config_tap_v3_HttpGenericBodyMatch_msginit,
  &envoy_config_tap_v3_HttpGenericBodyMatch_GenericTextMatch_msginit,
  &envoy_config_tap_v3_OutputConfig_msginit,
  &envoy_config_tap_v3_OutputSink_msginit,
  &envoy_config_tap_v3_StreamingAdminSink_msginit,
  &envoy_config_tap_v3_FilePerTapSink_msginit,
  &envoy_config_tap_v3_StreamingGrpcSink_msginit,
};

const upb_MiniTable_File envoy_config_tap_v3_common_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  11,
  0,
  0,
};

#include "upb/port_undef.inc"

