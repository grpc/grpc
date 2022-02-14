/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/route/v3/route.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "envoy/config/route/v3/route.upb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/config/core/v3/config_source.upb.h"
#include "envoy/config/core/v3/extension.upb.h"
#include "envoy/config/route/v3/route_components.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

#include "upb/port_def.inc"

static const upb_msglayout_sub envoy_config_route_v3_RouteConfiguration_submsgs[6] = {
  {.submsg = &envoy_config_core_v3_HeaderValueOption_msginit},
  {.submsg = &envoy_config_route_v3_ClusterSpecifierPlugin_msginit},
  {.submsg = &envoy_config_route_v3_Vhds_msginit},
  {.submsg = &envoy_config_route_v3_VirtualHost_msginit},
  {.submsg = &google_protobuf_BoolValue_msginit},
  {.submsg = &google_protobuf_UInt32Value_msginit},
};

static const upb_msglayout_field envoy_config_route_v3_RouteConfiguration__fields[12] = {
  {1, UPB_SIZE(4, 8), 0, 0, 9, _UPB_MODE_SCALAR | (_UPB_REP_STRVIEW << _UPB_REP_SHIFT)},
  {2, UPB_SIZE(24, 48), 0, 3, 11, _UPB_MODE_ARRAY | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
  {3, UPB_SIZE(28, 56), 0, 0, 9, _UPB_MODE_ARRAY | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
  {4, UPB_SIZE(32, 64), 0, 0, 11, _UPB_MODE_ARRAY | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
  {5, UPB_SIZE(36, 72), 0, 0, 9, _UPB_MODE_ARRAY | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
  {6, UPB_SIZE(40, 80), 0, 0, 11, _UPB_MODE_ARRAY | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
  {7, UPB_SIZE(12, 24), 1, 4, 11, _UPB_MODE_SCALAR | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
  {8, UPB_SIZE(44, 88), 0, 0, 9, _UPB_MODE_ARRAY | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
  {9, UPB_SIZE(16, 32), 2, 2, 11, _UPB_MODE_SCALAR | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
  {10, UPB_SIZE(1, 1), 0, 0, 8, _UPB_MODE_SCALAR | (_UPB_REP_1BYTE << _UPB_REP_SHIFT)},
  {11, UPB_SIZE(20, 40), 3, 5, 11, _UPB_MODE_SCALAR | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
  {12, UPB_SIZE(48, 96), 0, 1, 11, _UPB_MODE_ARRAY | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
};

const upb_msglayout envoy_config_route_v3_RouteConfiguration_msginit = {
  &envoy_config_route_v3_RouteConfiguration_submsgs[0],
  &envoy_config_route_v3_RouteConfiguration__fields[0],
  UPB_SIZE(56, 112), 12, _UPB_MSGEXT_NONE, 12, 255,
};

static const upb_msglayout_sub envoy_config_route_v3_ClusterSpecifierPlugin_submsgs[1] = {
  {.submsg = &envoy_config_core_v3_TypedExtensionConfig_msginit},
};

static const upb_msglayout_field envoy_config_route_v3_ClusterSpecifierPlugin__fields[1] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, _UPB_MODE_SCALAR | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
};

const upb_msglayout envoy_config_route_v3_ClusterSpecifierPlugin_msginit = {
  &envoy_config_route_v3_ClusterSpecifierPlugin_submsgs[0],
  &envoy_config_route_v3_ClusterSpecifierPlugin__fields[0],
  UPB_SIZE(8, 16), 1, _UPB_MSGEXT_NONE, 1, 255,
};

static const upb_msglayout_sub envoy_config_route_v3_Vhds_submsgs[1] = {
  {.submsg = &envoy_config_core_v3_ConfigSource_msginit},
};

static const upb_msglayout_field envoy_config_route_v3_Vhds__fields[1] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, _UPB_MODE_SCALAR | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
};

const upb_msglayout envoy_config_route_v3_Vhds_msginit = {
  &envoy_config_route_v3_Vhds_submsgs[0],
  &envoy_config_route_v3_Vhds__fields[0],
  UPB_SIZE(8, 16), 1, _UPB_MSGEXT_NONE, 1, 255,
};

static const upb_msglayout *messages_layout[3] = {
  &envoy_config_route_v3_RouteConfiguration_msginit,
  &envoy_config_route_v3_ClusterSpecifierPlugin_msginit,
  &envoy_config_route_v3_Vhds_msginit,
};

const upb_msglayout_file envoy_config_route_v3_route_proto_upb_file_layout = {
  messages_layout,
  NULL,
  3,
  0,
};

#include "upb/port_undef.inc"

