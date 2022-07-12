/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/metrics/v3/metrics_service.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "envoy/config/metrics/v3/metrics_service.upb.h"
#include "envoy/config/core/v3/config_source.upb.h"
#include "envoy/config/core/v3/grpc_service.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

#include "upb/port_def.inc"

static const upb_MiniTable_Sub envoy_config_metrics_v3_MetricsServiceConfig_submsgs[2] = {
  {.submsg = &envoy_config_core_v3_GrpcService_msginit},
  {.submsg = &google_protobuf_BoolValue_msginit},
};

static const upb_MiniTable_Field envoy_config_metrics_v3_MetricsServiceConfig__fields[4] = {
  {1, UPB_SIZE(12, 16), UPB_SIZE(1, 1), 0, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), UPB_SIZE(2, 2), 1, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 4), UPB_SIZE(0, 0), kUpb_NoSub, 5, kUpb_FieldMode_Scalar | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(8, 8), UPB_SIZE(0, 0), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_metrics_v3_MetricsServiceConfig_msginit = {
  &envoy_config_metrics_v3_MetricsServiceConfig_submsgs[0],
  &envoy_config_metrics_v3_MetricsServiceConfig__fields[0],
  UPB_SIZE(24, 32), 4, kUpb_ExtMode_NonExtendable, 4, 255, 0,
};

static const upb_MiniTable *messages_layout[1] = {
  &envoy_config_metrics_v3_MetricsServiceConfig_msginit,
};

const upb_MiniTable_File envoy_config_metrics_v3_metrics_service_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  1,
  0,
  0,
};

#include "upb/port_undef.inc"

