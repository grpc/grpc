/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/core/v3/udp_socket_config.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "envoy/config/core/v3/udp_socket_config.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "udpa/annotations/status.upb.h"
#include "validate/validate.upb.h"

#include "upb/port_def.inc"

static const upb_msglayout_sub envoy_config_core_v3_UdpSocketConfig_submsgs[2] = {
  {.submsg = &google_protobuf_BoolValue_msginit},
  {.submsg = &google_protobuf_UInt64Value_msginit},
};

static const upb_msglayout_field envoy_config_core_v3_UdpSocketConfig__fields[2] = {
  {1, UPB_SIZE(4, 8), 1, 1, 11, _UPB_MODE_SCALAR | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
  {2, UPB_SIZE(8, 16), 2, 0, 11, _UPB_MODE_SCALAR | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
};

const upb_msglayout envoy_config_core_v3_UdpSocketConfig_msginit = {
  &envoy_config_core_v3_UdpSocketConfig_submsgs[0],
  &envoy_config_core_v3_UdpSocketConfig__fields[0],
  UPB_SIZE(16, 24), 2, _UPB_MSGEXT_NONE, 2, 255,
};

static const upb_msglayout *messages_layout[1] = {
  &envoy_config_core_v3_UdpSocketConfig_msginit,
};

const upb_msglayout_file envoy_config_core_v3_udp_socket_config_proto_upb_file_layout = {
  messages_layout,
  NULL,
  1,
  0,
};

#include "upb/port_undef.inc"

