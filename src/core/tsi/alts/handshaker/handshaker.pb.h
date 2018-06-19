/* Automatically generated nanopb header */
/* Generated by nanopb-0.3.7-dev */

#ifndef PB_GRPC_GCP_HANDSHAKER_PB_H_INCLUDED
#define PB_GRPC_GCP_HANDSHAKER_PB_H_INCLUDED
#include "pb.h"
#include "src/core/tsi/alts/handshaker/transport_security_common.pb.h"

/* @@protoc_insertion_point(includes) */
#if PB_PROTO_HEADER_VERSION != 30
#error Regenerate this file with the current version of nanopb generator.
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Enum definitions */
typedef enum _grpc_gcp_HandshakeProtocol {
    grpc_gcp_HandshakeProtocol_HANDSHAKE_PROTOCOL_UNSPECIFIED = 0,
    grpc_gcp_HandshakeProtocol_TLS = 1,
    grpc_gcp_HandshakeProtocol_ALTS = 2
} grpc_gcp_HandshakeProtocol;
#define _grpc_gcp_HandshakeProtocol_MIN grpc_gcp_HandshakeProtocol_HANDSHAKE_PROTOCOL_UNSPECIFIED
#define _grpc_gcp_HandshakeProtocol_MAX grpc_gcp_HandshakeProtocol_ALTS
#define _grpc_gcp_HandshakeProtocol_ARRAYSIZE ((grpc_gcp_HandshakeProtocol)(grpc_gcp_HandshakeProtocol_ALTS+1))

typedef enum _grpc_gcp_NetworkProtocol {
    grpc_gcp_NetworkProtocol_NETWORK_PROTOCOL_UNSPECIFIED = 0,
    grpc_gcp_NetworkProtocol_TCP = 1,
    grpc_gcp_NetworkProtocol_UDP = 2
} grpc_gcp_NetworkProtocol;
#define _grpc_gcp_NetworkProtocol_MIN grpc_gcp_NetworkProtocol_NETWORK_PROTOCOL_UNSPECIFIED
#define _grpc_gcp_NetworkProtocol_MAX grpc_gcp_NetworkProtocol_UDP
#define _grpc_gcp_NetworkProtocol_ARRAYSIZE ((grpc_gcp_NetworkProtocol)(grpc_gcp_NetworkProtocol_UDP+1))

/* Struct definitions */
typedef struct _grpc_gcp_Identity {
    pb_callback_t service_account;
    pb_callback_t hostname;
/* @@protoc_insertion_point(struct:grpc_gcp_Identity) */
} grpc_gcp_Identity;

typedef struct _grpc_gcp_NextHandshakeMessageReq {
    pb_callback_t in_bytes;
/* @@protoc_insertion_point(struct:grpc_gcp_NextHandshakeMessageReq) */
} grpc_gcp_NextHandshakeMessageReq;

typedef struct _grpc_gcp_ServerHandshakeParameters {
    pb_callback_t record_protocols;
    pb_callback_t local_identities;
/* @@protoc_insertion_point(struct:grpc_gcp_ServerHandshakeParameters) */
} grpc_gcp_ServerHandshakeParameters;

typedef struct _grpc_gcp_Endpoint {
    pb_callback_t ip_address;
    bool has_port;
    int32_t port;
    bool has_protocol;
    grpc_gcp_NetworkProtocol protocol;
/* @@protoc_insertion_point(struct:grpc_gcp_Endpoint) */
} grpc_gcp_Endpoint;

typedef struct _grpc_gcp_HandshakerResult {
    pb_callback_t application_protocol;
    pb_callback_t record_protocol;
    pb_callback_t key_data;
    bool has_peer_identity;
    grpc_gcp_Identity peer_identity;
    bool has_local_identity;
    grpc_gcp_Identity local_identity;
    bool has_keep_channel_open;
    bool keep_channel_open;
    bool has_peer_rpc_versions;
    grpc_gcp_RpcProtocolVersions peer_rpc_versions;
/* @@protoc_insertion_point(struct:grpc_gcp_HandshakerResult) */
} grpc_gcp_HandshakerResult;

typedef struct _grpc_gcp_HandshakerStatus {
    bool has_code;
    uint32_t code;
    pb_callback_t details;
/* @@protoc_insertion_point(struct:grpc_gcp_HandshakerStatus) */
} grpc_gcp_HandshakerStatus;

