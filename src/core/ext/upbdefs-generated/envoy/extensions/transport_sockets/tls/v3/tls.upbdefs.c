/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/extensions/transport_sockets/tls/v3/tls.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include "upb/def.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.upbdefs.h"

extern upb_def_init envoy_config_core_v3_extension_proto_upbdefinit;
extern upb_def_init envoy_extensions_transport_sockets_tls_v3_common_proto_upbdefinit;
extern upb_def_init envoy_extensions_transport_sockets_tls_v3_secret_proto_upbdefinit;
extern upb_def_init google_protobuf_duration_proto_upbdefinit;
extern upb_def_init google_protobuf_wrappers_proto_upbdefinit;
extern upb_def_init udpa_annotations_migrate_proto_upbdefinit;
extern upb_def_init udpa_annotations_status_proto_upbdefinit;
extern upb_def_init udpa_annotations_versioning_proto_upbdefinit;
extern upb_def_init validate_validate_proto_upbdefinit;
extern const upb_msglayout envoy_extensions_transport_sockets_tls_v3_UpstreamTlsContext_msginit;
extern const upb_msglayout envoy_extensions_transport_sockets_tls_v3_DownstreamTlsContext_msginit;
extern const upb_msglayout envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_msginit;
extern const upb_msglayout envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CertificateProvider_msginit;
extern const upb_msglayout envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CertificateProviderInstance_msginit;
extern const upb_msglayout envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CombinedCertificateValidationContext_msginit;

static const upb_msglayout *layouts[6] = {
  &envoy_extensions_transport_sockets_tls_v3_UpstreamTlsContext_msginit,
  &envoy_extensions_transport_sockets_tls_v3_DownstreamTlsContext_msginit,
  &envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_msginit,
  &envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CertificateProvider_msginit,
  &envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CertificateProviderInstance_msginit,
  &envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CombinedCertificateValidationContext_msginit,
};

