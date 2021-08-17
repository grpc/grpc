/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/core/v3/config_source.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "envoy/config/core/v3/config_source.upb.h"
#include "envoy/config/core/v3/grpc_service.upb.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "xds/core/v3/authority.upb.h"
#include "envoy/annotations/deprecation.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

#include "upb/port_def.inc"

static const upb_msglayout *const envoy_config_core_v3_ApiConfigSource_submsgs[3] = {
  &envoy_config_core_v3_GrpcService_msginit,
  &envoy_config_core_v3_RateLimitSettings_msginit,
  &google_protobuf_Duration_msginit,
};

static const upb_msglayout_field envoy_config_core_v3_ApiConfigSource__fields[8] = {
  {1, UPB_SIZE(4, 4), 0, 0, 14, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(28, 40), 0, 0, 9, _UPB_MODE_ARRAY},
  {3, UPB_SIZE(16, 16), 1, 2, 11, _UPB_MODE_SCALAR},
  {4, UPB_SIZE(32, 48), 0, 0, 11, _UPB_MODE_ARRAY},
  {5, UPB_SIZE(20, 24), 2, 2, 11, _UPB_MODE_SCALAR},
  {6, UPB_SIZE(24, 32), 3, 1, 11, _UPB_MODE_SCALAR},
  {7, UPB_SIZE(12, 12), 0, 0, 8, _UPB_MODE_SCALAR},
  {8, UPB_SIZE(8, 8), 0, 0, 14, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_config_core_v3_ApiConfigSource_msginit = {
  &envoy_config_core_v3_ApiConfigSource_submsgs[0],
  &envoy_config_core_v3_ApiConfigSource__fields[0],
  UPB_SIZE(40, 56), 8, false, 8, 255,
};

const upb_msglayout envoy_config_core_v3_AggregatedConfigSource_msginit = {
  NULL,
  NULL,
  UPB_SIZE(0, 0), 0, false, 0, 255,
};

static const upb_msglayout_field envoy_config_core_v3_SelfConfigSource__fields[1] = {
  {1, UPB_SIZE(0, 0), 0, 0, 14, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_config_core_v3_SelfConfigSource_msginit = {
  NULL,
  &envoy_config_core_v3_SelfConfigSource__fields[0],
  UPB_SIZE(8, 8), 1, false, 1, 255,
};

static const upb_msglayout *const envoy_config_core_v3_RateLimitSettings_submsgs[2] = {
  &google_protobuf_DoubleValue_msginit,
  &google_protobuf_UInt32Value_msginit,
};

static const upb_msglayout_field envoy_config_core_v3_RateLimitSettings__fields[2] = {
  {1, UPB_SIZE(4, 8), 1, 1, 11, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(8, 16), 2, 0, 11, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_config_core_v3_RateLimitSettings_msginit = {
  &envoy_config_core_v3_RateLimitSettings_submsgs[0],
  &envoy_config_core_v3_RateLimitSettings__fields[0],
  UPB_SIZE(16, 24), 2, false, 2, 255,
};

static const upb_msglayout *const envoy_config_core_v3_ConfigSource_submsgs[5] = {
  &envoy_config_core_v3_AggregatedConfigSource_msginit,
  &envoy_config_core_v3_ApiConfigSource_msginit,
  &envoy_config_core_v3_SelfConfigSource_msginit,
  &google_protobuf_Duration_msginit,
  &xds_core_v3_Authority_msginit,
};

static const upb_msglayout_field envoy_config_core_v3_ConfigSource__fields[7] = {
  {1, UPB_SIZE(16, 24), UPB_SIZE(-25, -41), 0, 9, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(16, 24), UPB_SIZE(-25, -41), 1, 11, _UPB_MODE_SCALAR},
  {3, UPB_SIZE(16, 24), UPB_SIZE(-25, -41), 0, 11, _UPB_MODE_SCALAR},
  {4, UPB_SIZE(8, 8), 1, 3, 11, _UPB_MODE_SCALAR},
  {5, UPB_SIZE(16, 24), UPB_SIZE(-25, -41), 2, 11, _UPB_MODE_SCALAR},
  {6, UPB_SIZE(4, 4), 0, 0, 14, _UPB_MODE_SCALAR},
  {7, UPB_SIZE(12, 16), 0, 4, 11, _UPB_MODE_ARRAY},
};

const upb_msglayout envoy_config_core_v3_ConfigSource_msginit = {
  &envoy_config_core_v3_ConfigSource_submsgs[0],
  &envoy_config_core_v3_ConfigSource__fields[0],
  UPB_SIZE(32, 48), 7, false, 7, 255,
};

#include "upb/port_undef.inc"

