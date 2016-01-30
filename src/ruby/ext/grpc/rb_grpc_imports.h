/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPC_RB_GRPC_IMPORTS_H_
#define GRPC_RB_GRPC_IMPORTS_H_

#include <include/grpc/census.h>
#include <include/grpc/compression.h>
#include <include/grpc/grpc.h>
#include <include/grpc/grpc_security.h>

typedef const grpc_auth_property *(*grpc_auth_property_iterator_next_type)(grpc_auth_property_iterator *it);
extern grpc_auth_property_iterator_next_type grpc_auth_property_iterator_next_import;
#define grpc_auth_property_iterator_next grpc_auth_property_iterator_next_import
typedef grpc_auth_property_iterator(*grpc_auth_context_property_iterator_type)(const grpc_auth_context *ctx);
extern grpc_auth_context_property_iterator_type grpc_auth_context_property_iterator_import;
#define grpc_auth_context_property_iterator grpc_auth_context_property_iterator_import
typedef grpc_auth_property_iterator(*grpc_auth_context_peer_identity_type)(const grpc_auth_context *ctx);
extern grpc_auth_context_peer_identity_type grpc_auth_context_peer_identity_import;
#define grpc_auth_context_peer_identity grpc_auth_context_peer_identity_import
typedef grpc_auth_property_iterator(*grpc_auth_context_find_properties_by_name_type)(const grpc_auth_context *ctx, const char *name);
extern grpc_auth_context_find_properties_by_name_type grpc_auth_context_find_properties_by_name_import;
#define grpc_auth_context_find_properties_by_name grpc_auth_context_find_properties_by_name_import
typedef const char *(*grpc_auth_context_peer_identity_property_name_type)(const grpc_auth_context *ctx);
extern grpc_auth_context_peer_identity_property_name_type grpc_auth_context_peer_identity_property_name_import;
#define grpc_auth_context_peer_identity_property_name grpc_auth_context_peer_identity_property_name_import
typedef int(*grpc_auth_context_peer_is_authenticated_type)(const grpc_auth_context *ctx);
extern grpc_auth_context_peer_is_authenticated_type grpc_auth_context_peer_is_authenticated_import;
#define grpc_auth_context_peer_is_authenticated grpc_auth_context_peer_is_authenticated_import
typedef grpc_auth_context *(*grpc_call_auth_context_type)(grpc_call *call);
extern grpc_call_auth_context_type grpc_call_auth_context_import;
#define grpc_call_auth_context grpc_call_auth_context_import
typedef void(*grpc_auth_context_release_type)(grpc_auth_context *context);
extern grpc_auth_context_release_type grpc_auth_context_release_import;
#define grpc_auth_context_release grpc_auth_context_release_import
typedef void(*grpc_auth_context_add_property_type)(grpc_auth_context *ctx, const char *name, const char *value, size_t value_length);
extern grpc_auth_context_add_property_type grpc_auth_context_add_property_import;
#define grpc_auth_context_add_property grpc_auth_context_add_property_import
typedef void(*grpc_auth_context_add_cstring_property_type)(grpc_auth_context *ctx, const char *name, const char *value);
extern grpc_auth_context_add_cstring_property_type grpc_auth_context_add_cstring_property_import;
#define grpc_auth_context_add_cstring_property grpc_auth_context_add_cstring_property_import
typedef int(*grpc_auth_context_set_peer_identity_property_name_type)(grpc_auth_context *ctx, const char *name);
extern grpc_auth_context_set_peer_identity_property_name_type grpc_auth_context_set_peer_identity_property_name_import;
#define grpc_auth_context_set_peer_identity_property_name grpc_auth_context_set_peer_identity_property_name_import
typedef void(*grpc_channel_credentials_release_type)(grpc_channel_credentials *creds);
extern grpc_channel_credentials_release_type grpc_channel_credentials_release_import;
#define grpc_channel_credentials_release grpc_channel_credentials_release_import
typedef grpc_channel_credentials *(*grpc_google_default_credentials_create_type)(void);
extern grpc_google_default_credentials_create_type grpc_google_default_credentials_create_import;
#define grpc_google_default_credentials_create grpc_google_default_credentials_create_import
typedef grpc_channel_credentials *(*grpc_ssl_credentials_create_type)(const char *pem_root_certs, grpc_ssl_pem_key_cert_pair *pem_key_cert_pair, void *reserved);
extern grpc_ssl_credentials_create_type grpc_ssl_credentials_create_import;
#define grpc_ssl_credentials_create grpc_ssl_credentials_create_import
typedef void(*grpc_call_credentials_release_type)(grpc_call_credentials *creds);
extern grpc_call_credentials_release_type grpc_call_credentials_release_import;
#define grpc_call_credentials_release grpc_call_credentials_release_import
typedef grpc_channel_credentials *(*grpc_composite_channel_credentials_create_type)(grpc_channel_credentials *channel_creds, grpc_call_credentials *call_creds, void *reserved);
extern grpc_composite_channel_credentials_create_type grpc_composite_channel_credentials_create_import;
#define grpc_composite_channel_credentials_create grpc_composite_channel_credentials_create_import
typedef grpc_call_credentials *(*grpc_composite_call_credentials_create_type)(grpc_call_credentials *creds1, grpc_call_credentials *creds2, void *reserved);
extern grpc_composite_call_credentials_create_type grpc_composite_call_credentials_create_import;
#define grpc_composite_call_credentials_create grpc_composite_call_credentials_create_import
typedef grpc_call_credentials *(*grpc_google_compute_engine_credentials_create_type)(void *reserved);
extern grpc_google_compute_engine_credentials_create_type grpc_google_compute_engine_credentials_create_import;
#define grpc_google_compute_engine_credentials_create grpc_google_compute_engine_credentials_create_import
typedef grpc_call_credentials *(*grpc_service_account_jwt_access_credentials_create_type)(const char *json_key, gpr_timespec token_lifetime, void *reserved);
extern grpc_service_account_jwt_access_credentials_create_type grpc_service_account_jwt_access_credentials_create_import;
#define grpc_service_account_jwt_access_credentials_create grpc_service_account_jwt_access_credentials_create_import
typedef grpc_call_credentials *(*grpc_google_refresh_token_credentials_create_type)(const char *json_refresh_token, void *reserved);
extern grpc_google_refresh_token_credentials_create_type grpc_google_refresh_token_credentials_create_import;
#define grpc_google_refresh_token_credentials_create grpc_google_refresh_token_credentials_create_import
typedef grpc_call_credentials *(*grpc_access_token_credentials_create_type)(const char *access_token, void *reserved);
extern grpc_access_token_credentials_create_type grpc_access_token_credentials_create_import;
#define grpc_access_token_credentials_create grpc_access_token_credentials_create_import
typedef grpc_call_credentials *(*grpc_google_iam_credentials_create_type)(const char *authorization_token, const char *authority_selector, void *reserved);
extern grpc_google_iam_credentials_create_type grpc_google_iam_credentials_create_import;
#define grpc_google_iam_credentials_create grpc_google_iam_credentials_create_import
typedef grpc_call_credentials *(*grpc_metadata_credentials_create_from_plugin_type)(grpc_metadata_credentials_plugin plugin, void *reserved);
extern grpc_metadata_credentials_create_from_plugin_type grpc_metadata_credentials_create_from_plugin_import;
#define grpc_metadata_credentials_create_from_plugin grpc_metadata_credentials_create_from_plugin_import
typedef grpc_channel *(*grpc_secure_channel_create_type)(grpc_channel_credentials *creds, const char *target, const grpc_channel_args *args, void *reserved);
extern grpc_secure_channel_create_type grpc_secure_channel_create_import;
#define grpc_secure_channel_create grpc_secure_channel_create_import
typedef void(*grpc_server_credentials_release_type)(grpc_server_credentials *creds);
extern grpc_server_credentials_release_type grpc_server_credentials_release_import;
#define grpc_server_credentials_release grpc_server_credentials_release_import
typedef grpc_server_credentials *(*grpc_ssl_server_credentials_create_type)(const char *pem_root_certs, grpc_ssl_pem_key_cert_pair *pem_key_cert_pairs, size_t num_key_cert_pairs, int force_client_auth, void *reserved);
extern grpc_ssl_server_credentials_create_type grpc_ssl_server_credentials_create_import;
#define grpc_ssl_server_credentials_create grpc_ssl_server_credentials_create_import
typedef int(*grpc_server_add_secure_http2_port_type)(grpc_server *server, const char *addr, grpc_server_credentials *creds);
extern grpc_server_add_secure_http2_port_type grpc_server_add_secure_http2_port_import;
#define grpc_server_add_secure_http2_port grpc_server_add_secure_http2_port_import
typedef grpc_call_error(*grpc_call_set_credentials_type)(grpc_call *call, grpc_call_credentials *creds);
extern grpc_call_set_credentials_type grpc_call_set_credentials_import;
#define grpc_call_set_credentials grpc_call_set_credentials_import
typedef void(*grpc_server_credentials_set_auth_metadata_processor_type)(grpc_server_credentials *creds, grpc_auth_metadata_processor processor);
extern grpc_server_credentials_set_auth_metadata_processor_type grpc_server_credentials_set_auth_metadata_processor_import;
#define grpc_server_credentials_set_auth_metadata_processor grpc_server_credentials_set_auth_metadata_processor_import
typedef int(*grpc_compression_algorithm_parse_type)(const char *name, size_t name_length, grpc_compression_algorithm *algorithm);
extern grpc_compression_algorithm_parse_type grpc_compression_algorithm_parse_import;
#define grpc_compression_algorithm_parse grpc_compression_algorithm_parse_import
typedef int(*grpc_compression_algorithm_name_type)(grpc_compression_algorithm algorithm, char **name);
extern grpc_compression_algorithm_name_type grpc_compression_algorithm_name_import;
#define grpc_compression_algorithm_name grpc_compression_algorithm_name_import
typedef grpc_compression_algorithm(*grpc_compression_algorithm_for_level_type)(grpc_compression_level level);
extern grpc_compression_algorithm_for_level_type grpc_compression_algorithm_for_level_import;
#define grpc_compression_algorithm_for_level grpc_compression_algorithm_for_level_import
typedef void(*grpc_compression_options_init_type)(grpc_compression_options *opts);
extern grpc_compression_options_init_type grpc_compression_options_init_import;
#define grpc_compression_options_init grpc_compression_options_init_import
typedef void(*grpc_compression_options_enable_algorithm_type)(grpc_compression_options *opts, grpc_compression_algorithm algorithm);
extern grpc_compression_options_enable_algorithm_type grpc_compression_options_enable_algorithm_import;
#define grpc_compression_options_enable_algorithm grpc_compression_options_enable_algorithm_import
typedef void(*grpc_compression_options_disable_algorithm_type)(grpc_compression_options *opts, grpc_compression_algorithm algorithm);
extern grpc_compression_options_disable_algorithm_type grpc_compression_options_disable_algorithm_import;
#define grpc_compression_options_disable_algorithm grpc_compression_options_disable_algorithm_import
typedef int(*grpc_compression_options_is_algorithm_enabled_type)(const grpc_compression_options *opts, grpc_compression_algorithm algorithm);
extern grpc_compression_options_is_algorithm_enabled_type grpc_compression_options_is_algorithm_enabled_import;
#define grpc_compression_options_is_algorithm_enabled grpc_compression_options_is_algorithm_enabled_import
typedef void(*grpc_metadata_array_init_type)(grpc_metadata_array *array);
extern grpc_metadata_array_init_type grpc_metadata_array_init_import;
#define grpc_metadata_array_init grpc_metadata_array_init_import
typedef void(*grpc_metadata_array_destroy_type)(grpc_metadata_array *array);
extern grpc_metadata_array_destroy_type grpc_metadata_array_destroy_import;
#define grpc_metadata_array_destroy grpc_metadata_array_destroy_import
typedef void(*grpc_call_details_init_type)(grpc_call_details *details);
extern grpc_call_details_init_type grpc_call_details_init_import;
#define grpc_call_details_init grpc_call_details_init_import
typedef void(*grpc_call_details_destroy_type)(grpc_call_details *details);
extern grpc_call_details_destroy_type grpc_call_details_destroy_import;
#define grpc_call_details_destroy grpc_call_details_destroy_import
typedef void(*grpc_register_plugin_type)(void (*init)(void), void (*destroy)(void));
extern grpc_register_plugin_type grpc_register_plugin_import;
#define grpc_register_plugin grpc_register_plugin_import
typedef void(*grpc_init_type)(void);
extern grpc_init_type grpc_init_import;
#define grpc_init grpc_init_import
typedef void(*grpc_shutdown_type)(void);
extern grpc_shutdown_type grpc_shutdown_import;
#define grpc_shutdown grpc_shutdown_import
typedef const char *(*grpc_version_string_type)(void);
extern grpc_version_string_type grpc_version_string_import;
#define grpc_version_string grpc_version_string_import
typedef grpc_completion_queue *(*grpc_completion_queue_create_type)(void *reserved);
extern grpc_completion_queue_create_type grpc_completion_queue_create_import;
#define grpc_completion_queue_create grpc_completion_queue_create_import
typedef grpc_event(*grpc_completion_queue_next_type)(grpc_completion_queue *cq, gpr_timespec deadline, void *reserved);
extern grpc_completion_queue_next_type grpc_completion_queue_next_import;
#define grpc_completion_queue_next grpc_completion_queue_next_import
typedef grpc_event(*grpc_completion_queue_pluck_type)(grpc_completion_queue *cq, void *tag, gpr_timespec deadline, void *reserved);
extern grpc_completion_queue_pluck_type grpc_completion_queue_pluck_import;
#define grpc_completion_queue_pluck grpc_completion_queue_pluck_import
typedef void(*grpc_completion_queue_shutdown_type)(grpc_completion_queue *cq);
extern grpc_completion_queue_shutdown_type grpc_completion_queue_shutdown_import;
#define grpc_completion_queue_shutdown grpc_completion_queue_shutdown_import
typedef void(*grpc_completion_queue_destroy_type)(grpc_completion_queue *cq);
extern grpc_completion_queue_destroy_type grpc_completion_queue_destroy_import;
#define grpc_completion_queue_destroy grpc_completion_queue_destroy_import
typedef grpc_alarm *(*grpc_alarm_create_type)(grpc_completion_queue *cq, gpr_timespec deadline, void *tag);
extern grpc_alarm_create_type grpc_alarm_create_import;
#define grpc_alarm_create grpc_alarm_create_import
typedef void(*grpc_alarm_cancel_type)(grpc_alarm *alarm);
extern grpc_alarm_cancel_type grpc_alarm_cancel_import;
#define grpc_alarm_cancel grpc_alarm_cancel_import
typedef void(*grpc_alarm_destroy_type)(grpc_alarm *alarm);
extern grpc_alarm_destroy_type grpc_alarm_destroy_import;
#define grpc_alarm_destroy grpc_alarm_destroy_import
typedef grpc_connectivity_state(*grpc_channel_check_connectivity_state_type)(grpc_channel *channel, int try_to_connect);
extern grpc_channel_check_connectivity_state_type grpc_channel_check_connectivity_state_import;
#define grpc_channel_check_connectivity_state grpc_channel_check_connectivity_state_import
typedef void(*grpc_channel_watch_connectivity_state_type)(grpc_channel *channel, grpc_connectivity_state last_observed_state, gpr_timespec deadline, grpc_completion_queue *cq, void *tag);
extern grpc_channel_watch_connectivity_state_type grpc_channel_watch_connectivity_state_import;
#define grpc_channel_watch_connectivity_state grpc_channel_watch_connectivity_state_import
typedef grpc_call *(*grpc_channel_create_call_type)(grpc_channel *channel, grpc_call *parent_call, uint32_t propagation_mask, grpc_completion_queue *completion_queue, const char *method, const char *host, gpr_timespec deadline, void *reserved);
extern grpc_channel_create_call_type grpc_channel_create_call_import;
#define grpc_channel_create_call grpc_channel_create_call_import
typedef void(*grpc_channel_ping_type)(grpc_channel *channel, grpc_completion_queue *cq, void *tag, void *reserved);
extern grpc_channel_ping_type grpc_channel_ping_import;
#define grpc_channel_ping grpc_channel_ping_import
typedef void *(*grpc_channel_register_call_type)(grpc_channel *channel, const char *method, const char *host, void *reserved);
extern grpc_channel_register_call_type grpc_channel_register_call_import;
#define grpc_channel_register_call grpc_channel_register_call_import
typedef grpc_call *(*grpc_channel_create_registered_call_type)(grpc_channel *channel, grpc_call *parent_call, uint32_t propagation_mask, grpc_completion_queue *completion_queue, void *registered_call_handle, gpr_timespec deadline, void *reserved);
extern grpc_channel_create_registered_call_type grpc_channel_create_registered_call_import;
#define grpc_channel_create_registered_call grpc_channel_create_registered_call_import
typedef grpc_call_error(*grpc_call_start_batch_type)(grpc_call *call, const grpc_op *ops, size_t nops, void *tag, void *reserved);
extern grpc_call_start_batch_type grpc_call_start_batch_import;
#define grpc_call_start_batch grpc_call_start_batch_import
typedef char *(*grpc_call_get_peer_type)(grpc_call *call);
extern grpc_call_get_peer_type grpc_call_get_peer_import;
#define grpc_call_get_peer grpc_call_get_peer_import
typedef void(*grpc_census_call_set_context_type)(grpc_call *call, struct census_context *context);
extern grpc_census_call_set_context_type grpc_census_call_set_context_import;
#define grpc_census_call_set_context grpc_census_call_set_context_import
typedef struct census_context *(*grpc_census_call_get_context_type)(grpc_call *call);
extern grpc_census_call_get_context_type grpc_census_call_get_context_import;
#define grpc_census_call_get_context grpc_census_call_get_context_import
typedef char *(*grpc_channel_get_target_type)(grpc_channel *channel);
extern grpc_channel_get_target_type grpc_channel_get_target_import;
#define grpc_channel_get_target grpc_channel_get_target_import
typedef grpc_channel *(*grpc_insecure_channel_create_type)(const char *target, const grpc_channel_args *args, void *reserved);
extern grpc_insecure_channel_create_type grpc_insecure_channel_create_import;
#define grpc_insecure_channel_create grpc_insecure_channel_create_import
typedef grpc_channel *(*grpc_lame_client_channel_create_type)(const char *target, grpc_status_code error_code, const char *error_message);
extern grpc_lame_client_channel_create_type grpc_lame_client_channel_create_import;
#define grpc_lame_client_channel_create grpc_lame_client_channel_create_import
typedef void(*grpc_channel_destroy_type)(grpc_channel *channel);
extern grpc_channel_destroy_type grpc_channel_destroy_import;
#define grpc_channel_destroy grpc_channel_destroy_import
typedef grpc_call_error(*grpc_call_cancel_type)(grpc_call *call, void *reserved);
extern grpc_call_cancel_type grpc_call_cancel_import;
#define grpc_call_cancel grpc_call_cancel_import
typedef grpc_call_error(*grpc_call_cancel_with_status_type)(grpc_call *call, grpc_status_code status, const char *description, void *reserved);
extern grpc_call_cancel_with_status_type grpc_call_cancel_with_status_import;
#define grpc_call_cancel_with_status grpc_call_cancel_with_status_import
typedef void(*grpc_call_destroy_type)(grpc_call *call);
extern grpc_call_destroy_type grpc_call_destroy_import;
#define grpc_call_destroy grpc_call_destroy_import
typedef grpc_call_error(*grpc_server_request_call_type)(grpc_server *server, grpc_call **call, grpc_call_details *details, grpc_metadata_array *request_metadata, grpc_completion_queue *cq_bound_to_call, grpc_completion_queue *cq_for_notification, void *tag_new);
extern grpc_server_request_call_type grpc_server_request_call_import;
#define grpc_server_request_call grpc_server_request_call_import
typedef void *(*grpc_server_register_method_type)(grpc_server *server, const char *method, const char *host);
extern grpc_server_register_method_type grpc_server_register_method_import;
#define grpc_server_register_method grpc_server_register_method_import
typedef grpc_call_error(*grpc_server_request_registered_call_type)(grpc_server *server, void *registered_method, grpc_call **call, gpr_timespec *deadline, grpc_metadata_array *request_metadata, grpc_byte_buffer **optional_payload, grpc_completion_queue *cq_bound_to_call, grpc_completion_queue *cq_for_notification, void *tag_new);
extern grpc_server_request_registered_call_type grpc_server_request_registered_call_import;
#define grpc_server_request_registered_call grpc_server_request_registered_call_import
typedef grpc_server *(*grpc_server_create_type)(const grpc_channel_args *args, void *reserved);
extern grpc_server_create_type grpc_server_create_import;
#define grpc_server_create grpc_server_create_import
typedef void(*grpc_server_register_completion_queue_type)(grpc_server *server, grpc_completion_queue *cq, void *reserved);
extern grpc_server_register_completion_queue_type grpc_server_register_completion_queue_import;
#define grpc_server_register_completion_queue grpc_server_register_completion_queue_import
typedef int(*grpc_server_add_insecure_http2_port_type)(grpc_server *server, const char *addr);
extern grpc_server_add_insecure_http2_port_type grpc_server_add_insecure_http2_port_import;
#define grpc_server_add_insecure_http2_port grpc_server_add_insecure_http2_port_import
typedef void(*grpc_server_start_type)(grpc_server *server);
extern grpc_server_start_type grpc_server_start_import;
#define grpc_server_start grpc_server_start_import
typedef void(*grpc_server_shutdown_and_notify_type)(grpc_server *server, grpc_completion_queue *cq, void *tag);
extern grpc_server_shutdown_and_notify_type grpc_server_shutdown_and_notify_import;
#define grpc_server_shutdown_and_notify grpc_server_shutdown_and_notify_import
typedef void(*grpc_server_cancel_all_calls_type)(grpc_server *server);
extern grpc_server_cancel_all_calls_type grpc_server_cancel_all_calls_import;
#define grpc_server_cancel_all_calls grpc_server_cancel_all_calls_import
typedef void(*grpc_server_destroy_type)(grpc_server *server);
extern grpc_server_destroy_type grpc_server_destroy_import;
#define grpc_server_destroy grpc_server_destroy_import
typedef int(*grpc_tracer_set_enabled_type)(const char *name, int enabled);
extern grpc_tracer_set_enabled_type grpc_tracer_set_enabled_import;
#define grpc_tracer_set_enabled grpc_tracer_set_enabled_import
typedef int(*grpc_header_key_is_legal_type)(const char *key, size_t length);
extern grpc_header_key_is_legal_type grpc_header_key_is_legal_import;
#define grpc_header_key_is_legal grpc_header_key_is_legal_import
typedef int(*grpc_header_nonbin_value_is_legal_type)(const char *value, size_t length);
extern grpc_header_nonbin_value_is_legal_type grpc_header_nonbin_value_is_legal_import;
#define grpc_header_nonbin_value_is_legal grpc_header_nonbin_value_is_legal_import
typedef int(*grpc_is_binary_header_type)(const char *key, size_t length);
extern grpc_is_binary_header_type grpc_is_binary_header_import;
#define grpc_is_binary_header grpc_is_binary_header_import
typedef int(*census_initialize_type)(int features);
extern census_initialize_type census_initialize_import;
#define census_initialize census_initialize_import
typedef void(*census_shutdown_type)(void);
extern census_shutdown_type census_shutdown_import;
#define census_shutdown census_shutdown_import
typedef int(*census_supported_type)(void);
extern census_supported_type census_supported_import;
#define census_supported census_supported_import
typedef int(*census_enabled_type)(void);
extern census_enabled_type census_enabled_import;
#define census_enabled census_enabled_import
typedef size_t(*census_context_serialize_type)(const census_context *context, char *buffer, size_t buf_size);
extern census_context_serialize_type census_context_serialize_import;
#define census_context_serialize census_context_serialize_import
typedef int(*census_trace_mask_type)(const census_context *context);
extern census_trace_mask_type census_trace_mask_import;
#define census_trace_mask census_trace_mask_import
typedef void(*census_set_trace_mask_type)(int trace_mask);
extern census_set_trace_mask_type census_set_trace_mask_import;
#define census_set_trace_mask census_set_trace_mask_import
typedef census_timestamp(*census_start_rpc_op_timestamp_type)(void);
extern census_start_rpc_op_timestamp_type census_start_rpc_op_timestamp_import;
#define census_start_rpc_op_timestamp census_start_rpc_op_timestamp_import
typedef census_context *(*census_start_client_rpc_op_type)(const census_context *context, int64_t rpc_name_id, const census_rpc_name_info *rpc_name_info, const char *peer, int trace_mask, const census_timestamp *start_time);
extern census_start_client_rpc_op_type census_start_client_rpc_op_import;
#define census_start_client_rpc_op census_start_client_rpc_op_import
typedef void(*census_set_rpc_client_peer_type)(census_context *context, const char *peer);
extern census_set_rpc_client_peer_type census_set_rpc_client_peer_import;
#define census_set_rpc_client_peer census_set_rpc_client_peer_import
typedef census_context *(*census_start_server_rpc_op_type)(const char *buffer, int64_t rpc_name_id, const census_rpc_name_info *rpc_name_info, const char *peer, int trace_mask, census_timestamp *start_time);
extern census_start_server_rpc_op_type census_start_server_rpc_op_import;
#define census_start_server_rpc_op census_start_server_rpc_op_import
typedef census_context *(*census_start_op_type)(census_context *context, const char *family, const char *name, int trace_mask);
extern census_start_op_type census_start_op_import;
#define census_start_op census_start_op_import
typedef void(*census_end_op_type)(census_context *context, int status);
extern census_end_op_type census_end_op_import;
#define census_end_op census_end_op_import
typedef void(*census_trace_print_type)(census_context *context, uint32_t type, const char *buffer, size_t n);
extern census_trace_print_type census_trace_print_import;
#define census_trace_print census_trace_print_import
typedef int(*census_trace_scan_start_type)(int consume);
extern census_trace_scan_start_type census_trace_scan_start_import;
#define census_trace_scan_start census_trace_scan_start_import
typedef int(*census_get_trace_record_type)(census_trace_record *trace_record);
extern census_get_trace_record_type census_get_trace_record_import;
#define census_get_trace_record census_get_trace_record_import
typedef void(*census_trace_scan_end_type)();
extern census_trace_scan_end_type census_trace_scan_end_import;
#define census_trace_scan_end census_trace_scan_end_import
typedef census_tag_set *(*census_tag_set_create_type)(const census_tag_set *base, const census_tag *tags, int ntags, census_tag_set_create_status const **status);
extern census_tag_set_create_type census_tag_set_create_import;
#define census_tag_set_create census_tag_set_create_import
typedef void(*census_tag_set_destroy_type)(census_tag_set *tags);
extern census_tag_set_destroy_type census_tag_set_destroy_import;
#define census_tag_set_destroy census_tag_set_destroy_import
typedef const census_tag_set_create_status *(*census_tag_set_get_create_status_type)(const census_tag_set *tags);
extern census_tag_set_get_create_status_type census_tag_set_get_create_status_import;
#define census_tag_set_get_create_status census_tag_set_get_create_status_import
typedef void(*census_tag_set_initialize_iterator_type)(const census_tag_set *tags, census_tag_set_iterator *iterator);
extern census_tag_set_initialize_iterator_type census_tag_set_initialize_iterator_import;
#define census_tag_set_initialize_iterator census_tag_set_initialize_iterator_import
typedef int(*census_tag_set_next_tag_type)(census_tag_set_iterator *iterator, census_tag *tag);
extern census_tag_set_next_tag_type census_tag_set_next_tag_import;
#define census_tag_set_next_tag census_tag_set_next_tag_import
typedef int(*census_tag_set_get_tag_by_key_type)(const census_tag_set *tags, const char *key, census_tag *tag);
extern census_tag_set_get_tag_by_key_type census_tag_set_get_tag_by_key_import;
#define census_tag_set_get_tag_by_key census_tag_set_get_tag_by_key_import
typedef char *(*census_tag_set_encode_type)(const census_tag_set *tags, char *buffer, size_t buf_size, size_t *print_buf_size, size_t *bin_buf_size);
extern census_tag_set_encode_type census_tag_set_encode_import;
#define census_tag_set_encode census_tag_set_encode_import
typedef census_tag_set *(*census_tag_set_decode_type)(const char *buffer, size_t size, const char *bin_buffer, size_t bin_size);
extern census_tag_set_decode_type census_tag_set_decode_import;
#define census_tag_set_decode census_tag_set_decode_import
typedef census_tag_set *(*census_context_tag_set_type)(census_context *context);
extern census_context_tag_set_type census_context_tag_set_import;
#define census_context_tag_set census_context_tag_set_import
typedef void(*census_record_values_type)(census_context *context, census_value *values, size_t nvalues);
extern census_record_values_type census_record_values_import;
#define census_record_values census_record_values_import
typedef census_view *(*census_view_create_type)(uint32_t metric_id, const census_tag_set *tags, const census_aggregation *aggregations, size_t naggregations);
extern census_view_create_type census_view_create_import;
#define census_view_create census_view_create_import
typedef void(*census_view_delete_type)(census_view *view);
extern census_view_delete_type census_view_delete_import;
#define census_view_delete census_view_delete_import
typedef size_t(*census_view_metric_type)(const census_view *view);
extern census_view_metric_type census_view_metric_import;
#define census_view_metric census_view_metric_import
typedef size_t(*census_view_naggregations_type)(const census_view *view);
extern census_view_naggregations_type census_view_naggregations_import;
#define census_view_naggregations census_view_naggregations_import
typedef const census_tag_set *(*census_view_tags_type)(const census_view *view);
extern census_view_tags_type census_view_tags_import;
#define census_view_tags census_view_tags_import
typedef const census_aggregation *(*census_view_aggregrations_type)(const census_view *view);
extern census_view_aggregrations_type census_view_aggregrations_import;
#define census_view_aggregrations census_view_aggregrations_import
typedef const census_view_data *(*census_view_get_data_type)(const census_view *view);
extern census_view_get_data_type census_view_get_data_import;
#define census_view_get_data census_view_get_data_import
typedef void(*census_view_reset_type)(census_view *view);
extern census_view_reset_type census_view_reset_import;
#define census_view_reset census_view_reset_import

int grpc_rb_load_imports(const wchar_t *filename);

#endif
