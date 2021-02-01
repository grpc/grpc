/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/core/v3/protocol.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg.h"
#include "envoy/config/core/v3/protocol.upb.h"
#include "envoy/type/v3/percent.upb.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

#include "upb/port_def.inc"

const upb_msglayout envoy_config_core_v3_TcpProtocolOptions_msginit = {
  NULL,
  NULL,
  UPB_SIZE(0, 0), 0, false, 255,
};

static const upb_msglayout_field envoy_config_core_v3_UpstreamHttpProtocolOptions__fields[2] = {
  {1, UPB_SIZE(0, 0), 0, 0, 8, 1},
  {2, UPB_SIZE(1, 1), 0, 0, 8, 1},
};

const upb_msglayout envoy_config_core_v3_UpstreamHttpProtocolOptions_msginit = {
  NULL,
  &envoy_config_core_v3_UpstreamHttpProtocolOptions__fields[0],
  UPB_SIZE(8, 8), 2, false, 255,
};

static const upb_msglayout *const envoy_config_core_v3_HttpProtocolOptions_submsgs[2] = {
  &google_protobuf_Duration_msginit,
  &google_protobuf_UInt32Value_msginit,
};

static const upb_msglayout_field envoy_config_core_v3_HttpProtocolOptions__fields[5] = {
  {1, UPB_SIZE(8, 8), 1, 0, 11, 1},
  {2, UPB_SIZE(12, 16), 2, 1, 11, 1},
  {3, UPB_SIZE(16, 24), 3, 0, 11, 1},
  {4, UPB_SIZE(20, 32), 4, 0, 11, 1},
  {5, UPB_SIZE(4, 4), 0, 0, 14, 1},
};

const upb_msglayout envoy_config_core_v3_HttpProtocolOptions_msginit = {
  &envoy_config_core_v3_HttpProtocolOptions_submsgs[0],
  &envoy_config_core_v3_HttpProtocolOptions__fields[0],
  UPB_SIZE(24, 40), 5, false, 255,
};

static const upb_msglayout *const envoy_config_core_v3_Http1ProtocolOptions_submsgs[2] = {
  &envoy_config_core_v3_Http1ProtocolOptions_HeaderKeyFormat_msginit,
  &google_protobuf_BoolValue_msginit,
};

static const upb_msglayout_field envoy_config_core_v3_Http1ProtocolOptions__fields[7] = {
  {1, UPB_SIZE(12, 24), 1, 1, 11, 1},
  {2, UPB_SIZE(1, 1), 0, 0, 8, 1},
  {3, UPB_SIZE(4, 8), 0, 0, 9, 1},
  {4, UPB_SIZE(16, 32), 2, 0, 11, 1},
  {5, UPB_SIZE(2, 2), 0, 0, 8, 1},
  {6, UPB_SIZE(3, 3), 0, 0, 8, 1},
  {7, UPB_SIZE(20, 40), 3, 1, 11, 1},
};

const upb_msglayout envoy_config_core_v3_Http1ProtocolOptions_msginit = {
  &envoy_config_core_v3_Http1ProtocolOptions_submsgs[0],
  &envoy_config_core_v3_Http1ProtocolOptions__fields[0],
  UPB_SIZE(24, 48), 7, false, 255,
};

static const upb_msglayout *const envoy_config_core_v3_Http1ProtocolOptions_HeaderKeyFormat_submsgs[1] = {
  &envoy_config_core_v3_Http1ProtocolOptions_HeaderKeyFormat_ProperCaseWords_msginit,
};

static const upb_msglayout_field envoy_config_core_v3_Http1ProtocolOptions_HeaderKeyFormat__fields[1] = {
  {1, UPB_SIZE(0, 0), UPB_SIZE(-5, -9), 0, 11, 1},
};

const upb_msglayout envoy_config_core_v3_Http1ProtocolOptions_HeaderKeyFormat_msginit = {
  &envoy_config_core_v3_Http1ProtocolOptions_HeaderKeyFormat_submsgs[0],
  &envoy_config_core_v3_Http1ProtocolOptions_HeaderKeyFormat__fields[0],
  UPB_SIZE(8, 16), 1, false, 255,
};

const upb_msglayout envoy_config_core_v3_Http1ProtocolOptions_HeaderKeyFormat_ProperCaseWords_msginit = {
  NULL,
  NULL,
  UPB_SIZE(0, 0), 0, false, 255,
};