typedef struct _grpc_gcp_StartServerHandshakeReq_HandshakeParametersEntry {
    bool has_key;
    int32_t key;
    bool has_value;
    grpc_gcp_ServerHandshakeParameters value;
/* @@protoc_insertion_point(struct:grpc_gcp_StartServerHandshakeReq_HandshakeParametersEntry) */
} grpc_gcp_StartServerHandshakeReq_HandshakeParametersEntry;

typedef struct _grpc_gcp_HandshakerResp {
    pb_callback_t out_frames;
    bool has_bytes_consumed;
    uint32_t bytes_consumed;
    bool has_result;
    grpc_gcp_HandshakerResult result;
    bool has_status;
    grpc_gcp_HandshakerStatus status;
/* @@protoc_insertion_point(struct:grpc_gcp_HandshakerResp) */
} grpc_gcp_HandshakerResp;

typedef struct _grpc_gcp_StartClientHandshakeReq {
    bool has_handshake_security_protocol;
    grpc_gcp_HandshakeProtocol handshake_security_protocol;
    pb_callback_t application_protocols;
    pb_callback_t record_protocols;
    pb_callback_t target_identities;
    bool has_local_identity;
    grpc_gcp_Identity local_identity;
    bool has_local_endpoint;
    grpc_gcp_Endpoint local_endpoint;
    bool has_remote_endpoint;
    grpc_gcp_Endpoint remote_endpoint;
    pb_callback_t target_name;
    bool has_rpc_versions;
    grpc_gcp_RpcProtocolVersions rpc_versions;
/* @@protoc_insertion_point(struct:grpc_gcp_StartClientHandshakeReq) */
} grpc_gcp_StartClientHandshakeReq;

typedef struct _grpc_gcp_StartServerHandshakeReq {
    pb_callback_t application_protocols;
    pb_size_t handshake_parameters_count;
    grpc_gcp_StartServerHandshakeReq_HandshakeParametersEntry handshake_parameters[3];
    pb_callback_t in_bytes;
    bool has_local_endpoint;
    grpc_gcp_Endpoint local_endpoint;
    bool has_remote_endpoint;
    grpc_gcp_Endpoint remote_endpoint;
    bool has_rpc_versions;
    grpc_gcp_RpcProtocolVersions rpc_versions;
/* @@protoc_insertion_point(struct:grpc_gcp_StartServerHandshakeReq) */
} grpc_gcp_StartServerHandshakeReq;

typedef struct _grpc_gcp_HandshakerReq {
    bool has_client_start;
    grpc_gcp_StartClientHandshakeReq client_start;
    bool has_server_start;
    grpc_gcp_StartServerHandshakeReq server_start;
    bool has_next;
    grpc_gcp_NextHandshakeMessageReq next;
/* @@protoc_insertion_point(struct:grpc_gcp_HandshakerReq) */
} grpc_gcp_HandshakerReq;

/* Default values for struct fields */

