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

#include "rb_grpc_imports.h"  

grpc_auth_property_iterator_next_type grpc_auth_property_iterator_next_import;
grpc_auth_context_property_iterator_type grpc_auth_context_property_iterator_import;
grpc_auth_context_peer_identity_type grpc_auth_context_peer_identity_import;
grpc_auth_context_find_properties_by_name_type grpc_auth_context_find_properties_by_name_import;
grpc_auth_context_peer_identity_property_name_type grpc_auth_context_peer_identity_property_name_import;
grpc_auth_context_peer_is_authenticated_type grpc_auth_context_peer_is_authenticated_import;
grpc_call_auth_context_type grpc_call_auth_context_import;
grpc_auth_context_release_type grpc_auth_context_release_import;
grpc_auth_context_add_property_type grpc_auth_context_add_property_import;
grpc_auth_context_add_cstring_property_type grpc_auth_context_add_cstring_property_import;
grpc_auth_context_set_peer_identity_property_name_type grpc_auth_context_set_peer_identity_property_name_import;
grpc_channel_credentials_release_type grpc_channel_credentials_release_import;
grpc_google_default_credentials_create_type grpc_google_default_credentials_create_import;
grpc_ssl_credentials_create_type grpc_ssl_credentials_create_import;
grpc_call_credentials_release_type grpc_call_credentials_release_import;
grpc_composite_channel_credentials_create_type grpc_composite_channel_credentials_create_import;
grpc_composite_call_credentials_create_type grpc_composite_call_credentials_create_import;
grpc_google_compute_engine_credentials_create_type grpc_google_compute_engine_credentials_create_import;
grpc_service_account_jwt_access_credentials_create_type grpc_service_account_jwt_access_credentials_create_import;
grpc_google_refresh_token_credentials_create_type grpc_google_refresh_token_credentials_create_import;
grpc_access_token_credentials_create_type grpc_access_token_credentials_create_import;
grpc_google_iam_credentials_create_type grpc_google_iam_credentials_create_import;
grpc_metadata_credentials_create_from_plugin_type grpc_metadata_credentials_create_from_plugin_import;
grpc_secure_channel_create_type grpc_secure_channel_create_import;
grpc_server_credentials_release_type grpc_server_credentials_release_import;
grpc_ssl_server_credentials_create_type grpc_ssl_server_credentials_create_import;
grpc_server_add_secure_http2_port_type grpc_server_add_secure_http2_port_import;
grpc_call_set_credentials_type grpc_call_set_credentials_import;
grpc_server_credentials_set_auth_metadata_processor_type grpc_server_credentials_set_auth_metadata_processor_import;
grpc_compression_algorithm_parse_type grpc_compression_algorithm_parse_import;
grpc_compression_algorithm_name_type grpc_compression_algorithm_name_import;
grpc_compression_algorithm_for_level_type grpc_compression_algorithm_for_level_import;
grpc_compression_options_init_type grpc_compression_options_init_import;
grpc_compression_options_enable_algorithm_type grpc_compression_options_enable_algorithm_import;
grpc_compression_options_disable_algorithm_type grpc_compression_options_disable_algorithm_import;
grpc_compression_options_is_algorithm_enabled_type grpc_compression_options_is_algorithm_enabled_import;
grpc_metadata_array_init_type grpc_metadata_array_init_import;
grpc_metadata_array_destroy_type grpc_metadata_array_destroy_import;
grpc_call_details_init_type grpc_call_details_init_import;
grpc_call_details_destroy_type grpc_call_details_destroy_import;
grpc_register_plugin_type grpc_register_plugin_import;
grpc_init_type grpc_init_import;
grpc_shutdown_type grpc_shutdown_import;
grpc_version_string_type grpc_version_string_import;
grpc_completion_queue_create_type grpc_completion_queue_create_import;
grpc_completion_queue_next_type grpc_completion_queue_next_import;
grpc_completion_queue_pluck_type grpc_completion_queue_pluck_import;
grpc_completion_queue_shutdown_type grpc_completion_queue_shutdown_import;
grpc_completion_queue_destroy_type grpc_completion_queue_destroy_import;
grpc_alarm_create_type grpc_alarm_create_import;
grpc_alarm_cancel_type grpc_alarm_cancel_import;
grpc_alarm_destroy_type grpc_alarm_destroy_import;
grpc_channel_check_connectivity_state_type grpc_channel_check_connectivity_state_import;
grpc_channel_watch_connectivity_state_type grpc_channel_watch_connectivity_state_import;
grpc_channel_create_call_type grpc_channel_create_call_import;
grpc_channel_ping_type grpc_channel_ping_import;
grpc_channel_register_call_type grpc_channel_register_call_import;
grpc_channel_create_registered_call_type grpc_channel_create_registered_call_import;
grpc_call_start_batch_type grpc_call_start_batch_import;
grpc_call_get_peer_type grpc_call_get_peer_import;
grpc_census_call_set_context_type grpc_census_call_set_context_import;
grpc_census_call_get_context_type grpc_census_call_get_context_import;
grpc_channel_get_target_type grpc_channel_get_target_import;
grpc_insecure_channel_create_type grpc_insecure_channel_create_import;
grpc_lame_client_channel_create_type grpc_lame_client_channel_create_import;
grpc_channel_destroy_type grpc_channel_destroy_import;
grpc_call_cancel_type grpc_call_cancel_import;
grpc_call_cancel_with_status_type grpc_call_cancel_with_status_import;
grpc_call_destroy_type grpc_call_destroy_import;
grpc_server_request_call_type grpc_server_request_call_import;
grpc_server_register_method_type grpc_server_register_method_import;
grpc_server_request_registered_call_type grpc_server_request_registered_call_import;
grpc_server_create_type grpc_server_create_import;
grpc_server_register_completion_queue_type grpc_server_register_completion_queue_import;
grpc_server_add_insecure_http2_port_type grpc_server_add_insecure_http2_port_import;
grpc_server_start_type grpc_server_start_import;
grpc_server_shutdown_and_notify_type grpc_server_shutdown_and_notify_import;
grpc_server_cancel_all_calls_type grpc_server_cancel_all_calls_import;
grpc_server_destroy_type grpc_server_destroy_import;
grpc_tracer_set_enabled_type grpc_tracer_set_enabled_import;
grpc_header_key_is_legal_type grpc_header_key_is_legal_import;
grpc_header_nonbin_value_is_legal_type grpc_header_nonbin_value_is_legal_import;
grpc_is_binary_header_type grpc_is_binary_header_import;
census_initialize_type census_initialize_import;
census_shutdown_type census_shutdown_import;
census_supported_type census_supported_import;
census_enabled_type census_enabled_import;
census_context_serialize_type census_context_serialize_import;
census_trace_mask_type census_trace_mask_import;
census_set_trace_mask_type census_set_trace_mask_import;
census_start_rpc_op_timestamp_type census_start_rpc_op_timestamp_import;
census_start_client_rpc_op_type census_start_client_rpc_op_import;
census_set_rpc_client_peer_type census_set_rpc_client_peer_import;
census_start_server_rpc_op_type census_start_server_rpc_op_import;
census_start_op_type census_start_op_import;
census_end_op_type census_end_op_import;
census_trace_print_type census_trace_print_import;
census_trace_scan_start_type census_trace_scan_start_import;
census_get_trace_record_type census_get_trace_record_import;
census_trace_scan_end_type census_trace_scan_end_import;
census_tag_set_create_type census_tag_set_create_import;
census_tag_set_destroy_type census_tag_set_destroy_import;
census_tag_set_get_create_status_type census_tag_set_get_create_status_import;
census_tag_set_initialize_iterator_type census_tag_set_initialize_iterator_import;
census_tag_set_next_tag_type census_tag_set_next_tag_import;
census_tag_set_get_tag_by_key_type census_tag_set_get_tag_by_key_import;
census_tag_set_encode_type census_tag_set_encode_import;
census_tag_set_decode_type census_tag_set_decode_import;
census_context_tag_set_type census_context_tag_set_import;
census_record_values_type census_record_values_import;
census_view_create_type census_view_create_import;
census_view_delete_type census_view_delete_import;
census_view_metric_type census_view_metric_import;
census_view_naggregations_type census_view_naggregations_import;
census_view_tags_type census_view_tags_import;
census_view_aggregrations_type census_view_aggregrations_import;
census_view_get_data_type census_view_get_data_import;
census_view_reset_type census_view_reset_import;

