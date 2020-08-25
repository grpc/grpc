/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/listener/v3/listener.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include "upb/def.h"
#include "envoy/config/listener/v3/listener.upbdefs.h"

extern upb_def_init envoy_config_accesslog_v3_accesslog_proto_upbdefinit;
extern upb_def_init envoy_config_core_v3_address_proto_upbdefinit;
extern upb_def_init envoy_config_core_v3_base_proto_upbdefinit;
extern upb_def_init envoy_config_core_v3_socket_option_proto_upbdefinit;
extern upb_def_init envoy_config_listener_v3_api_listener_proto_upbdefinit;
extern upb_def_init envoy_config_listener_v3_listener_components_proto_upbdefinit;
extern upb_def_init envoy_config_listener_v3_udp_listener_config_proto_upbdefinit;
extern upb_def_init google_api_annotations_proto_upbdefinit;
extern upb_def_init google_protobuf_duration_proto_upbdefinit;
extern upb_def_init google_protobuf_wrappers_proto_upbdefinit;
extern upb_def_init udpa_annotations_status_proto_upbdefinit;
extern upb_def_init udpa_annotations_versioning_proto_upbdefinit;
extern upb_def_init validate_validate_proto_upbdefinit;
extern const upb_msglayout envoy_config_listener_v3_Listener_msginit;
extern const upb_msglayout envoy_config_listener_v3_Listener_DeprecatedV1_msginit;
extern const upb_msglayout envoy_config_listener_v3_Listener_ConnectionBalanceConfig_msginit;
extern const upb_msglayout envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance_msginit;

static const upb_msglayout *layouts[4] = {
  &envoy_config_listener_v3_Listener_msginit,
  &envoy_config_listener_v3_Listener_DeprecatedV1_msginit,
  &envoy_config_listener_v3_Listener_ConnectionBalanceConfig_msginit,
  &envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance_msginit,
};

