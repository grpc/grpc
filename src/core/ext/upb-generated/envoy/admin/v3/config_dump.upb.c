/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/admin/v3/config_dump.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "envoy/admin/v3/config_dump.upb.h"
#include "envoy/config/bootstrap/v3/bootstrap.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/timestamp.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"

#include "upb/port_def.inc"

static const upb_msglayout *const envoy_admin_v3_ConfigDump_submsgs[1] = {
  &google_protobuf_Any_msginit,
};

static const upb_msglayout_field envoy_admin_v3_ConfigDump__fields[1] = {
  {1, UPB_SIZE(0, 0), 0, 0, 11, _UPB_MODE_ARRAY},
};

const upb_msglayout envoy_admin_v3_ConfigDump_msginit = {
  &envoy_admin_v3_ConfigDump_submsgs[0],
  &envoy_admin_v3_ConfigDump__fields[0],
  UPB_SIZE(8, 8), 1, false, 1, 255,
};

static const upb_msglayout *const envoy_admin_v3_UpdateFailureState_submsgs[2] = {
  &google_protobuf_Any_msginit,
  &google_protobuf_Timestamp_msginit,
};

static const upb_msglayout_field envoy_admin_v3_UpdateFailureState__fields[4] = {
  {1, UPB_SIZE(20, 40), 1, 0, 11, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(24, 48), 2, 1, 11, _UPB_MODE_SCALAR},
  {3, UPB_SIZE(4, 8), 0, 0, 9, _UPB_MODE_SCALAR},
  {4, UPB_SIZE(12, 24), 0, 0, 9, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_admin_v3_UpdateFailureState_msginit = {
  &envoy_admin_v3_UpdateFailureState_submsgs[0],
  &envoy_admin_v3_UpdateFailureState__fields[0],
  UPB_SIZE(32, 64), 4, false, 4, 255,
};

static const upb_msglayout *const envoy_admin_v3_BootstrapConfigDump_submsgs[2] = {
  &envoy_config_bootstrap_v3_Bootstrap_msginit,
  &google_protobuf_Timestamp_msginit,
};

static const upb_msglayout_field envoy_admin_v3_BootstrapConfigDump__fields[2] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(8, 16), 2, 1, 11, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_admin_v3_BootstrapConfigDump_msginit = {
  &envoy_admin_v3_BootstrapConfigDump_submsgs[0],
  &envoy_admin_v3_BootstrapConfigDump__fields[0],
  UPB_SIZE(16, 24), 2, false, 2, 255,
};

static const upb_msglayout *const envoy_admin_v3_ListenersConfigDump_submsgs[2] = {
  &envoy_admin_v3_ListenersConfigDump_DynamicListener_msginit,
  &envoy_admin_v3_ListenersConfigDump_StaticListener_msginit,
};

static const upb_msglayout_field envoy_admin_v3_ListenersConfigDump__fields[3] = {
  {1, UPB_SIZE(0, 0), 0, 0, 9, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(8, 16), 0, 1, 11, _UPB_MODE_ARRAY},
  {3, UPB_SIZE(12, 24), 0, 0, 11, _UPB_MODE_ARRAY},
};

const upb_msglayout envoy_admin_v3_ListenersConfigDump_msginit = {
  &envoy_admin_v3_ListenersConfigDump_submsgs[0],
  &envoy_admin_v3_ListenersConfigDump__fields[0],
  UPB_SIZE(16, 32), 3, false, 3, 255,
};

static const upb_msglayout *const envoy_admin_v3_ListenersConfigDump_StaticListener_submsgs[2] = {
  &google_protobuf_Any_msginit,
  &google_protobuf_Timestamp_msginit,
};

static const upb_msglayout_field envoy_admin_v3_ListenersConfigDump_StaticListener__fields[2] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(8, 16), 2, 1, 11, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_admin_v3_ListenersConfigDump_StaticListener_msginit = {
  &envoy_admin_v3_ListenersConfigDump_StaticListener_submsgs[0],
  &envoy_admin_v3_ListenersConfigDump_StaticListener__fields[0],
  UPB_SIZE(16, 24), 2, false, 2, 255,
};

static const upb_msglayout *const envoy_admin_v3_ListenersConfigDump_DynamicListenerState_submsgs[2] = {
  &google_protobuf_Any_msginit,
  &google_protobuf_Timestamp_msginit,
};

static const upb_msglayout_field envoy_admin_v3_ListenersConfigDump_DynamicListenerState__fields[3] = {
  {1, UPB_SIZE(4, 8), 0, 0, 9, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(12, 24), 1, 0, 11, _UPB_MODE_SCALAR},
  {3, UPB_SIZE(16, 32), 2, 1, 11, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_admin_v3_ListenersConfigDump_DynamicListenerState_msginit = {
  &envoy_admin_v3_ListenersConfigDump_DynamicListenerState_submsgs[0],
  &envoy_admin_v3_ListenersConfigDump_DynamicListenerState__fields[0],
  UPB_SIZE(24, 48), 3, false, 3, 255,
};

static const upb_msglayout *const envoy_admin_v3_ListenersConfigDump_DynamicListener_submsgs[2] = {
  &envoy_admin_v3_ListenersConfigDump_DynamicListenerState_msginit,
  &envoy_admin_v3_UpdateFailureState_msginit,
};

static const upb_msglayout_field envoy_admin_v3_ListenersConfigDump_DynamicListener__fields[6] = {
  {1, UPB_SIZE(8, 8), 0, 0, 9, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(16, 24), 1, 0, 11, _UPB_MODE_SCALAR},
  {3, UPB_SIZE(20, 32), 2, 0, 11, _UPB_MODE_SCALAR},
  {4, UPB_SIZE(24, 40), 3, 0, 11, _UPB_MODE_SCALAR},
  {5, UPB_SIZE(28, 48), 4, 1, 11, _UPB_MODE_SCALAR},
  {6, UPB_SIZE(4, 4), 0, 0, 14, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_admin_v3_ListenersConfigDump_DynamicListener_msginit = {
  &envoy_admin_v3_ListenersConfigDump_DynamicListener_submsgs[0],
  &envoy_admin_v3_ListenersConfigDump_DynamicListener__fields[0],
  UPB_SIZE(32, 64), 6, false, 6, 255,
};

static const upb_msglayout *const envoy_admin_v3_ClustersConfigDump_submsgs[2] = {
  &envoy_admin_v3_ClustersConfigDump_DynamicCluster_msginit,
  &envoy_admin_v3_ClustersConfigDump_StaticCluster_msginit,
};

static const upb_msglayout_field envoy_admin_v3_ClustersConfigDump__fields[4] = {
  {1, UPB_SIZE(0, 0), 0, 0, 9, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(8, 16), 0, 1, 11, _UPB_MODE_ARRAY},
  {3, UPB_SIZE(12, 24), 0, 0, 11, _UPB_MODE_ARRAY},
  {4, UPB_SIZE(16, 32), 0, 0, 11, _UPB_MODE_ARRAY},
};

const upb_msglayout envoy_admin_v3_ClustersConfigDump_msginit = {
  &envoy_admin_v3_ClustersConfigDump_submsgs[0],
  &envoy_admin_v3_ClustersConfigDump__fields[0],
  UPB_SIZE(24, 48), 4, false, 4, 255,
};

static const upb_msglayout *const envoy_admin_v3_ClustersConfigDump_StaticCluster_submsgs[2] = {
  &google_protobuf_Any_msginit,
  &google_protobuf_Timestamp_msginit,
};

static const upb_msglayout_field envoy_admin_v3_ClustersConfigDump_StaticCluster__fields[2] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(8, 16), 2, 1, 11, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_admin_v3_ClustersConfigDump_StaticCluster_msginit = {
  &envoy_admin_v3_ClustersConfigDump_StaticCluster_submsgs[0],
  &envoy_admin_v3_ClustersConfigDump_StaticCluster__fields[0],
  UPB_SIZE(16, 24), 2, false, 2, 255,
};

static const upb_msglayout *const envoy_admin_v3_ClustersConfigDump_DynamicCluster_submsgs[3] = {
  &envoy_admin_v3_UpdateFailureState_msginit,
  &google_protobuf_Any_msginit,
  &google_protobuf_Timestamp_msginit,
};

static const upb_msglayout_field envoy_admin_v3_ClustersConfigDump_DynamicCluster__fields[5] = {
  {1, UPB_SIZE(8, 8), 0, 0, 9, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(16, 24), 1, 1, 11, _UPB_MODE_SCALAR},
  {3, UPB_SIZE(20, 32), 2, 2, 11, _UPB_MODE_SCALAR},
  {4, UPB_SIZE(24, 40), 3, 0, 11, _UPB_MODE_SCALAR},
  {5, UPB_SIZE(4, 4), 0, 0, 14, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_admin_v3_ClustersConfigDump_DynamicCluster_msginit = {
  &envoy_admin_v3_ClustersConfigDump_DynamicCluster_submsgs[0],
  &envoy_admin_v3_ClustersConfigDump_DynamicCluster__fields[0],
  UPB_SIZE(32, 48), 5, false, 5, 255,
};

static const upb_msglayout *const envoy_admin_v3_RoutesConfigDump_submsgs[2] = {
  &envoy_admin_v3_RoutesConfigDump_DynamicRouteConfig_msginit,
  &envoy_admin_v3_RoutesConfigDump_StaticRouteConfig_msginit,
};

static const upb_msglayout_field envoy_admin_v3_RoutesConfigDump__fields[2] = {
  {2, UPB_SIZE(0, 0), 0, 1, 11, _UPB_MODE_ARRAY},
  {3, UPB_SIZE(4, 8), 0, 0, 11, _UPB_MODE_ARRAY},
};

const upb_msglayout envoy_admin_v3_RoutesConfigDump_msginit = {
  &envoy_admin_v3_RoutesConfigDump_submsgs[0],
  &envoy_admin_v3_RoutesConfigDump__fields[0],
  UPB_SIZE(8, 16), 2, false, 0, 255,
};

static const upb_msglayout *const envoy_admin_v3_RoutesConfigDump_StaticRouteConfig_submsgs[2] = {
  &google_protobuf_Any_msginit,
  &google_protobuf_Timestamp_msginit,
};

static const upb_msglayout_field envoy_admin_v3_RoutesConfigDump_StaticRouteConfig__fields[2] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(8, 16), 2, 1, 11, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_admin_v3_RoutesConfigDump_StaticRouteConfig_msginit = {
  &envoy_admin_v3_RoutesConfigDump_StaticRouteConfig_submsgs[0],
  &envoy_admin_v3_RoutesConfigDump_StaticRouteConfig__fields[0],
  UPB_SIZE(16, 24), 2, false, 2, 255,
};

static const upb_msglayout *const envoy_admin_v3_RoutesConfigDump_DynamicRouteConfig_submsgs[3] = {
  &envoy_admin_v3_UpdateFailureState_msginit,
  &google_protobuf_Any_msginit,
  &google_protobuf_Timestamp_msginit,
};

static const upb_msglayout_field envoy_admin_v3_RoutesConfigDump_DynamicRouteConfig__fields[5] = {
  {1, UPB_SIZE(8, 8), 0, 0, 9, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(16, 24), 1, 1, 11, _UPB_MODE_SCALAR},
  {3, UPB_SIZE(20, 32), 2, 2, 11, _UPB_MODE_SCALAR},
  {4, UPB_SIZE(24, 40), 3, 0, 11, _UPB_MODE_SCALAR},
  {5, UPB_SIZE(4, 4), 0, 0, 14, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_admin_v3_RoutesConfigDump_DynamicRouteConfig_msginit = {
  &envoy_admin_v3_RoutesConfigDump_DynamicRouteConfig_submsgs[0],
  &envoy_admin_v3_RoutesConfigDump_DynamicRouteConfig__fields[0],
  UPB_SIZE(32, 48), 5, false, 5, 255,
};

static const upb_msglayout *const envoy_admin_v3_ScopedRoutesConfigDump_submsgs[2] = {
  &envoy_admin_v3_ScopedRoutesConfigDump_DynamicScopedRouteConfigs_msginit,
  &envoy_admin_v3_ScopedRoutesConfigDump_InlineScopedRouteConfigs_msginit,
};

static const upb_msglayout_field envoy_admin_v3_ScopedRoutesConfigDump__fields[2] = {
  {1, UPB_SIZE(0, 0), 0, 1, 11, _UPB_MODE_ARRAY},
  {2, UPB_SIZE(4, 8), 0, 0, 11, _UPB_MODE_ARRAY},
};

const upb_msglayout envoy_admin_v3_ScopedRoutesConfigDump_msginit = {
  &envoy_admin_v3_ScopedRoutesConfigDump_submsgs[0],
  &envoy_admin_v3_ScopedRoutesConfigDump__fields[0],
  UPB_SIZE(8, 16), 2, false, 2, 255,
};

static const upb_msglayout *const envoy_admin_v3_ScopedRoutesConfigDump_InlineScopedRouteConfigs_submsgs[2] = {
  &google_protobuf_Any_msginit,
  &google_protobuf_Timestamp_msginit,
};

static const upb_msglayout_field envoy_admin_v3_ScopedRoutesConfigDump_InlineScopedRouteConfigs__fields[3] = {
  {1, UPB_SIZE(4, 8), 0, 0, 9, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(16, 32), 0, 0, 11, _UPB_MODE_ARRAY},
  {3, UPB_SIZE(12, 24), 1, 1, 11, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_admin_v3_ScopedRoutesConfigDump_InlineScopedRouteConfigs_msginit = {
  &envoy_admin_v3_ScopedRoutesConfigDump_InlineScopedRouteConfigs_submsgs[0],
  &envoy_admin_v3_ScopedRoutesConfigDump_InlineScopedRouteConfigs__fields[0],
  UPB_SIZE(24, 48), 3, false, 3, 255,
};

static const upb_msglayout *const envoy_admin_v3_ScopedRoutesConfigDump_DynamicScopedRouteConfigs_submsgs[3] = {
  &envoy_admin_v3_UpdateFailureState_msginit,
  &google_protobuf_Any_msginit,
  &google_protobuf_Timestamp_msginit,
};

static const upb_msglayout_field envoy_admin_v3_ScopedRoutesConfigDump_DynamicScopedRouteConfigs__fields[6] = {
  {1, UPB_SIZE(8, 8), 0, 0, 9, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(16, 24), 0, 0, 9, _UPB_MODE_SCALAR},
  {3, UPB_SIZE(32, 56), 0, 1, 11, _UPB_MODE_ARRAY},
  {4, UPB_SIZE(24, 40), 1, 2, 11, _UPB_MODE_SCALAR},
  {5, UPB_SIZE(28, 48), 2, 0, 11, _UPB_MODE_SCALAR},
  {6, UPB_SIZE(4, 4), 0, 0, 14, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_admin_v3_ScopedRoutesConfigDump_DynamicScopedRouteConfigs_msginit = {
  &envoy_admin_v3_ScopedRoutesConfigDump_DynamicScopedRouteConfigs_submsgs[0],
  &envoy_admin_v3_ScopedRoutesConfigDump_DynamicScopedRouteConfigs__fields[0],
  UPB_SIZE(40, 64), 6, false, 6, 255,
};

static const upb_msglayout *const envoy_admin_v3_SecretsConfigDump_submsgs[2] = {
  &envoy_admin_v3_SecretsConfigDump_DynamicSecret_msginit,
  &envoy_admin_v3_SecretsConfigDump_StaticSecret_msginit,
};

static const upb_msglayout_field envoy_admin_v3_SecretsConfigDump__fields[3] = {
  {1, UPB_SIZE(0, 0), 0, 1, 11, _UPB_MODE_ARRAY},
  {2, UPB_SIZE(4, 8), 0, 0, 11, _UPB_MODE_ARRAY},
  {3, UPB_SIZE(8, 16), 0, 0, 11, _UPB_MODE_ARRAY},
};

const upb_msglayout envoy_admin_v3_SecretsConfigDump_msginit = {
  &envoy_admin_v3_SecretsConfigDump_submsgs[0],
  &envoy_admin_v3_SecretsConfigDump__fields[0],
  UPB_SIZE(16, 24), 3, false, 3, 255,
};

static const upb_msglayout *const envoy_admin_v3_SecretsConfigDump_DynamicSecret_submsgs[3] = {
  &envoy_admin_v3_UpdateFailureState_msginit,
  &google_protobuf_Any_msginit,
  &google_protobuf_Timestamp_msginit,
};

static const upb_msglayout_field envoy_admin_v3_SecretsConfigDump_DynamicSecret__fields[6] = {
  {1, UPB_SIZE(8, 8), 0, 0, 9, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(16, 24), 0, 0, 9, _UPB_MODE_SCALAR},
  {3, UPB_SIZE(24, 40), 1, 2, 11, _UPB_MODE_SCALAR},
  {4, UPB_SIZE(28, 48), 2, 1, 11, _UPB_MODE_SCALAR},
  {5, UPB_SIZE(32, 56), 3, 0, 11, _UPB_MODE_SCALAR},
  {6, UPB_SIZE(4, 4), 0, 0, 14, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_admin_v3_SecretsConfigDump_DynamicSecret_msginit = {
  &envoy_admin_v3_SecretsConfigDump_DynamicSecret_submsgs[0],
  &envoy_admin_v3_SecretsConfigDump_DynamicSecret__fields[0],
  UPB_SIZE(40, 64), 6, false, 6, 255,
};

static const upb_msglayout *const envoy_admin_v3_SecretsConfigDump_StaticSecret_submsgs[2] = {
  &google_protobuf_Any_msginit,
  &google_protobuf_Timestamp_msginit,
};

static const upb_msglayout_field envoy_admin_v3_SecretsConfigDump_StaticSecret__fields[3] = {
  {1, UPB_SIZE(4, 8), 0, 0, 9, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(12, 24), 1, 1, 11, _UPB_MODE_SCALAR},
  {3, UPB_SIZE(16, 32), 2, 0, 11, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_admin_v3_SecretsConfigDump_StaticSecret_msginit = {
  &envoy_admin_v3_SecretsConfigDump_StaticSecret_submsgs[0],
  &envoy_admin_v3_SecretsConfigDump_StaticSecret__fields[0],
  UPB_SIZE(24, 48), 3, false, 3, 255,
};

static const upb_msglayout *const envoy_admin_v3_EndpointsConfigDump_submsgs[2] = {
  &envoy_admin_v3_EndpointsConfigDump_DynamicEndpointConfig_msginit,
  &envoy_admin_v3_EndpointsConfigDump_StaticEndpointConfig_msginit,
};

static const upb_msglayout_field envoy_admin_v3_EndpointsConfigDump__fields[2] = {
  {2, UPB_SIZE(0, 0), 0, 1, 11, _UPB_MODE_ARRAY},
  {3, UPB_SIZE(4, 8), 0, 0, 11, _UPB_MODE_ARRAY},
};

const upb_msglayout envoy_admin_v3_EndpointsConfigDump_msginit = {
  &envoy_admin_v3_EndpointsConfigDump_submsgs[0],
  &envoy_admin_v3_EndpointsConfigDump__fields[0],
  UPB_SIZE(8, 16), 2, false, 0, 255,
};

static const upb_msglayout *const envoy_admin_v3_EndpointsConfigDump_StaticEndpointConfig_submsgs[2] = {
  &google_protobuf_Any_msginit,
  &google_protobuf_Timestamp_msginit,
};

static const upb_msglayout_field envoy_admin_v3_EndpointsConfigDump_StaticEndpointConfig__fields[2] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(8, 16), 2, 1, 11, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_admin_v3_EndpointsConfigDump_StaticEndpointConfig_msginit = {
  &envoy_admin_v3_EndpointsConfigDump_StaticEndpointConfig_submsgs[0],
  &envoy_admin_v3_EndpointsConfigDump_StaticEndpointConfig__fields[0],
  UPB_SIZE(16, 24), 2, false, 2, 255,
};

static const upb_msglayout *const envoy_admin_v3_EndpointsConfigDump_DynamicEndpointConfig_submsgs[3] = {
  &envoy_admin_v3_UpdateFailureState_msginit,
  &google_protobuf_Any_msginit,
  &google_protobuf_Timestamp_msginit,
};

static const upb_msglayout_field envoy_admin_v3_EndpointsConfigDump_DynamicEndpointConfig__fields[5] = {
  {1, UPB_SIZE(8, 8), 0, 0, 9, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(16, 24), 1, 1, 11, _UPB_MODE_SCALAR},
  {3, UPB_SIZE(20, 32), 2, 2, 11, _UPB_MODE_SCALAR},
  {4, UPB_SIZE(24, 40), 3, 0, 11, _UPB_MODE_SCALAR},
  {5, UPB_SIZE(4, 4), 0, 0, 14, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_admin_v3_EndpointsConfigDump_DynamicEndpointConfig_msginit = {
  &envoy_admin_v3_EndpointsConfigDump_DynamicEndpointConfig_submsgs[0],
  &envoy_admin_v3_EndpointsConfigDump_DynamicEndpointConfig__fields[0],
  UPB_SIZE(32, 48), 5, false, 5, 255,
};

#include "upb/port_undef.inc"

