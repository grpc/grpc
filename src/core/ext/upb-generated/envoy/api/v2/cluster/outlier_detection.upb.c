/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/api/v2/cluster/outlier_detection.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg.h"
#include "envoy/api/v2/cluster/outlier_detection.upb.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "validate/validate.upb.h"
#include "gogoproto/gogo.upb.h"

#include "upb/port_def.inc"

static const upb_msglayout *const envoy_api_v2_cluster_OutlierDetection_submsgs[11] = {
  &google_protobuf_Duration_msginit,
  &google_protobuf_UInt32Value_msginit,
};

static const upb_msglayout_field envoy_api_v2_cluster_OutlierDetection__fields[11] = {
  {1, UPB_SIZE(0, 0), 0, 1, 11, 1},
  {2, UPB_SIZE(4, 8), 0, 0, 11, 1},
  {3, UPB_SIZE(8, 16), 0, 0, 11, 1},
  {4, UPB_SIZE(12, 24), 0, 1, 11, 1},
  {5, UPB_SIZE(16, 32), 0, 1, 11, 1},
  {6, UPB_SIZE(20, 40), 0, 1, 11, 1},
  {7, UPB_SIZE(24, 48), 0, 1, 11, 1},
  {8, UPB_SIZE(28, 56), 0, 1, 11, 1},
  {9, UPB_SIZE(32, 64), 0, 1, 11, 1},
  {10, UPB_SIZE(36, 72), 0, 1, 11, 1},
  {11, UPB_SIZE(40, 80), 0, 1, 11, 1},
};

const upb_msglayout envoy_api_v2_cluster_OutlierDetection_msginit = {
  &envoy_api_v2_cluster_OutlierDetection_submsgs[0],
  &envoy_api_v2_cluster_OutlierDetection__fields[0],
  UPB_SIZE(44, 88), 11, false,
};

#include "upb/port_undef.inc"

