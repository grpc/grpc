/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/extensions/transport_sockets/tls/v3/tls_spiffe_validator_config.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_EXTENSIONS_TRANSPORT_SOCKETS_TLS_V3_TLS_SPIFFE_VALIDATOR_CONFIG_PROTO_UPB_H_
#define ENVOY_EXTENSIONS_TRANSPORT_SOCKETS_TLS_V3_TLS_SPIFFE_VALIDATOR_CONFIG_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig;
struct envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain;
typedef struct envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig;
typedef struct envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain;
extern const upb_MiniTable envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_msginit;
extern const upb_MiniTable envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_msginit;
struct envoy_config_core_v3_DataSource;
extern const upb_MiniTable envoy_config_core_v3_DataSource_msginit;



/* envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig */

UPB_INLINE envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig* envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_new(upb_Arena* arena) {
  return (envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig*)_upb_Message_New(&envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_msginit, arena);
}
UPB_INLINE envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig* envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig* ret = envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig* envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig* ret = envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_serialize(const envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig* msg, upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_msginit, 0, arena, len);
}
UPB_INLINE char* envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_serialize_ex(const envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_msginit, options, arena, len);
}
UPB_INLINE bool envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_has_trust_domains(const envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig *msg) { return _upb_has_submsg_nohasbit(msg, UPB_SIZE(0, 0)); }
UPB_INLINE const envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain* const* envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_trust_domains(const envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig *msg, size_t *len) { return (const envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain* const*)_upb_array_accessor(msg, UPB_SIZE(0, 0), len); }

UPB_INLINE envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain** envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_mutable_trust_domains(envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig *msg, size_t *len) {
  return (envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain**)_upb_array_mutable_accessor(msg, UPB_SIZE(0, 0), len);
}
UPB_INLINE envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain** envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_resize_trust_domains(envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig *msg, size_t len, upb_Arena *arena) {
  return (envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain**)_upb_Array_Resize_accessor2(msg, UPB_SIZE(0, 0), len, UPB_SIZE(2, 3), arena);
}
UPB_INLINE struct envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain* envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_add_trust_domains(envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig *msg, upb_Arena *arena) {
  struct envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain* sub = (struct envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain*)_upb_Message_New(&envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_msginit, arena);
  bool ok = _upb_Array_Append_accessor2(
      msg, UPB_SIZE(0, 0), UPB_SIZE(2, 3), &sub, arena);
  if (!ok) return NULL;
  return sub;
}

/* envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig.TrustDomain */

UPB_INLINE envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain* envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_new(upb_Arena* arena) {
  return (envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain*)_upb_Message_New(&envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_msginit, arena);
}
UPB_INLINE envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain* envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain* ret = envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain* envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain* ret = envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_serialize(const envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain* msg, upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_msginit, 0, arena, len);
}
UPB_INLINE char* envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_serialize_ex(const envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_msginit, options, arena, len);
}
UPB_INLINE upb_StringView envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_name(const envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_StringView);
}
UPB_INLINE bool envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_has_trust_bundle(const envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain *msg) { return _upb_hasbit(msg, 1); }
UPB_INLINE const struct envoy_config_core_v3_DataSource* envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_trust_bundle(const envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(12, 24), const struct envoy_config_core_v3_DataSource*);
}

UPB_INLINE void envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_set_name(envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_StringView) = value;
}
UPB_INLINE void envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_set_trust_bundle(envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain *msg, struct envoy_config_core_v3_DataSource* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(12, 24), struct envoy_config_core_v3_DataSource*) = value;
}
UPB_INLINE struct envoy_config_core_v3_DataSource* envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_mutable_trust_bundle(envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain *msg, upb_Arena *arena) {
  struct envoy_config_core_v3_DataSource* sub = (struct envoy_config_core_v3_DataSource*)envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_trust_bundle(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_DataSource*)_upb_Message_New(&envoy_config_core_v3_DataSource_msginit, arena);
    if (!sub) return NULL;
    envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_set_trust_bundle(msg, sub);
  }
  return sub;
}

extern const upb_MiniTable_File envoy_extensions_transport_sockets_tls_v3_tls_spiffe_validator_config_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_EXTENSIONS_TRANSPORT_SOCKETS_TLS_V3_TLS_SPIFFE_VALIDATOR_CONFIG_PROTO_UPB_H_ */