int grpc_rb_load_imports(const wchar_t *filename) {
  HMODULE hm = LoadLibrary(filename);
  if (hm == NULL) return 0;
  grpc_auth_property_iterator_next_import = (grpc_auth_property_iterator_next_type) GetProcAddress(hm, "grpc_auth_property_iterator_next");
  grpc_auth_context_property_iterator_import = (grpc_auth_context_property_iterator_type) GetProcAddress(hm, "grpc_auth_context_property_iterator");
  grpc_auth_context_peer_identity_import = (grpc_auth_context_peer_identity_type) GetProcAddress(hm, "grpc_auth_context_peer_identity");
  grpc_auth_context_find_properties_by_name_import = (grpc_auth_context_find_properties_by_name_type) GetProcAddress(hm, "grpc_auth_context_find_properties_by_name");
  grpc_auth_context_peer_identity_property_name_import = (grpc_auth_context_peer_identity_property_name_type) GetProcAddress(hm, "grpc_auth_context_peer_identity_property_name");
  grpc_auth_context_peer_is_authenticated_import = (grpc_auth_context_peer_is_authenticated_type) GetProcAddress(hm, "grpc_auth_context_peer_is_authenticated");
  grpc_call_auth_context_import = (grpc_call_auth_context_type) GetProcAddress(hm, "grpc_call_auth_context");
  grpc_auth_context_release_import = (grpc_auth_context_release_type) GetProcAddress(hm, "grpc_auth_context_release");
  grpc_auth_context_add_property_import = (grpc_auth_context_add_property_type) GetProcAddress(hm, "grpc_auth_context_add_property");
  grpc_auth_context_add_cstring_property_import = (grpc_auth_context_add_cstring_property_type) GetProcAddress(hm, "grpc_auth_context_add_cstring_property");
  grpc_auth_context_set_peer_identity_property_name_import = (grpc_auth_context_set_peer_identity_property_name_type) GetProcAddress(hm, "grpc_auth_context_set_peer_identity_property_name");
  grpc_channel_credentials_release_import = (grpc_channel_credentials_release_type) GetProcAddress(hm, "grpc_channel_credentials_release");
  grpc_google_default_credentials_create_import = (grpc_google_default_credentials_create_type) GetProcAddress(hm, "grpc_google_default_credentials_create");
  grpc_ssl_credentials_create_import = (grpc_ssl_credentials_create_type) GetProcAddress(hm, "grpc_ssl_credentials_create");
  grpc_call_credentials_release_import = (grpc_call_credentials_release_type) GetProcAddress(hm, "grpc_call_credentials_release");
  grpc_composite_channel_credentials_create_import = (grpc_composite_channel_credentials_create_type) GetProcAddress(hm, "grpc_composite_channel_credentials_create");
  grpc_composite_call_credentials_create_import = (grpc_composite_call_credentials_create_type) GetProcAddress(hm, "grpc_composite_call_credentials_create");
  grpc_google_compute_engine_credentials_create_import = (grpc_google_compute_engine_credentials_create_type) GetProcAddress(hm, "grpc_google_compute_engine_credentials_create");
  grpc_service_account_jwt_access_credentials_create_import = (grpc_service_account_jwt_access_credentials_create_type) GetProcAddress(hm, "grpc_service_account_jwt_access_credentials_create");
  grpc_google_refresh_token_credentials_create_import = (grpc_google_refresh_token_credentials_create_type) GetProcAddress(hm, "grpc_google_refresh_token_credentials_create");
  grpc_access_token_credentials_create_import = (grpc_access_token_credentials_create_type) GetProcAddress(hm, "grpc_access_token_credentials_create");
  grpc_google_iam_credentials_create_import = (grpc_google_iam_credentials_create_type) GetProcAddress(hm, "grpc_google_iam_credentials_create");
  grpc_metadata_credentials_create_from_plugin_import = (grpc_metadata_credentials_create_from_plugin_type) GetProcAddress(hm, "grpc_metadata_credentials_create_from_plugin");
  grpc_secure_channel_create_import = (grpc_secure_channel_create_type) GetProcAddress(hm, "grpc_secure_channel_create");
  grpc_server_credentials_release_import = (grpc_server_credentials_release_type) GetProcAddress(hm, "grpc_server_credentials_release");
  grpc_ssl_server_credentials_create_import = (grpc_ssl_server_credentials_create_type) GetProcAddress(hm, "grpc_ssl_server_credentials_create");
  grpc_server_add_secure_http2_port_import = (grpc_server_add_secure_http2_port_type) GetProcAddress(hm, "grpc_server_add_secure_http2_port");
  grpc_call_set_credentials_import = (grpc_call_set_credentials_type) GetProcAddress(hm, "grpc_call_set_credentials");
  grpc_server_credentials_set_auth_metadata_processor_import = (grpc_server_credentials_set_auth_metadata_processor_type) GetProcAddress(hm, "grpc_server_credentials_set_auth_metadata_processor");
  grpc_compression_algorithm_parse_import = (grpc_compression_algorithm_parse_type) GetProcAddress(hm, "grpc_compression_algorithm_parse");
  grpc_compression_algorithm_name_import = (grpc_compression_algorithm_name_type) GetProcAddress(hm, "grpc_compression_algorithm_name");
  grpc_compression_algorithm_for_level_import = (grpc_compression_algorithm_for_level_type) GetProcAddress(hm, "grpc_compression_algorithm_for_level");
  grpc_compression_options_init_import = (grpc_compression_options_init_type) GetProcAddress(hm, "grpc_compression_options_init");
  grpc_compression_options_enable_algorithm_import = (grpc_compression_options_enable_algorithm_type) GetProcAddress(hm, "grpc_compression_options_enable_algorithm");
  grpc_compression_options_disable_algorithm_import = (grpc_compression_options_disable_algorithm_type) GetProcAddress(hm, "grpc_compression_options_disable_algorithm");
  grpc_compression_options_is_algorithm_enabled_import = (grpc_compression_options_is_algorithm_enabled_type) GetProcAddress(hm, "grpc_compression_options_is_algorithm_enabled");
  grpc_metadata_array_init_import = (grpc_metadata_array_init_type) GetProcAddress(hm, "grpc_metadata_array_init");
  grpc_metadata_array_destroy_import = (grpc_metadata_array_destroy_type) GetProcAddress(hm, "grpc_metadata_array_destroy");
  grpc_call_details_init_import = (grpc_call_details_init_type) GetProcAddress(hm, "grpc_call_details_init");
  grpc_call_details_destroy_import = (grpc_call_details_destroy_type) GetProcAddress(hm, "grpc_call_details_destroy");
  grpc_register_plugin_import = (grpc_register_plugin_type) GetProcAddress(hm, "grpc_register_plugin");
  grpc_init_import = (grpc_init_type) GetProcAddress(hm, "grpc_init");
  grpc_shutdown_import = (grpc_shutdown_type) GetProcAddress(hm, "grpc_shutdown");
  grpc_version_string_import = (grpc_version_string_type) GetProcAddress(hm, "grpc_version_string");
  grpc_completion_queue_create_import = (grpc_completion_queue_create_type) GetProcAddress(hm, "grpc_completion_queue_create");
  grpc_completion_queue_next_import = (grpc_completion_queue_next_type) GetProcAddress(hm, "grpc_completion_queue_next");
  grpc_completion_queue_pluck_import = (grpc_completion_queue_pluck_type) GetProcAddress(hm, "grpc_completion_queue_pluck");
  grpc_completion_queue_shutdown_import = (grpc_completion_queue_shutdown_type) GetProcAddress(hm, "grpc_completion_queue_shutdown");
  grpc_completion_queue_destroy_import = (grpc_completion_queue_destroy_type) GetProcAddress(hm, "grpc_completion_queue_destroy");
  grpc_alarm_create_import = (grpc_alarm_create_type) GetProcAddress(hm, "grpc_alarm_create");
  grpc_alarm_cancel_import = (grpc_alarm_cancel_type) GetProcAddress(hm, "grpc_alarm_cancel");
  grpc_alarm_destroy_import = (grpc_alarm_destroy_type) GetProcAddress(hm, "grpc_alarm_destroy");
  grpc_channel_check_connectivity_state_import = (grpc_channel_check_connectivity_state_type) GetProcAddress(hm, "grpc_channel_check_connectivity_state");
  grpc_channel_watch_connectivity_state_import = (grpc_channel_watch_connectivity_state_type) GetProcAddress(hm, "grpc_channel_watch_connectivity_state");
  grpc_channel_create_call_import = (grpc_channel_create_call_type) GetProcAddress(hm, "grpc_channel_create_call");
  grpc_channel_ping_import = (grpc_channel_ping_type) GetProcAddress(hm, "grpc_channel_ping");
  grpc_channel_register_call_import = (grpc_channel_register_call_type) GetProcAddress(hm, "grpc_channel_register_call");
  grpc_channel_create_registered_call_import = (grpc_channel_create_registered_call_type) GetProcAddress(hm, "grpc_channel_create_registered_call");
  grpc_call_start_batch_import = (grpc_call_start_batch_type) GetProcAddress(hm, "grpc_call_start_batch");
  grpc_call_get_peer_import = (grpc_call_get_peer_type) GetProcAddress(hm, "grpc_call_get_peer");
  grpc_census_call_set_context_import = (grpc_census_call_set_context_type) GetProcAddress(hm, "grpc_census_call_set_context");
  grpc_census_call_get_context_import = (grpc_census_call_get_context_type) GetProcAddress(hm, "grpc_census_call_get_context");
  grpc_channel_get_target_import = (grpc_channel_get_target_type) GetProcAddress(hm, "grpc_channel_get_target");
  grpc_insecure_channel_create_import = (grpc_insecure_channel_create_type) GetProcAddress(hm, "grpc_insecure_channel_create");
  grpc_lame_client_channel_create_import = (grpc_lame_client_channel_create_type) GetProcAddress(hm, "grpc_lame_client_channel_create");
  grpc_channel_destroy_import = (grpc_channel_destroy_type) GetProcAddress(hm, "grpc_channel_destroy");
  grpc_call_cancel_import = (grpc_call_cancel_type) GetProcAddress(hm, "grpc_call_cancel");
  grpc_call_cancel_with_status_import = (grpc_call_cancel_with_status_type) GetProcAddress(hm, "grpc_call_cancel_with_status");
  grpc_call_destroy_import = (grpc_call_destroy_type) GetProcAddress(hm, "grpc_call_destroy");
  grpc_server_request_call_import = (grpc_server_request_call_type) GetProcAddress(hm, "grpc_server_request_call");
  grpc_server_register_method_import = (grpc_server_register_method_type) GetProcAddress(hm, "grpc_server_register_method");
  grpc_server_request_registered_call_import = (grpc_server_request_registered_call_type) GetProcAddress(hm, "grpc_server_request_registered_call");
  grpc_server_create_import = (grpc_server_create_type) GetProcAddress(hm, "grpc_server_create");
  grpc_server_register_completion_queue_import = (grpc_server_register_completion_queue_type) GetProcAddress(hm, "grpc_server_register_completion_queue");
  grpc_server_add_insecure_http2_port_import = (grpc_server_add_insecure_http2_port_type) GetProcAddress(hm, "grpc_server_add_insecure_http2_port");
  grpc_server_start_import = (grpc_server_start_type) GetProcAddress(hm, "grpc_server_start");
  grpc_server_shutdown_and_notify_import = (grpc_server_shutdown_and_notify_type) GetProcAddress(hm, "grpc_server_shutdown_and_notify");
  grpc_server_cancel_all_calls_import = (grpc_server_cancel_all_calls_type) GetProcAddress(hm, "grpc_server_cancel_all_calls");
  grpc_server_destroy_import = (grpc_server_destroy_type) GetProcAddress(hm, "grpc_server_destroy");
  grpc_tracer_set_enabled_import = (grpc_tracer_set_enabled_type) GetProcAddress(hm, "grpc_tracer_set_enabled");
  grpc_header_key_is_legal_import = (grpc_header_key_is_legal_type) GetProcAddress(hm, "grpc_header_key_is_legal");
  grpc_header_nonbin_value_is_legal_import = (grpc_header_nonbin_value_is_legal_type) GetProcAddress(hm, "grpc_header_nonbin_value_is_legal");
  grpc_is_binary_header_import = (grpc_is_binary_header_type) GetProcAddress(hm, "grpc_is_binary_header");
  census_initialize_import = (census_initialize_type) GetProcAddress(hm, "census_initialize");
  census_shutdown_import = (census_shutdown_type) GetProcAddress(hm, "census_shutdown");
  census_supported_import = (census_supported_type) GetProcAddress(hm, "census_supported");
  census_enabled_import = (census_enabled_type) GetProcAddress(hm, "census_enabled");
  census_context_serialize_import = (census_context_serialize_type) GetProcAddress(hm, "census_context_serialize");
  census_trace_mask_import = (census_trace_mask_type) GetProcAddress(hm, "census_trace_mask");
  census_set_trace_mask_import = (census_set_trace_mask_type) GetProcAddress(hm, "census_set_trace_mask");
  census_start_rpc_op_timestamp_import = (census_start_rpc_op_timestamp_type) GetProcAddress(hm, "census_start_rpc_op_timestamp");
  census_start_client_rpc_op_import = (census_start_client_rpc_op_type) GetProcAddress(hm, "census_start_client_rpc_op");
  census_set_rpc_client_peer_import = (census_set_rpc_client_peer_type) GetProcAddress(hm, "census_set_rpc_client_peer");
  census_start_server_rpc_op_import = (census_start_server_rpc_op_type) GetProcAddress(hm, "census_start_server_rpc_op");
  census_start_op_import = (census_start_op_type) GetProcAddress(hm, "census_start_op");
  census_end_op_import = (census_end_op_type) GetProcAddress(hm, "census_end_op");
  census_trace_print_import = (census_trace_print_type) GetProcAddress(hm, "census_trace_print");
  census_trace_scan_start_import = (census_trace_scan_start_type) GetProcAddress(hm, "census_trace_scan_start");
  census_get_trace_record_import = (census_get_trace_record_type) GetProcAddress(hm, "census_get_trace_record");
  census_trace_scan_end_import = (census_trace_scan_end_type) GetProcAddress(hm, "census_trace_scan_end");
  census_tag_set_create_import = (census_tag_set_create_type) GetProcAddress(hm, "census_tag_set_create");
  census_tag_set_destroy_import = (census_tag_set_destroy_type) GetProcAddress(hm, "census_tag_set_destroy");
  census_tag_set_get_create_status_import = (census_tag_set_get_create_status_type) GetProcAddress(hm, "census_tag_set_get_create_status");
  census_tag_set_initialize_iterator_import = (census_tag_set_initialize_iterator_type) GetProcAddress(hm, "census_tag_set_initialize_iterator");
  census_tag_set_next_tag_import = (census_tag_set_next_tag_type) GetProcAddress(hm, "census_tag_set_next_tag");
  census_tag_set_get_tag_by_key_import = (census_tag_set_get_tag_by_key_type) GetProcAddress(hm, "census_tag_set_get_tag_by_key");
  census_tag_set_encode_import = (census_tag_set_encode_type) GetProcAddress(hm, "census_tag_set_encode");
  census_tag_set_decode_import = (census_tag_set_decode_type) GetProcAddress(hm, "census_tag_set_decode");
  census_context_tag_set_import = (census_context_tag_set_type) GetProcAddress(hm, "census_context_tag_set");
  census_record_values_import = (census_record_values_type) GetProcAddress(hm, "census_record_values");
  census_view_create_import = (census_view_create_type) GetProcAddress(hm, "census_view_create");
  census_view_delete_import = (census_view_delete_type) GetProcAddress(hm, "census_view_delete");
  census_view_metric_import = (census_view_metric_type) GetProcAddress(hm, "census_view_metric");
  census_view_naggregations_import = (census_view_naggregations_type) GetProcAddress(hm, "census_view_naggregations");
  census_view_tags_import = (census_view_tags_type) GetProcAddress(hm, "census_view_tags");
  census_view_aggregrations_import = (census_view_aggregrations_type) GetProcAddress(hm, "census_view_aggregrations");
  census_view_get_data_import = (census_view_get_data_type) GetProcAddress(hm, "census_view_get_data");
  census_view_reset_import = (census_view_reset_type) GetProcAddress(hm, "census_view_reset");
}

#endif