/* Initializer values for message structs */
#define grpc_gcp_Endpoint_init_default           {{{NULL}, NULL}, false, 0, false, (grpc_gcp_NetworkProtocol)0}
#define grpc_gcp_Identity_init_default           {{{NULL}, NULL}, {{NULL}, NULL}}
#define grpc_gcp_StartClientHandshakeReq_init_default {false, (grpc_gcp_HandshakeProtocol)0, {{NULL}, NULL}, {{NULL}, NULL}, {{NULL}, NULL}, false, grpc_gcp_Identity_init_default, false, grpc_gcp_Endpoint_init_default, false, grpc_gcp_Endpoint_init_default, {{NULL}, NULL}, false, grpc_gcp_RpcProtocolVersions_init_default}
#define grpc_gcp_ServerHandshakeParameters_init_default {{{NULL}, NULL}, {{NULL}, NULL}}
#define grpc_gcp_StartServerHandshakeReq_init_default {{{NULL}, NULL}, 0, {grpc_gcp_StartServerHandshakeReq_HandshakeParametersEntry_init_default, grpc_gcp_StartServerHandshakeReq_HandshakeParametersEntry_init_default, grpc_gcp_StartServerHandshakeReq_HandshakeParametersEntry_init_default}, {{NULL}, NULL}, false, grpc_gcp_Endpoint_init_default, false, grpc_gcp_Endpoint_init_default, false, grpc_gcp_RpcProtocolVersions_init_default}
#define grpc_gcp_StartServerHandshakeReq_HandshakeParametersEntry_init_default {false, 0, false, grpc_gcp_ServerHandshakeParameters_init_default}
#define grpc_gcp_NextHandshakeMessageReq_init_default {{{NULL}, NULL}}
#define grpc_gcp_HandshakerReq_init_default      {false, grpc_gcp_StartClientHandshakeReq_init_default, false, grpc_gcp_StartServerHandshakeReq_init_default, false, grpc_gcp_NextHandshakeMessageReq_init_default}
#define grpc_gcp_HandshakerResult_init_default   {{{NULL}, NULL}, {{NULL}, NULL}, {{NULL}, NULL}, false, grpc_gcp_Identity_init_default, false, grpc_gcp_Identity_init_default, false, 0, false, grpc_gcp_RpcProtocolVersions_init_default}
#define grpc_gcp_HandshakerStatus_init_default   {false, 0, {{NULL}, NULL}}
#define grpc_gcp_HandshakerResp_init_default     {{{NULL}, NULL}, false, 0, false, grpc_gcp_HandshakerResult_init_default, false, grpc_gcp_HandshakerStatus_init_default}
#define grpc_gcp_Endpoint_init_zero              {{{NULL}, NULL}, false, 0, false, (grpc_gcp_NetworkProtocol)0}
#define grpc_gcp_Identity_init_zero              {{{NULL}, NULL}, {{NULL}, NULL}}
#define grpc_gcp_StartClientHandshakeReq_init_zero {false, (grpc_gcp_HandshakeProtocol)0, {{NULL}, NULL}, {{NULL}, NULL}, {{NULL}, NULL}, false, grpc_gcp_Identity_init_zero, false, grpc_gcp_Endpoint_init_zero, false, grpc_gcp_Endpoint_init_zero, {{NULL}, NULL}, false, grpc_gcp_RpcProtocolVersions_init_zero}
#define grpc_gcp_ServerHandshakeParameters_init_zero {{{NULL}, NULL}, {{NULL}, NULL}}
#define grpc_gcp_StartServerHandshakeReq_init_zero {{{NULL}, NULL}, 0, {grpc_gcp_StartServerHandshakeReq_HandshakeParametersEntry_init_zero, grpc_gcp_StartServerHandshakeReq_HandshakeParametersEntry_init_zero, grpc_gcp_StartServerHandshakeReq_HandshakeParametersEntry_init_zero}, {{NULL}, NULL}, false, grpc_gcp_Endpoint_init_zero, false, grpc_gcp_Endpoint_init_zero, false, grpc_gcp_RpcProtocolVersions_init_zero}
#define grpc_gcp_StartServerHandshakeReq_HandshakeParametersEntry_init_zero {false, 0, false, grpc_gcp_ServerHandshakeParameters_init_zero}
#define grpc_gcp_NextHandshakeMessageReq_init_zero {{{NULL}, NULL}}
#define grpc_gcp_HandshakerReq_init_zero         {false, grpc_gcp_StartClientHandshakeReq_init_zero, false, grpc_gcp_StartServerHandshakeReq_init_zero, false, grpc_gcp_NextHandshakeMessageReq_init_zero}
#define grpc_gcp_HandshakerResult_init_zero      {{{NULL}, NULL}, {{NULL}, NULL}, {{NULL}, NULL}, false, grpc_gcp_Identity_init_zero, false, grpc_gcp_Identity_init_zero, false, 0, false, grpc_gcp_RpcProtocolVersions_init_zero}
#define grpc_gcp_HandshakerStatus_init_zero      {false, 0, {{NULL}, NULL}}
#define grpc_gcp_HandshakerResp_init_zero        {{{NULL}, NULL}, false, 0, false, grpc_gcp_HandshakerResult_init_zero, false, grpc_gcp_HandshakerStatus_init_zero}

