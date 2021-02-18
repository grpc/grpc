/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/overload/v3/overload.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg.h"
#include "envoy/config/overload/v3/overload.upb.h"
#include "envoy/type/v3/percent.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/duration.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

#include "upb/port_def.inc"

static const upb_msglayout *const envoy_config_overload_v3_ResourceMonitor_submsgs[1] = {
  &google_protobuf_Any_msginit,
};

static const upb_msglayout_field envoy_config_overload_v3_ResourceMonitor__fields[2] = {
  {1, UPB_SIZE(0, 0), 0, 0, 9, 1},
  {3, UPB_SIZE(8, 16), UPB_SIZE(-13, -25), 0, 11, 1},
};

const upb_msglayout envoy_config_overload_v3_ResourceMonitor_msginit = {
  &envoy_config_overload_v3_ResourceMonitor_submsgs[0],
  &envoy_config_overload_v3_ResourceMonitor__fields[0],
  UPB_SIZE(16, 32), 2, false, 255,
};

static const upb_msglayout_field envoy_config_overload_v3_ThresholdTrigger__fields[1] = {
  {1, UPB_SIZE(0, 0), 0, 0, 1, 1},
};

const upb_msglayout envoy_config_overload_v3_ThresholdTrigger_msginit = {
  NULL,
  &envoy_config_overload_v3_ThresholdTrigger__fields[0],
  UPB_SIZE(8, 8), 1, false, 255,
};

static const upb_msglayout_field envoy_config_overload_v3_ScaledTrigger__fields[2] = {
  {1, UPB_SIZE(0, 0), 0, 0, 1, 1},
  {2, UPB_SIZE(8, 8), 0, 0, 1, 1},
};

const upb_msglayout envoy_config_overload_v3_ScaledTrigger_msginit = {
  NULL,
  &envoy_config_overload_v3_ScaledTrigger__fields[0],
  UPB_SIZE(16, 16), 2, false, 255,
};

static const upb_msglayout *const envoy_config_overload_v3_Trigger_submsgs[2] = {
  &envoy_config_overload_v3_ScaledTrigger_msginit,
  &envoy_config_overload_v3_ThresholdTrigger_msginit,
};

static const upb_msglayout_field envoy_config_overload_v3_Trigger__fields[3] = {
  {1, UPB_SIZE(0, 0), 0, 0, 9, 1},
  {2, UPB_SIZE(8, 16), UPB_SIZE(-13, -25), 1, 11, 1},
  {3, UPB_SIZE(8, 16), UPB_SIZE(-13, -25), 0, 11, 1},
};

const upb_msglayout envoy_config_overload_v3_Trigger_msginit = {
  &envoy_config_overload_v3_Trigger_submsgs[0],
  &envoy_config_overload_v3_Trigger__fields[0],
  UPB_SIZE(16, 32), 3, false, 255,
};

static const upb_msglayout *const envoy_config_overload_v3_ScaleTimersOverloadActionConfig_submsgs[1] = {
  &envoy_config_overload_v3_ScaleTimersOverloadActionConfig_ScaleTimer_msginit,
};

static const upb_msglayout_field envoy_config_overload_v3_ScaleTimersOverloadActionConfig__fields[1] = {
  {1, UPB_SIZE(0, 0), 0, 0, 11, 3},
};

const upb_msglayout envoy_config_overload_v3_ScaleTimersOverloadActionConfig_msginit = {
  &envoy_config_overload_v3_ScaleTimersOverloadActionConfig_submsgs[0],
  &envoy_config_overload_v3_ScaleTimersOverloadActionConfig__fields[0],
  UPB_SIZE(8, 8), 1, false, 255,
};

static const upb_msglayout *const envoy_config_overload_v3_ScaleTimersOverloadActionConfig_ScaleTimer_submsgs[2] = {
  &envoy_type_v3_Percent_msginit,
  &google_protobuf_Duration_msginit,
};

static const upb_msglayout_field envoy_config_overload_v3_ScaleTimersOverloadActionConfig_ScaleTimer__fields[3] = {
  {1, UPB_SIZE(0, 0), 0, 0, 14, 1},
  {2, UPB_SIZE(4, 8), UPB_SIZE(-9, -17), 1, 11, 1},
  {3, UPB_SIZE(4, 8), UPB_SIZE(-9, -17), 0, 11, 1},
};

const upb_msglayout envoy_config_overload_v3_ScaleTimersOverloadActionConfig_ScaleTimer_msginit = {
  &envoy_config_overload_v3_ScaleTimersOverloadActionConfig_ScaleTimer_submsgs[0],
  &envoy_config_overload_v3_ScaleTimersOverloadActionConfig_ScaleTimer__fields[0],
  UPB_SIZE(16, 24), 3, false, 255,
};

static const upb_msglayout *const envoy_config_overload_v3_OverloadAction_submsgs[2] = {
  &envoy_config_overload_v3_Trigger_msginit,
  &google_protobuf_Any_msginit,
};

static const upb_msglayout_field envoy_config_overload_v3_OverloadAction__fields[3] = {
  {1, UPB_SIZE(4, 8), 0, 0, 9, 1},
  {2, UPB_SIZE(16, 32), 0, 0, 11, 3},
  {3, UPB_SIZE(12, 24), 1, 1, 11, 1},
};

const upb_msglayout envoy_config_overload_v3_OverloadAction_msginit = {
  &envoy_config_overload_v3_OverloadAction_submsgs[0],
  &envoy_config_overload_v3_OverloadAction__fields[0],
  UPB_SIZE(24, 48), 3, false, 255,
};

static const upb_msglayout *const envoy_config_overload_v3_OverloadManager_submsgs[3] = {
  &envoy_config_overload_v3_OverloadAction_msginit,
  &envoy_config_overload_v3_ResourceMonitor_msginit,
  &google_protobuf_Duration_msginit,
};

static const upb_msglayout_field envoy_config_overload_v3_OverloadManager__fields[3] = {
  {1, UPB_SIZE(4, 8), 1, 2, 11, 1},
  {2, UPB_SIZE(8, 16), 0, 1, 11, 3},
  {3, UPB_SIZE(12, 24), 0, 0, 11, 3},
};

const upb_msglayout envoy_config_overload_v3_OverloadManager_msginit = {
  &envoy_config_overload_v3_OverloadManager_submsgs[0],
  &envoy_config_overload_v3_OverloadManager__fields[0],
  UPB_SIZE(16, 32), 3, false, 255,
};

#include "upb/port_undef.inc"

