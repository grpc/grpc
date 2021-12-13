/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/cluster/v3/circuit_breaker.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "envoy/config/cluster/v3/circuit_breaker.upb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/type/v3/percent.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

#include "upb/port_def.inc"

static const upb_msglayout_sub envoy_config_cluster_v3_CircuitBreakers_submsgs[1] = {
  {.submsg = &envoy_config_cluster_v3_CircuitBreakers_Thresholds_msginit},
};

static const upb_msglayout_field envoy_config_cluster_v3_CircuitBreakers__fields[1] = {
  {1, UPB_SIZE(0, 0), 0, 0, 11, _UPB_MODE_ARRAY | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
};

const upb_msglayout envoy_config_cluster_v3_CircuitBreakers_msginit = {
  &envoy_config_cluster_v3_CircuitBreakers_submsgs[0],
  &envoy_config_cluster_v3_CircuitBreakers__fields[0],
  UPB_SIZE(8, 8), 1, _UPB_MSGEXT_NONE, 1, 255,
};

static const upb_msglayout_sub envoy_config_cluster_v3_CircuitBreakers_Thresholds_submsgs[2] = {
  {.submsg = &envoy_config_cluster_v3_CircuitBreakers_Thresholds_RetryBudget_msginit},
  {.submsg = &google_protobuf_UInt32Value_msginit},
};

static const upb_msglayout_field envoy_config_cluster_v3_CircuitBreakers_Thresholds__fields[8] = {
  {1, UPB_SIZE(4, 4), 0, 0, 14, _UPB_MODE_SCALAR | (_UPB_REP_4BYTE << _UPB_REP_SHIFT)},
  {2, UPB_SIZE(12, 16), 1, 1, 11, _UPB_MODE_SCALAR | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
  {3, UPB_SIZE(16, 24), 2, 1, 11, _UPB_MODE_SCALAR | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
  {4, UPB_SIZE(20, 32), 3, 1, 11, _UPB_MODE_SCALAR | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
  {5, UPB_SIZE(24, 40), 4, 1, 11, _UPB_MODE_SCALAR | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
  {6, UPB_SIZE(8, 8), 0, 0, 8, _UPB_MODE_SCALAR | (_UPB_REP_1BYTE << _UPB_REP_SHIFT)},
  {7, UPB_SIZE(28, 48), 5, 1, 11, _UPB_MODE_SCALAR | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
  {8, UPB_SIZE(32, 56), 6, 0, 11, _UPB_MODE_SCALAR | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
};

const upb_msglayout envoy_config_cluster_v3_CircuitBreakers_Thresholds_msginit = {
  &envoy_config_cluster_v3_CircuitBreakers_Thresholds_submsgs[0],
  &envoy_config_cluster_v3_CircuitBreakers_Thresholds__fields[0],
  UPB_SIZE(40, 64), 8, _UPB_MSGEXT_NONE, 8, 255,
};

static const upb_msglayout_sub envoy_config_cluster_v3_CircuitBreakers_Thresholds_RetryBudget_submsgs[2] = {
  {.submsg = &envoy_type_v3_Percent_msginit},
  {.submsg = &google_protobuf_UInt32Value_msginit},
};

static const upb_msglayout_field envoy_config_cluster_v3_CircuitBreakers_Thresholds_RetryBudget__fields[2] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, _UPB_MODE_SCALAR | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
  {2, UPB_SIZE(8, 16), 2, 1, 11, _UPB_MODE_SCALAR | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
};

const upb_msglayout envoy_config_cluster_v3_CircuitBreakers_Thresholds_RetryBudget_msginit = {
  &envoy_config_cluster_v3_CircuitBreakers_Thresholds_RetryBudget_submsgs[0],
  &envoy_config_cluster_v3_CircuitBreakers_Thresholds_RetryBudget__fields[0],
  UPB_SIZE(16, 24), 2, _UPB_MSGEXT_NONE, 2, 255,
};

static const upb_msglayout *messages_layout[3] = {
  &envoy_config_cluster_v3_CircuitBreakers_msginit,
  &envoy_config_cluster_v3_CircuitBreakers_Thresholds_msginit,
  &envoy_config_cluster_v3_CircuitBreakers_Thresholds_RetryBudget_msginit,
};

const upb_msglayout_file envoy_config_cluster_v3_circuit_breaker_proto_upb_file_layout = {
  messages_layout,
  NULL,
  3,
  0,
};

#include "upb/port_undef.inc"