static const char descriptor[4707] = {'\n', '3', 'e', 'n', 'v', 'o', 'y', '/', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', '/', 't', 'r', 'a', 'n', 's', 'p', 
'o', 'r', 't', '_', 's', 'o', 'c', 'k', 'e', 't', 's', '/', 't', 'l', 's', '/', 'v', '3', '/', 't', 'l', 's', '.', 'p', 'r', 
'o', 't', 'o', '\022', ')', 'e', 'n', 'v', 'o', 'y', '.', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', '.', 't', 'r', 'a', 
'n', 's', 'p', 'o', 'r', 't', '_', 's', 'o', 'c', 'k', 'e', 't', 's', '.', 't', 'l', 's', '.', 'v', '3', '\032', '$', 'e', 'n', 
'v', 'o', 'y', '/', 'c', 'o', 'n', 'f', 'i', 'g', '/', 'c', 'o', 'r', 'e', '/', 'v', '3', '/', 'e', 'x', 't', 'e', 'n', 's', 
'i', 'o', 'n', '.', 'p', 'r', 'o', 't', 'o', '\032', '6', 'e', 'n', 'v', 'o', 'y', '/', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 
'n', 's', '/', 't', 'r', 'a', 'n', 's', 'p', 'o', 'r', 't', '_', 's', 'o', 'c', 'k', 'e', 't', 's', '/', 't', 'l', 's', '/', 
'v', '3', '/', 'c', 'o', 'm', 'm', 'o', 'n', '.', 'p', 'r', 'o', 't', 'o', '\032', '6', 'e', 'n', 'v', 'o', 'y', '/', 'e', 'x', 
't', 'e', 'n', 's', 'i', 'o', 'n', 's', '/', 't', 'r', 'a', 'n', 's', 'p', 'o', 'r', 't', '_', 's', 'o', 'c', 'k', 'e', 't', 
's', '/', 't', 'l', 's', '/', 'v', '3', '/', 's', 'e', 'c', 'r', 'e', 't', '.', 'p', 'r', 'o', 't', 'o', '\032', '\036', 'g', 'o', 
'o', 'g', 'l', 'e', '/', 'p', 'r', 'o', 't', 'o', 'b', 'u', 'f', '/', 'd', 'u', 'r', 'a', 't', 'i', 'o', 'n', '.', 'p', 'r', 
'o', 't', 'o', '\032', '\036', 'g', 'o', 'o', 'g', 'l', 'e', '/', 'p', 'r', 'o', 't', 'o', 'b', 'u', 'f', '/', 'w', 'r', 'a', 'p', 
'p', 'e', 'r', 's', '.', 'p', 'r', 'o', 't', 'o', '\032', '\036', 'u', 'd', 'p', 'a', '/', 'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 
'o', 'n', 's', '/', 'm', 'i', 'g', 'r', 'a', 't', 'e', '.', 'p', 'r', 'o', 't', 'o', '\032', '\035', 'u', 'd', 'p', 'a', '/', 'a', 
'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's', '/', 's', 't', 'a', 't', 'u', 's', '.', 'p', 'r', 'o', 't', 'o', '\032', '!', 
'u', 'd', 'p', 'a', '/', 'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's', '/', 'v', 'e', 'r', 's', 'i', 'o', 'n', 'i', 
'n', 'g', '.', 'p', 'r', 'o', 't', 'o', '\032', '\027', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'e', '/', 'v', 'a', 'l', 'i', 'd', 'a', 
't', 'e', '.', 'p', 'r', 'o', 't', 'o', '\"', '\301', '\002', '\n', '\022', 'U', 'p', 's', 't', 'r', 'e', 'a', 'm', 'T', 'l', 's', 'C', 
'o', 'n', 't', 'e', 'x', 't', '\022', 'i', '\n', '\022', 'c', 'o', 'm', 'm', 'o', 'n', '_', 't', 'l', 's', '_', 'c', 'o', 'n', 't', 
'e', 'x', 't', '\030', '\001', ' ', '\001', '(', '\013', '2', ';', '.', 'e', 'n', 'v', 'o', 'y', '.', 'e', 'x', 't', 'e', 'n', 's', 'i', 
'o', 'n', 's', '.', 't', 'r', 'a', 'n', 's', 'p', 'o', 'r', 't', '_', 's', 'o', 'c', 'k', 'e', 't', 's', '.', 't', 'l', 's', 
'.', 'v', '3', '.', 'C', 'o', 'm', 'm', 'o', 'n', 'T', 'l', 's', 'C', 'o', 'n', 't', 'e', 'x', 't', 'R', '\020', 'c', 'o', 'm', 
'm', 'o', 'n', 'T', 'l', 's', 'C', 'o', 'n', 't', 'e', 'x', 't', '\022', '\032', '\n', '\003', 's', 'n', 'i', '\030', '\002', ' ', '\001', '(', 
'\t', 'B', '\010', '\372', 'B', '\005', 'r', '\003', '(', '\377', '\001', 'R', '\003', 's', 'n', 'i', '\022', '/', '\n', '\023', 'a', 'l', 'l', 'o', 'w', 
'_', 'r', 'e', 'n', 'e', 'g', 'o', 't', 'i', 'a', 't', 'i', 'o', 'n', '\030', '\003', ' ', '\001', '(', '\010', 'R', '\022', 'a', 'l', 'l', 
'o', 'w', 'R', 'e', 'n', 'e', 'g', 'o', 't', 'i', 'a', 't', 'i', 'o', 'n', '\022', 'F', '\n', '\020', 'm', 'a', 'x', '_', 's', 'e', 
's', 's', 'i', 'o', 'n', '_', 'k', 'e', 'y', 's', '\030', '\004', ' ', '\001', '(', '\013', '2', '\034', '.', 'g', 'o', 'o', 'g', 'l', 'e', 
'.', 'p', 'r', 'o', 't', 'o', 'b', 'u', 'f', '.', 'U', 'I', 'n', 't', '3', '2', 'V', 'a', 'l', 'u', 'e', 'R', '\016', 'm', 'a', 
'x', 'S', 'e', 's', 's', 'i', 'o', 'n', 'K', 'e', 'y', 's', ':', '+', '\232', '\305', '\210', '\036', '&', '\n', '$', 'e', 'n', 'v', 'o', 
'y', '.', 'a', 'p', 'i', '.', 'v', '2', '.', 'a', 'u', 't', 'h', '.', 'U', 'p', 's', 't', 'r', 'e', 'a', 'm', 'T', 'l', 's', 
'C', 'o', 'n', 't', 'e', 'x', 't', '\"', '\352', '\007', '\n', '\024', 'D', 'o', 'w', 'n', 's', 't', 'r', 'e', 'a', 'm', 'T', 'l', 's', 
'C', 'o', 'n', 't', 'e', 'x', 't', '\022', 'i', '\n', '\022', 'c', 'o', 'm', 'm', 'o', 'n', '_', 't', 'l', 's', '_', 'c', 'o', 'n', 
't', 'e', 'x', 't', '\030', '\001', ' ', '\001', '(', '\013', '2', ';', '.', 'e', 'n', 'v', 'o', 'y', '.', 'e', 'x', 't', 'e', 'n', 's', 
'i', 'o', 'n', 's', '.', 't', 'r', 'a', 'n', 's', 'p', 'o', 'r', 't', '_', 's', 'o', 'c', 'k', 'e', 't', 's', '.', 't', 'l', 
's', '.', 'v', '3', '.', 'C', 'o', 'm', 'm', 'o', 'n', 'T', 'l', 's', 'C', 'o', 'n', 't', 'e', 'x', 't', 'R', '\020', 'c', 'o', 
'm', 'm', 'o', 'n', 'T', 'l', 's', 'C', 'o', 'n', 't', 'e', 'x', 't', '\022', 'X', '\n', '\032', 'r', 'e', 'q', 'u', 'i', 'r', 'e', 
'_', 'c', 'l', 'i', 'e', 'n', 't', '_', 'c', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 'e', '\030', '\002', ' ', '\001', '(', '\013', 
'2', '\032', '.', 'g', 'o', 'o', 'g', 'l', 'e', '.', 'p', 'r', 'o', 't', 'o', 'b', 'u', 'f', '.', 'B', 'o', 'o', 'l', 'V', 'a', 
'l', 'u', 'e', 'R', '\030', 'r', 'e', 'q', 'u', 'i', 'r', 'e', 'C', 'l', 'i', 'e', 'n', 't', 'C', 'e', 'r', 't', 'i', 'f', 'i', 
'c', 'a', 't', 'e', '\022', ';', '\n', '\013', 'r', 'e', 'q', 'u', 'i', 'r', 'e', '_', 's', 'n', 'i', '\030', '\003', ' ', '\001', '(', '\013', 
'2', '\032', '.', 'g', 'o', 'o', 'g', 'l', 'e', '.', 'p', 'r', 'o', 't', 'o', 'b', 'u', 'f', '.', 'B', 'o', 'o', 'l', 'V', 'a', 
'l', 'u', 'e', 'R', '\n', 'r', 'e', 'q', 'u', 'i', 'r', 'e', 'S', 'n', 'i', '\022', 'q', '\n', '\023', 's', 'e', 's', 's', 'i', 'o', 
'n', '_', 't', 'i', 'c', 'k', 'e', 't', '_', 'k', 'e', 'y', 's', '\030', '\004', ' ', '\001', '(', '\013', '2', '?', '.', 'e', 'n', 'v', 
'o', 'y', '.', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', '.', 't', 'r', 'a', 'n', 's', 'p', 'o', 'r', 't', '_', 's', 
'o', 'c', 'k', 'e', 't', 's', '.', 't', 'l', 's', '.', 'v', '3', '.', 'T', 'l', 's', 'S', 'e', 's', 's', 'i', 'o', 'n', 'T', 
'i', 'c', 'k', 'e', 't', 'K', 'e', 'y', 's', 'H', '\000', 'R', '\021', 's', 'e', 's', 's', 'i', 'o', 'n', 'T', 'i', 'c', 'k', 'e', 
't', 'K', 'e', 'y', 's', '\022', '\215', '\001', '\n', '%', 's', 'e', 's', 's', 'i', 'o', 'n', '_', 't', 'i', 'c', 'k', 'e', 't', '_', 
'k', 'e', 'y', 's', '_', 's', 'd', 's', '_', 's', 'e', 'c', 'r', 'e', 't', '_', 'c', 'o', 'n', 'f', 'i', 'g', '\030', '\005', ' ', 
'\001', '(', '\013', '2', ':', '.', 'e', 'n', 'v', 'o', 'y', '.', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', '.', 't', 'r', 
'a', 'n', 's', 'p', 'o', 'r', 't', '_', 's', 'o', 'c', 'k', 'e', 't', 's', '.', 't', 'l', 's', '.', 'v', '3', '.', 'S', 'd', 
's', 'S', 'e', 'c', 'r', 'e', 't', 'C', 'o', 'n', 'f', 'i', 'g', 'H', '\000', 'R', ' ', 's', 'e', 's', 's', 'i', 'o', 'n', 'T', 
'i', 'c', 'k', 'e', 't', 'K', 'e', 'y', 's', 'S', 'd', 's', 'S', 'e', 'c', 'r', 'e', 't', 'C', 'o', 'n', 'f', 'i', 'g', '\022', 
'Q', '\n', '$', 'd', 'i', 's', 'a', 'b', 'l', 'e', '_', 's', 't', 'a', 't', 'e', 'l', 'e', 's', 's', '_', 's', 'e', 's', 's', 
'i', 'o', 'n', '_', 'r', 'e', 's', 'u', 'm', 'p', 't', 'i', 'o', 'n', '\030', '\007', ' ', '\001', '(', '\010', 'H', '\000', 'R', '!', 'd', 
'i', 's', 'a', 'b', 'l', 'e', 'S', 't', 'a', 't', 'e', 'l', 'e', 's', 's', 'S', 'e', 's', 's', 'i', 'o', 'n', 'R', 'e', 's', 
'u', 'm', 'p', 't', 'i', 'o', 'n', '\022', 'T', '\n', '\017', 's', 'e', 's', 's', 'i', 'o', 'n', '_', 't', 'i', 'm', 'e', 'o', 'u', 
't', '\030', '\006', ' ', '\001', '(', '\013', '2', '\031', '.', 'g', 'o', 'o', 'g', 'l', 'e', '.', 'p', 'r', 'o', 't', 'o', 'b', 'u', 'f', 
'.', 'D', 'u', 'r', 'a', 't', 'i', 'o', 'n', 'B', '\020', '\372', 'B', '\r', '\252', '\001', '\n', '\032', '\006', '\010', '\200', '\200', '\200', '\200', '\020', 
'2', '\000', 'R', '\016', 's', 'e', 's', 's', 'i', 'o', 'n', 'T', 'i', 'm', 'e', 'o', 'u', 't', '\022', '\210', '\001', '\n', '\022', 'o', 'c', 
's', 'p', '_', 's', 't', 'a', 'p', 'l', 'e', '_', 'p', 'o', 'l', 'i', 'c', 'y', '\030', '\010', ' ', '\001', '(', '\016', '2', 'P', '.', 
'e', 'n', 'v', 'o', 'y', '.', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', '.', 't', 'r', 'a', 'n', 's', 'p', 'o', 'r', 
't', '_', 's', 'o', 'c', 'k', 'e', 't', 's', '.', 't', 'l', 's', '.', 'v', '3', '.', 'D', 'o', 'w', 'n', 's', 't', 'r', 'e', 
'a', 'm', 'T', 'l', 's', 'C', 'o', 'n', 't', 'e', 'x', 't', '.', 'O', 'c', 's', 'p', 'S', 't', 'a', 'p', 'l', 'e', 'P', 'o', 
'l', 'i', 'c', 'y', 'B', '\010', '\372', 'B', '\005', '\202', '\001', '\002', '\020', '\001', 'R', '\020', 'o', 'c', 's', 'p', 'S', 't', 'a', 'p', 'l', 
'e', 'P', 'o', 'l', 'i', 'c', 'y', '\"', 'N', '\n', '\020', 'O', 'c', 's', 'p', 'S', 't', 'a', 'p', 'l', 'e', 'P', 'o', 'l', 'i', 
'c', 'y', '\022', '\024', '\n', '\020', 'L', 'E', 'N', 'I', 'E', 'N', 'T', '_', 'S', 'T', 'A', 'P', 'L', 'I', 'N', 'G', '\020', '\000', '\022', 
'\023', '\n', '\017', 'S', 'T', 'R', 'I', 'C', 'T', '_', 'S', 'T', 'A', 'P', 'L', 'I', 'N', 'G', '\020', '\001', '\022', '\017', '\n', '\013', 'M', 
'U', 'S', 'T', '_', 'S', 'T', 'A', 'P', 'L', 'E', '\020', '\002', ':', '-', '\232', '\305', '\210', '\036', '(', '\n', '&', 'e', 'n', 'v', 'o', 
'y', '.', 'a', 'p', 'i', '.', 'v', '2', '.', 'a', 'u', 't', 'h', '.', 'D', 'o', 'w', 'n', 's', 't', 'r', 'e', 'a', 'm', 'T', 
'l', 's', 'C', 'o', 'n', 't', 'e', 'x', 't', 'B', '\032', '\n', '\030', 's', 'e', 's', 's', 'i', 'o', 'n', '_', 't', 'i', 'c', 'k', 
'e', 't', '_', 'k', 'e', 'y', 's', '_', 't', 'y', 'p', 'e', '\"', '\247', '\026', '\n', '\020', 'C', 'o', 'm', 'm', 'o', 'n', 'T', 'l', 
's', 'C', 'o', 'n', 't', 'e', 'x', 't', '\022', 'W', '\n', '\n', 't', 'l', 's', '_', 'p', 'a', 'r', 'a', 'm', 's', '\030', '\001', ' ', 
'\001', '(', '\013', '2', '8', '.', 'e', 'n', 'v', 'o', 'y', '.', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', '.', 't', 'r', 
'a', 'n', 's', 'p', 'o', 'r', 't', '_', 's', 'o', 'c', 'k', 'e', 't', 's', '.', 't', 'l', 's', '.', 'v', '3', '.', 'T', 'l', 
's', 'P', 'a', 'r', 'a', 'm', 'e', 't', 'e', 'r', 's', 'R', '\t', 't', 'l', 's', 'P', 'a', 'r', 'a', 'm', 's', '\022', 'd', '\n', 
'\020', 't', 'l', 's', '_', 'c', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 'e', 's', '\030', '\002', ' ', '\003', '(', '\013', '2', '9', 
'.', 'e', 'n', 'v', 'o', 'y', '.', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', '.', 't', 'r', 'a', 'n', 's', 'p', 'o', 
'r', 't', '_', 's', 'o', 'c', 'k', 'e', 't', 's', '.', 't', 'l', 's', '.', 'v', '3', '.', 'T', 'l', 's', 'C', 'e', 'r', 't', 
'i', 'f', 'i', 'c', 'a', 't', 'e', 'R', '\017', 't', 'l', 's', 'C', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 'e', 's', '\022', 
'\220', '\001', '\n', '\"', 't', 'l', 's', '_', 'c', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 'e', '_', 's', 'd', 's', '_', 's', 
'e', 'c', 'r', 'e', 't', '_', 'c', 'o', 'n', 'f', 'i', 'g', 's', '\030', '\006', ' ', '\003', '(', '\013', '2', ':', '.', 'e', 'n', 'v', 
'o', 'y', '.', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', '.', 't', 'r', 'a', 'n', 's', 'p', 'o', 'r', 't', '_', 's', 
'o', 'c', 'k', 'e', 't', 's', '.', 't', 'l', 's', '.', 'v', '3', '.', 'S', 'd', 's', 'S', 'e', 'c', 'r', 'e', 't', 'C', 'o', 
'n', 'f', 'i', 'g', 'B', '\010', '\372', 'B', '\005', '\222', '\001', '\002', '\020', '\002', 'R', '\036', 't', 'l', 's', 'C', 'e', 'r', 't', 'i', 'f', 
'i', 'c', 'a', 't', 'e', 'S', 'd', 's', 'S', 'e', 'c', 'r', 'e', 't', 'C', 'o', 'n', 'f', 'i', 'g', 's', '\022', '\240', '\001', '\n', 
'$', 't', 'l', 's', '_', 'c', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 'e', '_', 'c', 'e', 'r', 't', 'i', 'f', 'i', 'c', 
'a', 't', 'e', '_', 'p', 'r', 'o', 'v', 'i', 'd', 'e', 'r', '\030', '\t', ' ', '\001', '(', '\013', '2', 'O', '.', 'e', 'n', 'v', 'o', 
'y', '.', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', '.', 't', 'r', 'a', 'n', 's', 'p', 'o', 'r', 't', '_', 's', 'o', 
'c', 'k', 'e', 't', 's', '.', 't', 'l', 's', '.', 'v', '3', '.', 'C', 'o', 'm', 'm', 'o', 'n', 'T', 'l', 's', 'C', 'o', 'n', 
't', 'e', 'x', 't', '.', 'C', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 'e', 'P', 'r', 'o', 'v', 'i', 'd', 'e', 'r', 'R', 
'!', 't', 'l', 's', 'C', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 'e', 'C', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 
'e', 'P', 'r', 'o', 'v', 'i', 'd', 'e', 'r', '\022', '\271', '\001', '\n', '-', 't', 'l', 's', '_', 'c', 'e', 'r', 't', 'i', 'f', 'i', 
'c', 'a', 't', 'e', '_', 'c', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 'e', '_', 'p', 'r', 'o', 'v', 'i', 'd', 'e', 'r', 
'_', 'i', 'n', 's', 't', 'a', 'n', 'c', 'e', '\030', '\013', ' ', '\001', '(', '\013', '2', 'W', '.', 'e', 'n', 'v', 'o', 'y', '.', 'e', 
'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', '.', 't', 'r', 'a', 'n', 's', 'p', 'o', 'r', 't', '_', 's', 'o', 'c', 'k', 'e', 
't', 's', '.', 't', 'l', 's', '.', 'v', '3', '.', 'C', 'o', 'm', 'm', 'o', 'n', 'T', 'l', 's', 'C', 'o', 'n', 't', 'e', 'x', 
't', '.', 'C', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 'e', 'P', 'r', 'o', 'v', 'i', 'd', 'e', 'r', 'I', 'n', 's', 't', 
'a', 'n', 'c', 'e', 'R', ')', 't', 'l', 's', 'C', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 'e', 'C', 'e', 'r', 't', 'i', 
'f', 'i', 'c', 'a', 't', 'e', 'P', 'r', 'o', 'v', 'i', 'd', 'e', 'r', 'I', 'n', 's', 't', 'a', 'n', 'c', 'e', '\022', 'x', '\n', 
'\022', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', '_', 'c', 'o', 'n', 't', 'e', 'x', 't', '\030', '\003', ' ', '\001', '(', '\013', 
'2', 'G', '.', 'e', 'n', 'v', 'o', 'y', '.', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', '.', 't', 'r', 'a', 'n', 's', 
'p', 'o', 'r', 't', '_', 's', 'o', 'c', 'k', 'e', 't', 's', '.', 't', 'l', 's', '.', 'v', '3', '.', 'C', 'e', 'r', 't', 'i', 
'f', 'i', 'c', 'a', 't', 'e', 'V', 'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', 'C', 'o', 'n', 't', 'e', 'x', 't', 'H', '\000', 
'R', '\021', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', 'C', 'o', 'n', 't', 'e', 'x', 't', '\022', '\214', '\001', '\n', '$', 'v', 
'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', '_', 'c', 'o', 'n', 't', 'e', 'x', 't', '_', 's', 'd', 's', '_', 's', 'e', 'c', 
'r', 'e', 't', '_', 'c', 'o', 'n', 'f', 'i', 'g', '\030', '\007', ' ', '\001', '(', '\013', '2', ':', '.', 'e', 'n', 'v', 'o', 'y', '.', 
'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', '.', 't', 'r', 'a', 'n', 's', 'p', 'o', 'r', 't', '_', 's', 'o', 'c', 'k', 
'e', 't', 's', '.', 't', 'l', 's', '.', 'v', '3', '.', 'S', 'd', 's', 'S', 'e', 'c', 'r', 'e', 't', 'C', 'o', 'n', 'f', 'i', 
'g', 'H', '\000', 'R', ' ', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', 'C', 'o', 'n', 't', 'e', 'x', 't', 'S', 'd', 's', 
'S', 'e', 'c', 'r', 'e', 't', 'C', 'o', 'n', 'f', 'i', 'g', '\022', '\242', '\001', '\n', '\033', 'c', 'o', 'm', 'b', 'i', 'n', 'e', 'd', 
'_', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', '_', 'c', 'o', 'n', 't', 'e', 'x', 't', '\030', '\010', ' ', '\001', '(', '\013', 
'2', '`', '.', 'e', 'n', 'v', 'o', 'y', '.', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', '.', 't', 'r', 'a', 'n', 's', 
'p', 'o', 'r', 't', '_', 's', 'o', 'c', 'k', 'e', 't', 's', '.', 't', 'l', 's', '.', 'v', '3', '.', 'C', 'o', 'm', 'm', 'o', 
'n', 'T', 'l', 's', 'C', 'o', 'n', 't', 'e', 'x', 't', '.', 'C', 'o', 'm', 'b', 'i', 'n', 'e', 'd', 'C', 'e', 'r', 't', 'i', 
'f', 'i', 'c', 'a', 't', 'e', 'V', 'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', 'C', 'o', 'n', 't', 'e', 'x', 't', 'H', '\000', 
'R', '\031', 'c', 'o', 'm', 'b', 'i', 'n', 'e', 'd', 'V', 'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', 'C', 'o', 'n', 't', 'e', 
'x', 't', '\022', '\250', '\001', '\n', '\'', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', '_', 'c', 'o', 'n', 't', 'e', 'x', 't', 
'_', 'c', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 'e', '_', 'p', 'r', 'o', 'v', 'i', 'd', 'e', 'r', '\030', '\n', ' ', '\001', 
'(', '\013', '2', 'O', '.', 'e', 'n', 'v', 'o', 'y', '.', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', '.', 't', 'r', 'a', 
'n', 's', 'p', 'o', 'r', 't', '_', 's', 'o', 'c', 'k', 'e', 't', 's', '.', 't', 'l', 's', '.', 'v', '3', '.', 'C', 'o', 'm', 
'm', 'o', 'n', 'T', 'l', 's', 'C', 'o', 'n', 't', 'e', 'x', 't', '.', 'C', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 'e', 
'P', 'r', 'o', 'v', 'i', 'd', 'e', 'r', 'H', '\000', 'R', '$', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', 'C', 'o', 'n', 
't', 'e', 'x', 't', 'C', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 'e', 'P', 'r', 'o', 'v', 'i', 'd', 'e', 'r', '\022', '\301', 
'\001', '\n', '0', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', '_', 'c', 'o', 'n', 't', 'e', 'x', 't', '_', 'c', 'e', 'r', 
't', 'i', 'f', 'i', 'c', 'a', 't', 'e', '_', 'p', 'r', 'o', 'v', 'i', 'd', 'e', 'r', '_', 'i', 'n', 's', 't', 'a', 'n', 'c', 
'e', '\030', '\014', ' ', '\001', '(', '\013', '2', 'W', '.', 'e', 'n', 'v', 'o', 'y', '.', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 
's', '.', 't', 'r', 'a', 'n', 's', 'p', 'o', 'r', 't', '_', 's', 'o', 'c', 'k', 'e', 't', 's', '.', 't', 'l', 's', '.', 'v', 
'3', '.', 'C', 'o', 'm', 'm', 'o', 'n', 'T', 'l', 's', 'C', 'o', 'n', 't', 'e', 'x', 't', '.', 'C', 'e', 'r', 't', 'i', 'f', 
'i', 'c', 'a', 't', 'e', 'P', 'r', 'o', 'v', 'i', 'd', 'e', 'r', 'I', 'n', 's', 't', 'a', 'n', 'c', 'e', 'H', '\000', 'R', ',', 
'v', 'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', 'C', 'o', 'n', 't', 'e', 'x', 't', 'C', 'e', 'r', 't', 'i', 'f', 'i', 'c', 
'a', 't', 'e', 'P', 'r', 'o', 'v', 'i', 'd', 'e', 'r', 'I', 'n', 's', 't', 'a', 'n', 'c', 'e', '\022', '%', '\n', '\016', 'a', 'l', 
'p', 'n', '_', 'p', 'r', 'o', 't', 'o', 'c', 'o', 'l', 's', '\030', '\004', ' ', '\003', '(', '\t', 'R', '\r', 'a', 'l', 'p', 'n', 'P', 
'r', 'o', 't', 'o', 'c', 'o', 'l', 's', '\022', 'W', '\n', '\021', 'c', 'u', 's', 't', 'o', 'm', '_', 'h', 'a', 'n', 'd', 's', 'h', 
'a', 'k', 'e', 'r', '\030', '\r', ' ', '\001', '(', '\013', '2', '*', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', 
'.', 'c', 'o', 'r', 'e', '.', 'v', '3', '.', 'T', 'y', 'p', 'e', 'd', 'E', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 'C', 'o', 
'n', 'f', 'i', 'g', 'R', '\020', 'c', 'u', 's', 't', 'o', 'm', 'H', 'a', 'n', 'd', 's', 'h', 'a', 'k', 'e', 'r', '\032', '\222', '\001', 
'\n', '\023', 'C', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 'e', 'P', 'r', 'o', 'v', 'i', 'd', 'e', 'r', '\022', '\033', '\n', '\004', 
'n', 'a', 'm', 'e', '\030', '\001', ' ', '\001', '(', '\t', 'B', '\007', '\372', 'B', '\004', 'r', '\002', '\020', '\001', 'R', '\004', 'n', 'a', 'm', 'e', 
'\022', 'O', '\n', '\014', 't', 'y', 'p', 'e', 'd', '_', 'c', 'o', 'n', 'f', 'i', 'g', '\030', '\002', ' ', '\001', '(', '\013', '2', '*', '.', 
'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'r', 'e', '.', 'v', '3', '.', 'T', 'y', 'p', 'e', 
'd', 'E', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 'C', 'o', 'n', 'f', 'i', 'g', 'H', '\000', 'R', '\013', 't', 'y', 'p', 'e', 'd', 
'C', 'o', 'n', 'f', 'i', 'g', 'B', '\r', '\n', '\006', 'c', 'o', 'n', 'f', 'i', 'g', '\022', '\003', '\370', 'B', '\001', '\032', 'm', '\n', '\033', 
'C', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 'e', 'P', 'r', 'o', 'v', 'i', 'd', 'e', 'r', 'I', 'n', 's', 't', 'a', 'n', 
'c', 'e', '\022', '#', '\n', '\r', 'i', 'n', 's', 't', 'a', 'n', 'c', 'e', '_', 'n', 'a', 'm', 'e', '\030', '\001', ' ', '\001', '(', '\t', 
'R', '\014', 'i', 'n', 's', 't', 'a', 'n', 'c', 'e', 'N', 'a', 'm', 'e', '\022', ')', '\n', '\020', 'c', 'e', 'r', 't', 'i', 'f', 'i', 
'c', 'a', 't', 'e', '_', 'n', 'a', 'm', 'e', '\030', '\002', ' ', '\001', '(', '\t', 'R', '\017', 'c', 'e', 'r', 't', 'i', 'f', 'i', 'c', 
'a', 't', 'e', 'N', 'a', 'm', 'e', '\032', '\364', '\006', '\n', '$', 'C', 'o', 'm', 'b', 'i', 'n', 'e', 'd', 'C', 'e', 'r', 't', 'i', 
'f', 'i', 'c', 'a', 't', 'e', 'V', 'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', 'C', 'o', 'n', 't', 'e', 'x', 't', '\022', '\217', 
'\001', '\n', '\032', 'd', 'e', 'f', 'a', 'u', 'l', 't', '_', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', '_', 'c', 'o', 'n', 
't', 'e', 'x', 't', '\030', '\001', ' ', '\001', '(', '\013', '2', 'G', '.', 'e', 'n', 'v', 'o', 'y', '.', 'e', 'x', 't', 'e', 'n', 's', 
'i', 'o', 'n', 's', '.', 't', 'r', 'a', 'n', 's', 'p', 'o', 'r', 't', '_', 's', 'o', 'c', 'k', 'e', 't', 's', '.', 't', 'l', 
's', '.', 'v', '3', '.', 'C', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 'e', 'V', 'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 
'n', 'C', 'o', 'n', 't', 'e', 'x', 't', 'B', '\010', '\372', 'B', '\005', '\212', '\001', '\002', '\020', '\001', 'R', '\030', 'd', 'e', 'f', 'a', 'u', 
'l', 't', 'V', 'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', 'C', 'o', 'n', 't', 'e', 'x', 't', '\022', '\266', '\001', '\n', '$', 'v', 
'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', '_', 'c', 'o', 'n', 't', 'e', 'x', 't', '_', 's', 'd', 's', '_', 's', 'e', 'c', 
'r', 'e', 't', '_', 'c', 'o', 'n', 'f', 'i', 'g', '\030', '\002', ' ', '\001', '(', '\013', '2', ':', '.', 'e', 'n', 'v', 'o', 'y', '.', 
'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', '.', 't', 'r', 'a', 'n', 's', 'p', 'o', 'r', 't', '_', 's', 'o', 'c', 'k', 
'e', 't', 's', '.', 't', 'l', 's', '.', 'v', '3', '.', 'S', 'd', 's', 'S', 'e', 'c', 'r', 'e', 't', 'C', 'o', 'n', 'f', 'i', 
'g', 'B', '*', '\372', 'B', '\005', '\212', '\001', '\002', '\020', '\001', '\362', '\230', '\376', '\217', '\005', '\034', '\022', '\032', 'd', 'y', 'n', 'a', 'm', 'i', 
'c', '_', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', '_', 'c', 'o', 'n', 't', 'e', 'x', 't', 'R', ' ', 'v', 'a', 'l', 
'i', 'd', 'a', 't', 'i', 'o', 'n', 'C', 'o', 'n', 't', 'e', 'x', 't', 'S', 'd', 's', 'S', 'e', 'c', 'r', 'e', 't', 'C', 'o', 
'n', 'f', 'i', 'g', '\022', '\312', '\001', '\n', '\'', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', '_', 'c', 'o', 'n', 't', 'e', 
'x', 't', '_', 'c', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 'e', '_', 'p', 'r', 'o', 'v', 'i', 'd', 'e', 'r', '\030', '\003', 
' ', '\001', '(', '\013', '2', 'O', '.', 'e', 'n', 'v', 'o', 'y', '.', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', '.', 't', 
'r', 'a', 'n', 's', 'p', 'o', 'r', 't', '_', 's', 'o', 'c', 'k', 'e', 't', 's', '.', 't', 'l', 's', '.', 'v', '3', '.', 'C', 
'o', 'm', 'm', 'o', 'n', 'T', 'l', 's', 'C', 'o', 'n', 't', 'e', 'x', 't', '.', 'C', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 
't', 'e', 'P', 'r', 'o', 'v', 'i', 'd', 'e', 'r', 'B', '\"', '\362', '\230', '\376', '\217', '\005', '\034', '\022', '\032', 'd', 'y', 'n', 'a', 'm', 
'i', 'c', '_', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', '_', 'c', 'o', 'n', 't', 'e', 'x', 't', 'R', '$', 'v', 'a', 
'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', 'C', 'o', 'n', 't', 'e', 'x', 't', 'C', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 
'e', 'P', 'r', 'o', 'v', 'i', 'd', 'e', 'r', '\022', '\343', '\001', '\n', '0', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', '_', 
'c', 'o', 'n', 't', 'e', 'x', 't', '_', 'c', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 'e', '_', 'p', 'r', 'o', 'v', 'i', 
'd', 'e', 'r', '_', 'i', 'n', 's', 't', 'a', 'n', 'c', 'e', '\030', '\004', ' ', '\001', '(', '\013', '2', 'W', '.', 'e', 'n', 'v', 'o', 
'y', '.', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', '.', 't', 'r', 'a', 'n', 's', 'p', 'o', 'r', 't', '_', 's', 'o', 
'c', 'k', 'e', 't', 's', '.', 't', 'l', 's', '.', 'v', '3', '.', 'C', 'o', 'm', 'm', 'o', 'n', 'T', 'l', 's', 'C', 'o', 'n', 
't', 'e', 'x', 't', '.', 'C', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 'e', 'P', 'r', 'o', 'v', 'i', 'd', 'e', 'r', 'I', 
'n', 's', 't', 'a', 'n', 'c', 'e', 'B', '\"', '\362', '\230', '\376', '\217', '\005', '\034', '\022', '\032', 'd', 'y', 'n', 'a', 'm', 'i', 'c', '_', 
'v', 'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', '_', 'c', 'o', 'n', 't', 'e', 'x', 't', 'R', ',', 'v', 'a', 'l', 'i', 'd', 
'a', 't', 'i', 'o', 'n', 'C', 'o', 'n', 't', 'e', 'x', 't', 'C', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 'e', 'P', 'r', 
'o', 'v', 'i', 'd', 'e', 'r', 'I', 'n', 's', 't', 'a', 'n', 'c', 'e', ':', 'N', '\232', '\305', '\210', '\036', 'I', '\n', 'G', 'e', 'n', 
'v', 'o', 'y', '.', 'a', 'p', 'i', '.', 'v', '2', '.', 'a', 'u', 't', 'h', '.', 'C', 'o', 'm', 'm', 'o', 'n', 'T', 'l', 's', 
'C', 'o', 'n', 't', 'e', 'x', 't', '.', 'C', 'o', 'm', 'b', 'i', 'n', 'e', 'd', 'C', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 
't', 'e', 'V', 'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 'n', 'C', 'o', 'n', 't', 'e', 'x', 't', ':', ')', '\232', '\305', '\210', '\036', 
'$', '\n', '\"', 'e', 'n', 'v', 'o', 'y', '.', 'a', 'p', 'i', '.', 'v', '2', '.', 'a', 'u', 't', 'h', '.', 'C', 'o', 'm', 'm', 
'o', 'n', 'T', 'l', 's', 'C', 'o', 'n', 't', 'e', 'x', 't', 'B', '\031', '\n', '\027', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'i', 'o', 
'n', '_', 'c', 'o', 'n', 't', 'e', 'x', 't', '_', 't', 'y', 'p', 'e', 'J', '\004', '\010', '\005', '\020', '\006', 'B', 'M', '\n', '7', 'i', 
'o', '.', 'e', 'n', 'v', 'o', 'y', 'p', 'r', 'o', 'x', 'y', '.', 'e', 'n', 'v', 'o', 'y', '.', 'e', 'x', 't', 'e', 'n', 's', 
'i', 'o', 'n', 's', '.', 't', 'r', 'a', 'n', 's', 'p', 'o', 'r', 't', '_', 's', 'o', 'c', 'k', 'e', 't', 's', '.', 't', 'l', 
's', '.', 'v', '3', 'B', '\010', 'T', 'l', 's', 'P', 'r', 'o', 't', 'o', 'P', '\001', '\272', '\200', '\310', '\321', '\006', '\002', '\020', '\002', 'b', 
'\006', 'p', 'r', 'o', 't', 'o', '3', 
};

static upb_def_init *deps[10] = {
  &envoy_config_core_v3_extension_proto_upbdefinit,
  &envoy_extensions_transport_sockets_tls_v3_common_proto_upbdefinit,
  &envoy_extensions_transport_sockets_tls_v3_secret_proto_upbdefinit,
  &google_protobuf_duration_proto_upbdefinit,
  &google_protobuf_wrappers_proto_upbdefinit,
  &udpa_annotations_migrate_proto_upbdefinit,
  &udpa_annotations_status_proto_upbdefinit,
  &udpa_annotations_versioning_proto_upbdefinit,
  &validate_validate_proto_upbdefinit,
  NULL
};

upb_def_init envoy_extensions_transport_sockets_tls_v3_tls_proto_upbdefinit = {
  deps,
  layouts,
  "envoy/extensions/transport_sockets/tls/v3/tls.proto",
  UPB_STRVIEW_INIT(descriptor, 4707)
};
