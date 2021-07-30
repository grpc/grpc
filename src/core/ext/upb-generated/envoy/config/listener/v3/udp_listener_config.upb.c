/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/listener/v3/udp_listener_config.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg.h"
#include "envoy/config/listener/v3/udp_listener_config.upb.h"
#include "envoy/config/core/v3/udp_socket_config.upb.h"
#include "envoy/config/listener/v3/quic_config.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"

#include "upb/port_def.inc"

static const upb_msglayout *const envoy_config_listener_v3_UdpListenerConfig_submsgs[2] = {
  &envoy_config_core_v3_UdpSocketConfig_msginit,
  &envoy_config_listener_v3_QuicProtocolOptions_msginit,
};

static const upb_msglayout_field envoy_config_listener_v3_UdpListenerConfig__fields[2] = {
  {5, UPB_SIZE(4, 8), 1, 0, 11, 1},
  {7, UPB_SIZE(8, 16), 2, 1, 11, 1},
};

const upb_msglayout envoy_config_listener_v3_UdpListenerConfig_msginit = {
  &envoy_config_listener_v3_UdpListenerConfig_submsgs[0],
  &envoy_config_listener_v3_UdpListenerConfig__fields[0],
  UPB_SIZE(16, 24), 2, false, 255,
};

const upb_msglayout envoy_config_listener_v3_ActiveRawUdpListenerConfig_msginit = {
  NULL,
  NULL,
  UPB_SIZE(0, 0), 0, false, 255,
};

#include "upb/port_undef.inc"