static const upb_msglayout *const envoy_config_core_v3_KeepaliveSettings_submsgs[2] = {
  &envoy_type_v3_Percent_msginit,
  &google_protobuf_Duration_msginit,
};

static const upb_msglayout_field envoy_config_core_v3_KeepaliveSettings__fields[3] = {
  {1, UPB_SIZE(4, 8), 1, 1, 11, 1},
  {2, UPB_SIZE(8, 16), 2, 1, 11, 1},
  {3, UPB_SIZE(12, 24), 3, 0, 11, 1},
};

const upb_msglayout envoy_config_core_v3_KeepaliveSettings_msginit = {
  &envoy_config_core_v3_KeepaliveSettings_submsgs[0],
  &envoy_config_core_v3_KeepaliveSettings__fields[0],
  UPB_SIZE(16, 32), 3, false, 255,
};

static const upb_msglayout *const envoy_config_core_v3_Http2ProtocolOptions_submsgs[4] = {
  &envoy_config_core_v3_Http2ProtocolOptions_SettingsParameter_msginit,
  &envoy_config_core_v3_KeepaliveSettings_msginit,
  &google_protobuf_BoolValue_msginit,
  &google_protobuf_UInt32Value_msginit,
};

static const upb_msglayout_field envoy_config_core_v3_Http2ProtocolOptions__fields[15] = {
  {1, UPB_SIZE(8, 8), 1, 3, 11, 1},
  {2, UPB_SIZE(12, 16), 2, 3, 11, 1},
  {3, UPB_SIZE(16, 24), 3, 3, 11, 1},
  {4, UPB_SIZE(20, 32), 4, 3, 11, 1},
  {5, UPB_SIZE(2, 2), 0, 0, 8, 1},
  {6, UPB_SIZE(3, 3), 0, 0, 8, 1},
  {7, UPB_SIZE(24, 40), 5, 3, 11, 1},
  {8, UPB_SIZE(28, 48), 6, 3, 11, 1},
  {9, UPB_SIZE(32, 56), 7, 3, 11, 1},
  {10, UPB_SIZE(36, 64), 8, 3, 11, 1},
  {11, UPB_SIZE(40, 72), 9, 3, 11, 1},
  {12, UPB_SIZE(4, 4), 0, 0, 8, 1},
  {13, UPB_SIZE(52, 96), 0, 0, 11, 3},
  {14, UPB_SIZE(44, 80), 10, 2, 11, 1},
  {15, UPB_SIZE(48, 88), 11, 1, 11, 1},
};

const upb_msglayout envoy_config_core_v3_Http2ProtocolOptions_msginit = {
  &envoy_config_core_v3_Http2ProtocolOptions_submsgs[0],
  &envoy_config_core_v3_Http2ProtocolOptions__fields[0],
  UPB_SIZE(56, 104), 15, false, 255,
};

static const upb_msglayout *const envoy_config_core_v3_Http2ProtocolOptions_SettingsParameter_submsgs[1] = {
  &google_protobuf_UInt32Value_msginit,
};

static const upb_msglayout_field envoy_config_core_v3_Http2ProtocolOptions_SettingsParameter__fields[2] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, 1},
  {2, UPB_SIZE(8, 16), 2, 0, 11, 1},
};

const upb_msglayout envoy_config_core_v3_Http2ProtocolOptions_SettingsParameter_msginit = {
  &envoy_config_core_v3_Http2ProtocolOptions_SettingsParameter_submsgs[0],
  &envoy_config_core_v3_Http2ProtocolOptions_SettingsParameter__fields[0],
  UPB_SIZE(16, 24), 2, false, 255,
};

static const upb_msglayout *const envoy_config_core_v3_GrpcProtocolOptions_submsgs[1] = {
  &envoy_config_core_v3_Http2ProtocolOptions_msginit,
};

static const upb_msglayout_field envoy_config_core_v3_GrpcProtocolOptions__fields[1] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, 1},
};

const upb_msglayout envoy_config_core_v3_GrpcProtocolOptions_msginit = {
  &envoy_config_core_v3_GrpcProtocolOptions_submsgs[0],
  &envoy_config_core_v3_GrpcProtocolOptions__fields[0],
  UPB_SIZE(8, 16), 1, false, 255,
};

#include "upb/port_undef.inc"

