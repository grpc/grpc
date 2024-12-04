/* This file was generated by upb_generator from the input file:
 *
 *     envoy/config/rbac/v3/rbac.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated.
 * NO CHECKED-IN PROTOBUF GENCODE */

#ifndef ENVOY_CONFIG_RBAC_V3_RBAC_PROTO_UPB_H__UPBDEFS_H_
#define ENVOY_CONFIG_RBAC_V3_RBAC_PROTO_UPB_H__UPBDEFS_H_

#include "upb/reflection/def.h"
#include "upb/reflection/internal/def_pool.h"

#include "upb/port/def.inc" // Must be last.
#ifdef __cplusplus
extern "C" {
#endif

extern _upb_DefPool_Init envoy_config_rbac_v3_rbac_proto_upbdefinit;

UPB_INLINE const upb_MessageDef *envoy_config_rbac_v3_RBAC_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_config_rbac_v3_rbac_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.config.rbac.v3.RBAC");
}

UPB_INLINE const upb_MessageDef *envoy_config_rbac_v3_RBAC_AuditLoggingOptions_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_config_rbac_v3_rbac_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.config.rbac.v3.RBAC.AuditLoggingOptions");
}

UPB_INLINE const upb_MessageDef *envoy_config_rbac_v3_RBAC_AuditLoggingOptions_AuditLoggerConfig_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_config_rbac_v3_rbac_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.config.rbac.v3.RBAC.AuditLoggingOptions.AuditLoggerConfig");
}

UPB_INLINE const upb_MessageDef *envoy_config_rbac_v3_RBAC_PoliciesEntry_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_config_rbac_v3_rbac_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.config.rbac.v3.RBAC.PoliciesEntry");
}

UPB_INLINE const upb_MessageDef *envoy_config_rbac_v3_Policy_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_config_rbac_v3_rbac_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.config.rbac.v3.Policy");
}

UPB_INLINE const upb_MessageDef *envoy_config_rbac_v3_Permission_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_config_rbac_v3_rbac_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.config.rbac.v3.Permission");
}

UPB_INLINE const upb_MessageDef *envoy_config_rbac_v3_Permission_Set_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_config_rbac_v3_rbac_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.config.rbac.v3.Permission.Set");
}

UPB_INLINE const upb_MessageDef *envoy_config_rbac_v3_Principal_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_config_rbac_v3_rbac_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.config.rbac.v3.Principal");
}

UPB_INLINE const upb_MessageDef *envoy_config_rbac_v3_Principal_Set_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_config_rbac_v3_rbac_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.config.rbac.v3.Principal.Set");
}

UPB_INLINE const upb_MessageDef *envoy_config_rbac_v3_Principal_Authenticated_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_config_rbac_v3_rbac_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.config.rbac.v3.Principal.Authenticated");
}

UPB_INLINE const upb_MessageDef *envoy_config_rbac_v3_Action_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_config_rbac_v3_rbac_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.config.rbac.v3.Action");
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* ENVOY_CONFIG_RBAC_V3_RBAC_PROTO_UPB_H__UPBDEFS_H_ */