/* Field tags (for use in manual encoding/decoding) */
#define grpc_gcp_Identity_service_account_tag    1
#define grpc_gcp_Identity_hostname_tag           2
#define grpc_gcp_NextHandshakeMessageReq_in_bytes_tag 1
#define grpc_gcp_ServerHandshakeParameters_record_protocols_tag 1
#define grpc_gcp_ServerHandshakeParameters_local_identities_tag 2
#define grpc_gcp_Endpoint_ip_address_tag         1
#define grpc_gcp_Endpoint_port_tag               2
#define grpc_gcp_Endpoint_protocol_tag           3
#define grpc_gcp_HandshakerResult_application_protocol_tag 1
#define grpc_gcp_HandshakerResult_record_protocol_tag 2
#define grpc_gcp_HandshakerResult_key_data_tag   3
#define grpc_gcp_HandshakerResult_peer_identity_tag 4
#define grpc_gcp_HandshakerResult_local_identity_tag 5
#define grpc_gcp_HandshakerResult_keep_channel_open_tag 6
#define grpc_gcp_HandshakerResult_peer_rpc_versions_tag 7
#define grpc_gcp_HandshakerStatus_code_tag       1
#define grpc_gcp_HandshakerStatus_details_tag    2
#define grpc_gcp_StartServerHandshakeReq_HandshakeParametersEntry_key_tag 1
#define grpc_gcp_StartServerHandshakeReq_HandshakeParametersEntry_value_tag 2
#define grpc_gcp_HandshakerResp_out_frames_tag   1
#define grpc_gcp_HandshakerResp_bytes_consumed_tag 2
#define grpc_gcp_HandshakerResp_result_tag       3
#define grpc_gcp_HandshakerResp_status_tag       4
#define grpc_gcp_StartClientHandshakeReq_handshake_security_protocol_tag 1
#define grpc_gcp_StartClientHandshakeReq_application_protocols_tag 2
#define grpc_gcp_StartClientHandshakeReq_record_protocols_tag 3
#define grpc_gcp_StartClientHandshakeReq_target_identities_tag 4
#define grpc_gcp_StartClientHandshakeReq_local_identity_tag 5
#define grpc_gcp_StartClientHandshakeReq_local_endpoint_tag 6
#define grpc_gcp_StartClientHandshakeReq_remote_endpoint_tag 7
#define grpc_gcp_StartClientHandshakeReq_target_name_tag 8
#define grpc_gcp_StartClientHandshakeReq_rpc_versions_tag 9
#define grpc_gcp_StartServerHandshakeReq_application_protocols_tag 1
#define grpc_gcp_StartServerHandshakeReq_handshake_parameters_tag 2
#define grpc_gcp_StartServerHandshakeReq_in_bytes_tag 3
#define grpc_gcp_StartServerHandshakeReq_local_endpoint_tag 4
#define grpc_gcp_StartServerHandshakeReq_remote_endpoint_tag 5
#define grpc_gcp_StartServerHandshakeReq_rpc_versions_tag 6
#define grpc_gcp_HandshakerReq_client_start_tag  1
#define grpc_gcp_HandshakerReq_server_start_tag  2
#define grpc_gcp_HandshakerReq_next_tag          3

/* Struct field encoding specification for nanopb */
extern const pb_field_t grpc_gcp_Endpoint_fields[4];
extern const pb_field_t grpc_gcp_Identity_fields[3];
extern const pb_field_t grpc_gcp_StartClientHandshakeReq_fields[10];
extern const pb_field_t grpc_gcp_ServerHandshakeParameters_fields[3];
extern const pb_field_t grpc_gcp_StartServerHandshakeReq_fields[7];
extern const pb_field_t grpc_gcp_StartServerHandshakeReq_HandshakeParametersEntry_fields[3];
extern const pb_field_t grpc_gcp_NextHandshakeMessageReq_fields[2];
extern const pb_field_t grpc_gcp_HandshakerReq_fields[4];
extern const pb_field_t grpc_gcp_HandshakerResult_fields[8];
extern const pb_field_t grpc_gcp_HandshakerStatus_fields[3];
extern const pb_field_t grpc_gcp_HandshakerResp_fields[5];

/* Maximum encoded size of messages (where known) */
/* grpc_gcp_Endpoint_size depends on runtime parameters */
/* grpc_gcp_Identity_size depends on runtime parameters */
/* grpc_gcp_StartClientHandshakeReq_size depends on runtime parameters */
/* grpc_gcp_ServerHandshakeParameters_size depends on runtime parameters */
/* grpc_gcp_StartServerHandshakeReq_size depends on runtime parameters */
#define grpc_gcp_StartServerHandshakeReq_HandshakeParametersEntry_size (17 + grpc_gcp_ServerHandshakeParameters_size)
/* grpc_gcp_NextHandshakeMessageReq_size depends on runtime parameters */
#define grpc_gcp_HandshakerReq_size              (18 + grpc_gcp_StartClientHandshakeReq_size + grpc_gcp_StartServerHandshakeReq_size + grpc_gcp_NextHandshakeMessageReq_size)
/* grpc_gcp_HandshakerResult_size depends on runtime parameters */
/* grpc_gcp_HandshakerStatus_size depends on runtime parameters */
/* grpc_gcp_HandshakerResp_size depends on runtime parameters */

/* Message IDs (where set with "msgid" option) */
#ifdef PB_MSGID

#define HANDSHAKER_MESSAGES \


#endif

#ifdef __cplusplus
} /* extern "C" */
#endif
/* @@protoc_insertion_point(eof) */

#endif
