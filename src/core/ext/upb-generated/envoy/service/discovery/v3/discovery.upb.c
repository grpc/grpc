/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/service/discovery/v3/discovery.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg.h"
#include "envoy/service/discovery/v3/discovery.upb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/rpc/status.upb.h"
#include "udpa/core/v1/resource_locator.upb.h"
#include "udpa/core/v1/resource_name.upb.h"
#include "udpa/annotations/migrate.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"

#include "upb/port_def.inc"

static const upb_msglayout *const envoy_service_discovery_v3_DiscoveryRequest_submsgs[2] = {
  &envoy_config_core_v3_Node_msginit,
  &google_rpc_Status_msginit,
};

static const upb_msglayout_field envoy_service_discovery_v3_DiscoveryRequest__fields[6] = {
  {1, UPB_SIZE(4, 8), 0, 0, 9, 1},
  {2, UPB_SIZE(28, 56), 1, 0, 11, 1},
  {3, UPB_SIZE(36, 72), 0, 0, 9, 3},
  {4, UPB_SIZE(12, 24), 0, 0, 9, 1},
  {5, UPB_SIZE(20, 40), 0, 0, 9, 1},
  {6, UPB_SIZE(32, 64), 2, 1, 11, 1},
};

const upb_msglayout envoy_service_discovery_v3_DiscoveryRequest_msginit = {
  &envoy_service_discovery_v3_DiscoveryRequest_submsgs[0],
  &envoy_service_discovery_v3_DiscoveryRequest__fields[0],
  UPB_SIZE(40, 80), 6, false, 255,
};

static const upb_msglayout *const envoy_service_discovery_v3_DiscoveryResponse_submsgs[2] = {
  &envoy_config_core_v3_ControlPlane_msginit,
  &google_protobuf_Any_msginit,
};

static const upb_msglayout_field envoy_service_discovery_v3_DiscoveryResponse__fields[6] = {
  {1, UPB_SIZE(4, 8), 0, 0, 9, 1},
  {2, UPB_SIZE(32, 64), 0, 1, 11, 3},
  {3, UPB_SIZE(1, 1), 0, 0, 8, 1},
  {4, UPB_SIZE(12, 24), 0, 0, 9, 1},
  {5, UPB_SIZE(20, 40), 0, 0, 9, 1},
  {6, UPB_SIZE(28, 56), 1, 0, 11, 1},
};

const upb_msglayout envoy_service_discovery_v3_DiscoveryResponse_msginit = {
  &envoy_service_discovery_v3_DiscoveryResponse_submsgs[0],
  &envoy_service_discovery_v3_DiscoveryResponse__fields[0],
  UPB_SIZE(40, 80), 6, false, 255,
};

static const upb_msglayout *const envoy_service_discovery_v3_DeltaDiscoveryRequest_submsgs[4] = {
  &envoy_config_core_v3_Node_msginit,
  &envoy_service_discovery_v3_DeltaDiscoveryRequest_InitialResourceVersionsEntry_msginit,
  &google_rpc_Status_msginit,
  &udpa_core_v1_ResourceLocator_msginit,
};

static const upb_msglayout_field envoy_service_discovery_v3_DeltaDiscoveryRequest__fields[9] = {
  {1, UPB_SIZE(20, 40), 1, 0, 11, 1},
  {2, UPB_SIZE(4, 8), 0, 0, 9, 1},
  {3, UPB_SIZE(28, 56), 0, 0, 9, 3},
  {4, UPB_SIZE(32, 64), 0, 0, 9, 3},
  {5, UPB_SIZE(36, 72), 0, 1, 11, _UPB_LABEL_MAP},
  {6, UPB_SIZE(12, 24), 0, 0, 9, 1},
  {7, UPB_SIZE(24, 48), 2, 2, 11, 1},
  {8, UPB_SIZE(40, 80), 0, 3, 11, 3},
  {9, UPB_SIZE(44, 88), 0, 3, 11, 3},
};

const upb_msglayout envoy_service_discovery_v3_DeltaDiscoveryRequest_msginit = {
  &envoy_service_discovery_v3_DeltaDiscoveryRequest_submsgs[0],
  &envoy_service_discovery_v3_DeltaDiscoveryRequest__fields[0],
  UPB_SIZE(48, 96), 9, false, 255,
};

static const upb_msglayout_field envoy_service_discovery_v3_DeltaDiscoveryRequest_InitialResourceVersionsEntry__fields[2] = {
  {1, UPB_SIZE(0, 0), 0, 0, 9, 1},
  {2, UPB_SIZE(8, 16), 0, 0, 9, 1},
};

const upb_msglayout envoy_service_discovery_v3_DeltaDiscoveryRequest_InitialResourceVersionsEntry_msginit = {
  NULL,
  &envoy_service_discovery_v3_DeltaDiscoveryRequest_InitialResourceVersionsEntry__fields[0],
  UPB_SIZE(16, 32), 2, false, 255,
};

static const upb_msglayout *const envoy_service_discovery_v3_DeltaDiscoveryResponse_submsgs[2] = {
  &envoy_service_discovery_v3_Resource_msginit,
  &udpa_core_v1_ResourceName_msginit,
};

static const upb_msglayout_field envoy_service_discovery_v3_DeltaDiscoveryResponse__fields[6] = {
  {1, UPB_SIZE(0, 0), 0, 0, 9, 1},
  {2, UPB_SIZE(24, 48), 0, 0, 11, 3},
  {4, UPB_SIZE(8, 16), 0, 0, 9, 1},
  {5, UPB_SIZE(16, 32), 0, 0, 9, 1},
  {6, UPB_SIZE(28, 56), 0, 0, 9, 3},
  {7, UPB_SIZE(32, 64), 0, 1, 11, 3},
};

const upb_msglayout envoy_service_discovery_v3_DeltaDiscoveryResponse_msginit = {
  &envoy_service_discovery_v3_DeltaDiscoveryResponse_submsgs[0],
  &envoy_service_discovery_v3_DeltaDiscoveryResponse__fields[0],
  UPB_SIZE(40, 80), 6, false, 255,
};

static const upb_msglayout *const envoy_service_discovery_v3_Resource_submsgs[2] = {
  &google_protobuf_Any_msginit,
  &udpa_core_v1_ResourceName_msginit,
};

static const upb_msglayout_field envoy_service_discovery_v3_Resource__fields[5] = {
  {1, UPB_SIZE(4, 8), 0, 0, 9, 1},
  {2, UPB_SIZE(20, 40), 1, 0, 11, 1},
  {3, UPB_SIZE(12, 24), 0, 0, 9, 1},
  {4, UPB_SIZE(28, 56), 0, 0, 9, 3},
  {5, UPB_SIZE(24, 48), 2, 1, 11, 1},
};

const upb_msglayout envoy_service_discovery_v3_Resource_msginit = {
  &envoy_service_discovery_v3_Resource_submsgs[0],
  &envoy_service_discovery_v3_Resource__fields[0],
  UPB_SIZE(32, 64), 5, false, 255,
};

#include "upb/port_undef.inc"

