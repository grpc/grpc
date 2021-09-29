/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/extensions/transport_sockets/tls/v3/common.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "envoy/extensions/transport_sockets/tls/v3/common.upb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/config/core/v3/extension.upb.h"
#include "envoy/type/matcher/v3/string.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "udpa/annotations/migrate.upb.h"
#include "udpa/annotations/sensitive.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

#include "upb/port_def.inc"

static const upb_msglayout_field envoy_extensions_transport_sockets_tls_v3_TlsParameters__fields[4] = {
  {1, UPB_SIZE(0, 0), 0, 0, 14, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(4, 4), 0, 0, 14, _UPB_MODE_SCALAR},
  {3, UPB_SIZE(8, 8), 0, 0, 9, _UPB_MODE_ARRAY},
  {4, UPB_SIZE(12, 16), 0, 0, 9, _UPB_MODE_ARRAY},
};

const upb_msglayout envoy_extensions_transport_sockets_tls_v3_TlsParameters_msginit = {
  NULL,
  &envoy_extensions_transport_sockets_tls_v3_TlsParameters__fields[0],
  UPB_SIZE(16, 24), 4, false, 4, 255,
};

static const upb_msglayout *const envoy_extensions_transport_sockets_tls_v3_PrivateKeyProvider_submsgs[1] = {
  &google_protobuf_Any_msginit,
};

static const upb_msglayout_field envoy_extensions_transport_sockets_tls_v3_PrivateKeyProvider__fields[2] = {
  {1, UPB_SIZE(0, 0), 0, 0, 9, _UPB_MODE_SCALAR},
  {3, UPB_SIZE(8, 16), UPB_SIZE(-13, -25), 0, 11, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_extensions_transport_sockets_tls_v3_PrivateKeyProvider_msginit = {
  &envoy_extensions_transport_sockets_tls_v3_PrivateKeyProvider_submsgs[0],
  &envoy_extensions_transport_sockets_tls_v3_PrivateKeyProvider__fields[0],
  UPB_SIZE(16, 32), 2, false, 1, 255,
};

static const upb_msglayout *const envoy_extensions_transport_sockets_tls_v3_TlsCertificate_submsgs[3] = {
  &envoy_config_core_v3_DataSource_msginit,
  &envoy_config_core_v3_WatchedDirectory_msginit,
  &envoy_extensions_transport_sockets_tls_v3_PrivateKeyProvider_msginit,
};

static const upb_msglayout_field envoy_extensions_transport_sockets_tls_v3_TlsCertificate__fields[7] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(8, 16), 2, 0, 11, _UPB_MODE_SCALAR},
  {3, UPB_SIZE(12, 24), 3, 0, 11, _UPB_MODE_SCALAR},
  {4, UPB_SIZE(16, 32), 4, 0, 11, _UPB_MODE_SCALAR},
  {5, UPB_SIZE(28, 56), 0, 0, 11, _UPB_MODE_ARRAY},
  {6, UPB_SIZE(20, 40), 5, 2, 11, _UPB_MODE_SCALAR},
  {7, UPB_SIZE(24, 48), 6, 1, 11, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_extensions_transport_sockets_tls_v3_TlsCertificate_msginit = {
  &envoy_extensions_transport_sockets_tls_v3_TlsCertificate_submsgs[0],
  &envoy_extensions_transport_sockets_tls_v3_TlsCertificate__fields[0],
  UPB_SIZE(32, 64), 7, false, 7, 255,
};

static const upb_msglayout *const envoy_extensions_transport_sockets_tls_v3_TlsSessionTicketKeys_submsgs[1] = {
  &envoy_config_core_v3_DataSource_msginit,
};

static const upb_msglayout_field envoy_extensions_transport_sockets_tls_v3_TlsSessionTicketKeys__fields[1] = {
  {1, UPB_SIZE(0, 0), 0, 0, 11, _UPB_MODE_ARRAY},
};

const upb_msglayout envoy_extensions_transport_sockets_tls_v3_TlsSessionTicketKeys_msginit = {
  &envoy_extensions_transport_sockets_tls_v3_TlsSessionTicketKeys_submsgs[0],
  &envoy_extensions_transport_sockets_tls_v3_TlsSessionTicketKeys__fields[0],
  UPB_SIZE(8, 8), 1, false, 1, 255,
};

static const upb_msglayout_field envoy_extensions_transport_sockets_tls_v3_CertificateProviderPluginInstance__fields[2] = {
  {1, UPB_SIZE(0, 0), 0, 0, 9, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(8, 16), 0, 0, 9, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_extensions_transport_sockets_tls_v3_CertificateProviderPluginInstance_msginit = {
  NULL,
  &envoy_extensions_transport_sockets_tls_v3_CertificateProviderPluginInstance__fields[0],
  UPB_SIZE(16, 32), 2, false, 2, 255,
};

static const upb_msglayout *const envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_submsgs[6] = {
  &envoy_config_core_v3_DataSource_msginit,
  &envoy_config_core_v3_TypedExtensionConfig_msginit,
  &envoy_config_core_v3_WatchedDirectory_msginit,
  &envoy_extensions_transport_sockets_tls_v3_CertificateProviderPluginInstance_msginit,
  &envoy_type_matcher_v3_StringMatcher_msginit,
  &google_protobuf_BoolValue_msginit,
};

static const upb_msglayout_field envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext__fields[11] = {
  {1, UPB_SIZE(12, 16), 1, 0, 11, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(36, 64), 0, 0, 9, _UPB_MODE_ARRAY},
  {3, UPB_SIZE(40, 72), 0, 0, 9, _UPB_MODE_ARRAY},
  {6, UPB_SIZE(16, 24), 2, 5, 11, _UPB_MODE_SCALAR},
  {7, UPB_SIZE(20, 32), 3, 0, 11, _UPB_MODE_SCALAR},
  {8, UPB_SIZE(8, 8), 0, 0, 8, _UPB_MODE_SCALAR},
  {9, UPB_SIZE(44, 80), 0, 4, 11, _UPB_MODE_ARRAY},
  {10, UPB_SIZE(4, 4), 0, 0, 14, _UPB_MODE_SCALAR},
  {11, UPB_SIZE(24, 40), 4, 2, 11, _UPB_MODE_SCALAR},
  {12, UPB_SIZE(28, 48), 5, 1, 11, _UPB_MODE_SCALAR},
  {13, UPB_SIZE(32, 56), 6, 3, 11, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_msginit = {
  &envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_submsgs[0],
  &envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext__fields[0],
  UPB_SIZE(48, 88), 11, false, 3, 255,
};

#include "upb/port_undef.inc"