static const char descriptor[2660] = {'\n', '\'', 'e', 'n', 'v', 'o', 'y', '/', 'c', 'o', 'n', 'f', 'i', 'g', '/', 'l', 'i', 's', 't', 'e', 'n', 'e', 'r', '/', 'v', 
'3', '/', 'l', 'i', 's', 't', 'e', 'n', 'e', 'r', '.', 'p', 'r', 'o', 't', 'o', '\022', '\030', 'e', 'n', 'v', 'o', 'y', '.', 'c', 
'o', 'n', 'f', 'i', 'g', '.', 'l', 'i', 's', 't', 'e', 'n', 'e', 'r', '.', 'v', '3', '\032', ')', 'e', 'n', 'v', 'o', 'y', '/', 
'c', 'o', 'n', 'f', 'i', 'g', '/', 'a', 'c', 'c', 'e', 's', 's', 'l', 'o', 'g', '/', 'v', '3', '/', 'a', 'c', 'c', 'e', 's', 
's', 'l', 'o', 'g', '.', 'p', 'r', 'o', 't', 'o', '\032', '\"', 'e', 'n', 'v', 'o', 'y', '/', 'c', 'o', 'n', 'f', 'i', 'g', '/', 
'c', 'o', 'r', 'e', '/', 'v', '3', '/', 'a', 'd', 'd', 'r', 'e', 's', 's', '.', 'p', 'r', 'o', 't', 'o', '\032', '\037', 'e', 'n', 
'v', 'o', 'y', '/', 'c', 'o', 'n', 'f', 'i', 'g', '/', 'c', 'o', 'r', 'e', '/', 'v', '3', '/', 'b', 'a', 's', 'e', '.', 'p', 
'r', 'o', 't', 'o', '\032', '(', 'e', 'n', 'v', 'o', 'y', '/', 'c', 'o', 'n', 'f', 'i', 'g', '/', 'c', 'o', 'r', 'e', '/', 'v', 
'3', '/', 's', 'o', 'c', 'k', 'e', 't', '_', 'o', 'p', 't', 'i', 'o', 'n', '.', 'p', 'r', 'o', 't', 'o', '\032', '+', 'e', 'n', 
'v', 'o', 'y', '/', 'c', 'o', 'n', 'f', 'i', 'g', '/', 'l', 'i', 's', 't', 'e', 'n', 'e', 'r', '/', 'v', '3', '/', 'a', 'p', 
'i', '_', 'l', 'i', 's', 't', 'e', 'n', 'e', 'r', '.', 'p', 'r', 'o', 't', 'o', '\032', '2', 'e', 'n', 'v', 'o', 'y', '/', 'c', 
'o', 'n', 'f', 'i', 'g', '/', 'l', 'i', 's', 't', 'e', 'n', 'e', 'r', '/', 'v', '3', '/', 'l', 'i', 's', 't', 'e', 'n', 'e', 
'r', '_', 'c', 'o', 'm', 'p', 'o', 'n', 'e', 'n', 't', 's', '.', 'p', 'r', 'o', 't', 'o', '\032', '2', 'e', 'n', 'v', 'o', 'y', 
'/', 'c', 'o', 'n', 'f', 'i', 'g', '/', 'l', 'i', 's', 't', 'e', 'n', 'e', 'r', '/', 'v', '3', '/', 'u', 'd', 'p', '_', 'l', 
'i', 's', 't', 'e', 'n', 'e', 'r', '_', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'p', 'r', 'o', 't', 'o', '\032', '\034', 'g', 'o', 'o', 
'g', 'l', 'e', '/', 'a', 'p', 'i', '/', 'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's', '.', 'p', 'r', 'o', 't', 'o', 
'\032', '\036', 'g', 'o', 'o', 'g', 'l', 'e', '/', 'p', 'r', 'o', 't', 'o', 'b', 'u', 'f', '/', 'd', 'u', 'r', 'a', 't', 'i', 'o', 
'n', '.', 'p', 'r', 'o', 't', 'o', '\032', '\036', 'g', 'o', 'o', 'g', 'l', 'e', '/', 'p', 'r', 'o', 't', 'o', 'b', 'u', 'f', '/', 
'w', 'r', 'a', 'p', 'p', 'e', 'r', 's', '.', 'p', 'r', 'o', 't', 'o', '\032', '\035', 'u', 'd', 'p', 'a', '/', 'a', 'n', 'n', 'o', 
't', 'a', 't', 'i', 'o', 'n', 's', '/', 's', 't', 'a', 't', 'u', 's', '.', 'p', 'r', 'o', 't', 'o', '\032', '!', 'u', 'd', 'p', 
'a', '/', 'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's', '/', 'v', 'e', 'r', 's', 'i', 'o', 'n', 'i', 'n', 'g', '.', 
'p', 'r', 'o', 't', 'o', '\032', '\027', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'e', '/', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'e', '.', 
'p', 'r', 'o', 't', 'o', '\"', '\353', '\017', '\n', '\010', 'L', 'i', 's', 't', 'e', 'n', 'e', 'r', '\022', '\022', '\n', '\004', 'n', 'a', 'm', 
'e', '\030', '\001', ' ', '\001', '(', '\t', 'R', '\004', 'n', 'a', 'm', 'e', '\022', 'A', '\n', '\007', 'a', 'd', 'd', 'r', 'e', 's', 's', '\030', 
'\002', ' ', '\001', '(', '\013', '2', '\035', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'r', 'e', 
'.', 'v', '3', '.', 'A', 'd', 'd', 'r', 'e', 's', 's', 'B', '\010', '\372', 'B', '\005', '\212', '\001', '\002', '\020', '\001', 'R', '\007', 'a', 'd', 
'd', 'r', 'e', 's', 's', '\022', 'J', '\n', '\r', 'f', 'i', 'l', 't', 'e', 'r', '_', 'c', 'h', 'a', 'i', 'n', 's', '\030', '\003', ' ', 
'\003', '(', '\013', '2', '%', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'l', 'i', 's', 't', 'e', 'n', 
'e', 'r', '.', 'v', '3', '.', 'F', 'i', 'l', 't', 'e', 'r', 'C', 'h', 'a', 'i', 'n', 'R', '\014', 'f', 'i', 'l', 't', 'e', 'r', 
'C', 'h', 'a', 'i', 'n', 's', '\022', 'f', '\n', '!', 'p', 'e', 'r', '_', 'c', 'o', 'n', 'n', 'e', 'c', 't', 'i', 'o', 'n', '_', 
'b', 'u', 'f', 'f', 'e', 'r', '_', 'l', 'i', 'm', 'i', 't', '_', 'b', 'y', 't', 'e', 's', '\030', '\005', ' ', '\001', '(', '\013', '2', 
'\034', '.', 'g', 'o', 'o', 'g', 'l', 'e', '.', 'p', 'r', 'o', 't', 'o', 'b', 'u', 'f', '.', 'U', 'I', 'n', 't', '3', '2', 'V', 
'a', 'l', 'u', 'e', 'R', '\035', 'p', 'e', 'r', 'C', 'o', 'n', 'n', 'e', 'c', 't', 'i', 'o', 'n', 'B', 'u', 'f', 'f', 'e', 'r', 
'L', 'i', 'm', 'i', 't', 'B', 'y', 't', 'e', 's', '\022', ':', '\n', '\010', 'm', 'e', 't', 'a', 'd', 'a', 't', 'a', '\030', '\006', ' ', 
'\001', '(', '\013', '2', '\036', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'r', 'e', '.', 'v', 
'3', '.', 'M', 'e', 't', 'a', 'd', 'a', 't', 'a', 'R', '\010', 'm', 'e', 't', 'a', 'd', 'a', 't', 'a', '\022', 'T', '\n', '\r', 'd', 
'e', 'p', 'r', 'e', 'c', 'a', 't', 'e', 'd', '_', 'v', '1', '\030', '\007', ' ', '\001', '(', '\013', '2', '/', '.', 'e', 'n', 'v', 'o', 
'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'l', 'i', 's', 't', 'e', 'n', 'e', 'r', '.', 'v', '3', '.', 'L', 'i', 's', 't', 
'e', 'n', 'e', 'r', '.', 'D', 'e', 'p', 'r', 'e', 'c', 'a', 't', 'e', 'd', 'V', '1', 'R', '\014', 'd', 'e', 'p', 'r', 'e', 'c', 
'a', 't', 'e', 'd', 'V', '1', '\022', 'K', '\n', '\n', 'd', 'r', 'a', 'i', 'n', '_', 't', 'y', 'p', 'e', '\030', '\010', ' ', '\001', '(', 
'\016', '2', ',', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'l', 'i', 's', 't', 'e', 'n', 'e', 'r', 
'.', 'v', '3', '.', 'L', 'i', 's', 't', 'e', 'n', 'e', 'r', '.', 'D', 'r', 'a', 'i', 'n', 'T', 'y', 'p', 'e', 'R', '\t', 'd', 
'r', 'a', 'i', 'n', 'T', 'y', 'p', 'e', '\022', 'S', '\n', '\020', 'l', 'i', 's', 't', 'e', 'n', 'e', 'r', '_', 'f', 'i', 'l', 't', 
'e', 'r', 's', '\030', '\t', ' ', '\003', '(', '\013', '2', '(', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 
'l', 'i', 's', 't', 'e', 'n', 'e', 'r', '.', 'v', '3', '.', 'L', 'i', 's', 't', 'e', 'n', 'e', 'r', 'F', 'i', 'l', 't', 'e', 
'r', 'R', '\017', 'l', 'i', 's', 't', 'e', 'n', 'e', 'r', 'F', 'i', 'l', 't', 'e', 'r', 's', '\022', 'S', '\n', '\030', 'l', 'i', 's', 
't', 'e', 'n', 'e', 'r', '_', 'f', 'i', 'l', 't', 'e', 'r', 's', '_', 't', 'i', 'm', 'e', 'o', 'u', 't', '\030', '\017', ' ', '\001', 
'(', '\013', '2', '\031', '.', 'g', 'o', 'o', 'g', 'l', 'e', '.', 'p', 'r', 'o', 't', 'o', 'b', 'u', 'f', '.', 'D', 'u', 'r', 'a', 
't', 'i', 'o', 'n', 'R', '\026', 'l', 'i', 's', 't', 'e', 'n', 'e', 'r', 'F', 'i', 'l', 't', 'e', 'r', 's', 'T', 'i', 'm', 'e', 
'o', 'u', 't', '\022', 'N', '\n', '$', 'c', 'o', 'n', 't', 'i', 'n', 'u', 'e', '_', 'o', 'n', '_', 'l', 'i', 's', 't', 'e', 'n', 
'e', 'r', '_', 'f', 'i', 'l', 't', 'e', 'r', 's', '_', 't', 'i', 'm', 'e', 'o', 'u', 't', '\030', '\021', ' ', '\001', '(', '\010', 'R', 
' ', 'c', 'o', 'n', 't', 'i', 'n', 'u', 'e', 'O', 'n', 'L', 'i', 's', 't', 'e', 'n', 'e', 'r', 'F', 'i', 'l', 't', 'e', 'r', 
's', 'T', 'i', 'm', 'e', 'o', 'u', 't', '\022', '<', '\n', '\013', 't', 'r', 'a', 'n', 's', 'p', 'a', 'r', 'e', 'n', 't', '\030', '\n', 
' ', '\001', '(', '\013', '2', '\032', '.', 'g', 'o', 'o', 'g', 'l', 'e', '.', 'p', 'r', 'o', 't', 'o', 'b', 'u', 'f', '.', 'B', 'o', 
'o', 'l', 'V', 'a', 'l', 'u', 'e', 'R', '\013', 't', 'r', 'a', 'n', 's', 'p', 'a', 'r', 'e', 'n', 't', '\022', '6', '\n', '\010', 'f', 
'r', 'e', 'e', 'b', 'i', 'n', 'd', '\030', '\013', ' ', '\001', '(', '\013', '2', '\032', '.', 'g', 'o', 'o', 'g', 'l', 'e', '.', 'p', 'r', 
'o', 't', 'o', 'b', 'u', 'f', '.', 'B', 'o', 'o', 'l', 'V', 'a', 'l', 'u', 'e', 'R', '\010', 'f', 'r', 'e', 'e', 'b', 'i', 'n', 
'd', '\022', 'I', '\n', '\016', 's', 'o', 'c', 'k', 'e', 't', '_', 'o', 'p', 't', 'i', 'o', 'n', 's', '\030', '\r', ' ', '\003', '(', '\013', 
'2', '\"', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'r', 'e', '.', 'v', '3', '.', 'S', 
'o', 'c', 'k', 'e', 't', 'O', 'p', 't', 'i', 'o', 'n', 'R', '\r', 's', 'o', 'c', 'k', 'e', 't', 'O', 'p', 't', 'i', 'o', 'n', 
's', '\022', 'X', '\n', '\032', 't', 'c', 'p', '_', 'f', 'a', 's', 't', '_', 'o', 'p', 'e', 'n', '_', 'q', 'u', 'e', 'u', 'e', '_', 
'l', 'e', 'n', 'g', 't', 'h', '\030', '\014', ' ', '\001', '(', '\013', '2', '\034', '.', 'g', 'o', 'o', 'g', 'l', 'e', '.', 'p', 'r', 'o', 
't', 'o', 'b', 'u', 'f', '.', 'U', 'I', 'n', 't', '3', '2', 'V', 'a', 'l', 'u', 'e', 'R', '\026', 't', 'c', 'p', 'F', 'a', 's', 
't', 'O', 'p', 'e', 'n', 'Q', 'u', 'e', 'u', 'e', 'L', 'e', 'n', 'g', 't', 'h', '\022', 'S', '\n', '\021', 't', 'r', 'a', 'f', 'f', 
'i', 'c', '_', 'd', 'i', 'r', 'e', 'c', 't', 'i', 'o', 'n', '\030', '\020', ' ', '\001', '(', '\016', '2', '&', '.', 'e', 'n', 'v', 'o', 
'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'r', 'e', '.', 'v', '3', '.', 'T', 'r', 'a', 'f', 'f', 'i', 'c', 'D', 
'i', 'r', 'e', 'c', 't', 'i', 'o', 'n', 'R', '\020', 't', 'r', 'a', 'f', 'f', 'i', 'c', 'D', 'i', 'r', 'e', 'c', 't', 'i', 'o', 
'n', '\022', '[', '\n', '\023', 'u', 'd', 'p', '_', 'l', 'i', 's', 't', 'e', 'n', 'e', 'r', '_', 'c', 'o', 'n', 'f', 'i', 'g', '\030', 
'\022', ' ', '\001', '(', '\013', '2', '+', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'l', 'i', 's', 't', 
'e', 'n', 'e', 'r', '.', 'v', '3', '.', 'U', 'd', 'p', 'L', 'i', 's', 't', 'e', 'n', 'e', 'r', 'C', 'o', 'n', 'f', 'i', 'g', 
'R', '\021', 'u', 'd', 'p', 'L', 'i', 's', 't', 'e', 'n', 'e', 'r', 'C', 'o', 'n', 'f', 'i', 'g', '\022', 'H', '\n', '\014', 'a', 'p', 
'i', '_', 'l', 'i', 's', 't', 'e', 'n', 'e', 'r', '\030', '\023', ' ', '\001', '(', '\013', '2', '%', '.', 'e', 'n', 'v', 'o', 'y', '.', 
'c', 'o', 'n', 'f', 'i', 'g', '.', 'l', 'i', 's', 't', 'e', 'n', 'e', 'r', '.', 'v', '3', '.', 'A', 'p', 'i', 'L', 'i', 's', 
't', 'e', 'n', 'e', 'r', 'R', '\013', 'a', 'p', 'i', 'L', 'i', 's', 't', 'e', 'n', 'e', 'r', '\022', 'v', '\n', '\031', 'c', 'o', 'n', 
'n', 'e', 'c', 't', 'i', 'o', 'n', '_', 'b', 'a', 'l', 'a', 'n', 'c', 'e', '_', 'c', 'o', 'n', 'f', 'i', 'g', '\030', '\024', ' ', 
'\001', '(', '\013', '2', ':', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'l', 'i', 's', 't', 'e', 'n', 
'e', 'r', '.', 'v', '3', '.', 'L', 'i', 's', 't', 'e', 'n', 'e', 'r', '.', 'C', 'o', 'n', 'n', 'e', 'c', 't', 'i', 'o', 'n', 
'B', 'a', 'l', 'a', 'n', 'c', 'e', 'C', 'o', 'n', 'f', 'i', 'g', 'R', '\027', 'c', 'o', 'n', 'n', 'e', 'c', 't', 'i', 'o', 'n', 
'B', 'a', 'l', 'a', 'n', 'c', 'e', 'C', 'o', 'n', 'f', 'i', 'g', '\022', '\035', '\n', '\n', 'r', 'e', 'u', 's', 'e', '_', 'p', 'o', 
'r', 't', '\030', '\025', ' ', '\001', '(', '\010', 'R', '\t', 'r', 'e', 'u', 's', 'e', 'P', 'o', 'r', 't', '\022', 'C', '\n', '\n', 'a', 'c', 
'c', 'e', 's', 's', '_', 'l', 'o', 'g', '\030', '\026', ' ', '\003', '(', '\013', '2', '$', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 
'n', 'f', 'i', 'g', '.', 'a', 'c', 'c', 'e', 's', 's', 'l', 'o', 'g', '.', 'v', '3', '.', 'A', 'c', 'c', 'e', 's', 's', 'L', 
'o', 'g', 'R', '\t', 'a', 'c', 'c', 'e', 's', 's', 'L', 'o', 'g', '\032', 'w', '\n', '\014', 'D', 'e', 'p', 'r', 'e', 'c', 'a', 't', 
'e', 'd', 'V', '1', '\022', '<', '\n', '\014', 'b', 'i', 'n', 'd', '_', 't', 'o', '_', 'p', 'o', 'r', 't', '\030', '\001', ' ', '\001', '(', 
'\013', '2', '\032', '.', 'g', 'o', 'o', 'g', 'l', 'e', '.', 'p', 'r', 'o', 't', 'o', 'b', 'u', 'f', '.', 'B', 'o', 'o', 'l', 'V', 
'a', 'l', 'u', 'e', 'R', '\n', 'b', 'i', 'n', 'd', 'T', 'o', 'P', 'o', 'r', 't', ':', ')', '\232', '\305', '\210', '\036', '$', '\n', '\"', 
'e', 'n', 'v', 'o', 'y', '.', 'a', 'p', 'i', '.', 'v', '2', '.', 'L', 'i', 's', 't', 'e', 'n', 'e', 'r', '.', 'D', 'e', 'p', 
'r', 'e', 'c', 'a', 't', 'e', 'd', 'V', '1', '\032', '\247', '\002', '\n', '\027', 'C', 'o', 'n', 'n', 'e', 'c', 't', 'i', 'o', 'n', 'B', 
'a', 'l', 'a', 'n', 'c', 'e', 'C', 'o', 'n', 'f', 'i', 'g', '\022', 'n', '\n', '\r', 'e', 'x', 'a', 'c', 't', '_', 'b', 'a', 'l', 
'a', 'n', 'c', 'e', '\030', '\001', ' ', '\001', '(', '\013', '2', 'G', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', 
'.', 'l', 'i', 's', 't', 'e', 'n', 'e', 'r', '.', 'v', '3', '.', 'L', 'i', 's', 't', 'e', 'n', 'e', 'r', '.', 'C', 'o', 'n', 
'n', 'e', 'c', 't', 'i', 'o', 'n', 'B', 'a', 'l', 'a', 'n', 'c', 'e', 'C', 'o', 'n', 'f', 'i', 'g', '.', 'E', 'x', 'a', 'c', 
't', 'B', 'a', 'l', 'a', 'n', 'c', 'e', 'H', '\000', 'R', '\014', 'e', 'x', 'a', 'c', 't', 'B', 'a', 'l', 'a', 'n', 'c', 'e', '\032', 
'Q', '\n', '\014', 'E', 'x', 'a', 'c', 't', 'B', 'a', 'l', 'a', 'n', 'c', 'e', ':', 'A', '\232', '\305', '\210', '\036', '<', '\n', ':', 'e', 
'n', 'v', 'o', 'y', '.', 'a', 'p', 'i', '.', 'v', '2', '.', 'L', 'i', 's', 't', 'e', 'n', 'e', 'r', '.', 'C', 'o', 'n', 'n', 
'e', 'c', 't', 'i', 'o', 'n', 'B', 'a', 'l', 'a', 'n', 'c', 'e', 'C', 'o', 'n', 'f', 'i', 'g', '.', 'E', 'x', 'a', 'c', 't', 
'B', 'a', 'l', 'a', 'n', 'c', 'e', ':', '4', '\232', '\305', '\210', '\036', '/', '\n', '-', 'e', 'n', 'v', 'o', 'y', '.', 'a', 'p', 'i', 
'.', 'v', '2', '.', 'L', 'i', 's', 't', 'e', 'n', 'e', 'r', '.', 'C', 'o', 'n', 'n', 'e', 'c', 't', 'i', 'o', 'n', 'B', 'a', 
'l', 'a', 'n', 'c', 'e', 'C', 'o', 'n', 'f', 'i', 'g', 'B', '\023', '\n', '\014', 'b', 'a', 'l', 'a', 'n', 'c', 'e', '_', 't', 'y', 
'p', 'e', '\022', '\003', '\370', 'B', '\001', '\"', ')', '\n', '\t', 'D', 'r', 'a', 'i', 'n', 'T', 'y', 'p', 'e', '\022', '\013', '\n', '\007', 'D', 
'E', 'F', 'A', 'U', 'L', 'T', '\020', '\000', '\022', '\017', '\n', '\013', 'M', 'O', 'D', 'I', 'F', 'Y', '_', 'O', 'N', 'L', 'Y', '\020', '\001', 
':', '\034', '\232', '\305', '\210', '\036', '\027', '\n', '\025', 'e', 'n', 'v', 'o', 'y', '.', 'a', 'p', 'i', '.', 'v', '2', '.', 'L', 'i', 's', 
't', 'e', 'n', 'e', 'r', 'J', '\004', '\010', '\016', '\020', '\017', 'J', '\004', '\010', '\004', '\020', '\005', 'R', '\020', 'u', 's', 'e', '_', 'o', 'r', 
'i', 'g', 'i', 'n', 'a', 'l', '_', 'd', 's', 't', 'B', 'A', '\n', '&', 'i', 'o', '.', 'e', 'n', 'v', 'o', 'y', 'p', 'r', 'o', 
'x', 'y', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'l', 'i', 's', 't', 'e', 'n', 'e', 'r', '.', 
'v', '3', 'B', '\r', 'L', 'i', 's', 't', 'e', 'n', 'e', 'r', 'P', 'r', 'o', 't', 'o', 'P', '\001', '\272', '\200', '\310', '\321', '\006', '\002', 
'\020', '\002', 'b', '\006', 'p', 'r', 'o', 't', 'o', '3', 
};

static upb_def_init *deps[14] = {
  &envoy_config_accesslog_v3_accesslog_proto_upbdefinit,
  &envoy_config_core_v3_address_proto_upbdefinit,
  &envoy_config_core_v3_base_proto_upbdefinit,
  &envoy_config_core_v3_socket_option_proto_upbdefinit,
  &envoy_config_listener_v3_api_listener_proto_upbdefinit,
  &envoy_config_listener_v3_listener_components_proto_upbdefinit,
  &envoy_config_listener_v3_udp_listener_config_proto_upbdefinit,
  &google_api_annotations_proto_upbdefinit,
  &google_protobuf_duration_proto_upbdefinit,
  &google_protobuf_wrappers_proto_upbdefinit,
  &udpa_annotations_status_proto_upbdefinit,
  &udpa_annotations_versioning_proto_upbdefinit,
  &validate_validate_proto_upbdefinit,
  NULL
};

upb_def_init envoy_config_listener_v3_listener_proto_upbdefinit = {
  deps,
  layouts,
  "envoy/config/listener/v3/listener.proto",
  UPB_STRVIEW_INIT(descriptor, 2660)
};
