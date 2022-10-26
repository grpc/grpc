/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/endpoint/v3/load_report.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "envoy/config/endpoint/v3/load_report.upb.h"
#include "envoy/config/core/v3/address.upb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/struct.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

#include "upb/port_def.inc"

static const upb_MiniTable_Sub envoy_config_endpoint_v3_UpstreamLocalityStats_submsgs[3] = {
  {.submsg = &envoy_config_core_v3_Locality_msginit},
  {.submsg = &envoy_config_endpoint_v3_EndpointLoadMetricStats_msginit},
  {.submsg = &envoy_config_endpoint_v3_UpstreamEndpointStats_msginit},
};

static const upb_MiniTable_Field envoy_config_endpoint_v3_UpstreamLocalityStats__fields[8] = {
  {1, UPB_SIZE(8, 8), UPB_SIZE(1, 1), 0, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(24, 32), UPB_SIZE(0, 0), kUpb_NoSub, 4, kUpb_FieldMode_Scalar | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(32, 40), UPB_SIZE(0, 0), kUpb_NoSub, 4, kUpb_FieldMode_Scalar | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(40, 48), UPB_SIZE(0, 0), kUpb_NoSub, 4, kUpb_FieldMode_Scalar | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(12, 16), UPB_SIZE(0, 0), 1, 11, kUpb_FieldMode_Array | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(4, 4), UPB_SIZE(0, 0), kUpb_NoSub, 13, kUpb_FieldMode_Scalar | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(16, 24), UPB_SIZE(0, 0), 2, 11, kUpb_FieldMode_Array | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(48, 56), UPB_SIZE(0, 0), kUpb_NoSub, 4, kUpb_FieldMode_Scalar | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_endpoint_v3_UpstreamLocalityStats_msginit = {
  &envoy_config_endpoint_v3_UpstreamLocalityStats_submsgs[0],
  &envoy_config_endpoint_v3_UpstreamLocalityStats__fields[0],
  UPB_SIZE(56, 64), 8, kUpb_ExtMode_NonExtendable, 8, 255, 0,
};

static const upb_MiniTable_Sub envoy_config_endpoint_v3_UpstreamEndpointStats_submsgs[3] = {
  {.submsg = &envoy_config_core_v3_Address_msginit},
  {.submsg = &envoy_config_endpoint_v3_EndpointLoadMetricStats_msginit},
  {.submsg = &google_protobuf_Struct_msginit},
};

static const upb_MiniTable_Field envoy_config_endpoint_v3_UpstreamEndpointStats__fields[7] = {
  {1, UPB_SIZE(4, 8), UPB_SIZE(1, 1), 0, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 32), UPB_SIZE(0, 0), kUpb_NoSub, 4, kUpb_FieldMode_Scalar | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(24, 40), UPB_SIZE(0, 0), kUpb_NoSub, 4, kUpb_FieldMode_Scalar | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(32, 48), UPB_SIZE(0, 0), kUpb_NoSub, 4, kUpb_FieldMode_Scalar | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(8, 16), UPB_SIZE(0, 0), 1, 11, kUpb_FieldMode_Array | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(12, 24), UPB_SIZE(2, 2), 2, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(40, 56), UPB_SIZE(0, 0), kUpb_NoSub, 4, kUpb_FieldMode_Scalar | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_endpoint_v3_UpstreamEndpointStats_msginit = {
  &envoy_config_endpoint_v3_UpstreamEndpointStats_submsgs[0],
  &envoy_config_endpoint_v3_UpstreamEndpointStats__fields[0],
  UPB_SIZE(48, 64), 7, kUpb_ExtMode_NonExtendable, 7, 255, 0,
};

static const upb_MiniTable_Field envoy_config_endpoint_v3_EndpointLoadMetricStats__fields[3] = {
  {1, UPB_SIZE(0, 0), UPB_SIZE(0, 0), kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), UPB_SIZE(0, 0), kUpb_NoSub, 4, kUpb_FieldMode_Scalar | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(16, 24), UPB_SIZE(0, 0), kUpb_NoSub, 1, kUpb_FieldMode_Scalar | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_endpoint_v3_EndpointLoadMetricStats_msginit = {
  NULL,
  &envoy_config_endpoint_v3_EndpointLoadMetricStats__fields[0],
  UPB_SIZE(24, 32), 3, kUpb_ExtMode_NonExtendable, 3, 255, 0,
};

static const upb_MiniTable_Sub envoy_config_endpoint_v3_ClusterStats_submsgs[3] = {
  {.submsg = &envoy_config_endpoint_v3_UpstreamLocalityStats_msginit},
  {.submsg = &google_protobuf_Duration_msginit},
  {.submsg = &envoy_config_endpoint_v3_ClusterStats_DroppedRequests_msginit},
};

static const upb_MiniTable_Field envoy_config_endpoint_v3_ClusterStats__fields[6] = {
  {1, UPB_SIZE(4, 8), UPB_SIZE(0, 0), kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 24), UPB_SIZE(0, 0), 0, 11, kUpb_FieldMode_Array | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(32, 64), UPB_SIZE(0, 0), kUpb_NoSub, 4, kUpb_FieldMode_Scalar | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(16, 32), UPB_SIZE(1, 1), 1, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(20, 40), UPB_SIZE(0, 0), 2, 11, kUpb_FieldMode_Array | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(24, 48), UPB_SIZE(0, 0), kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_endpoint_v3_ClusterStats_msginit = {
  &envoy_config_endpoint_v3_ClusterStats_submsgs[0],
  &envoy_config_endpoint_v3_ClusterStats__fields[0],
  UPB_SIZE(40, 72), 6, kUpb_ExtMode_NonExtendable, 6, 255, 0,
};

static const upb_MiniTable_Field envoy_config_endpoint_v3_ClusterStats_DroppedRequests__fields[2] = {
  {1, UPB_SIZE(0, 0), UPB_SIZE(0, 0), kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), UPB_SIZE(0, 0), kUpb_NoSub, 4, kUpb_FieldMode_Scalar | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_endpoint_v3_ClusterStats_DroppedRequests_msginit = {
  NULL,
  &envoy_config_endpoint_v3_ClusterStats_DroppedRequests__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, 255, 0,
};

static const upb_MiniTable *messages_layout[5] = {
  &envoy_config_endpoint_v3_UpstreamLocalityStats_msginit,
  &envoy_config_endpoint_v3_UpstreamEndpointStats_msginit,
  &envoy_config_endpoint_v3_EndpointLoadMetricStats_msginit,
  &envoy_config_endpoint_v3_ClusterStats_msginit,
  &envoy_config_endpoint_v3_ClusterStats_DroppedRequests_msginit,
};

const upb_MiniTable_File envoy_config_endpoint_v3_load_report_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  5,
  0,
  0,
};

#include "upb/port_undef.inc"

