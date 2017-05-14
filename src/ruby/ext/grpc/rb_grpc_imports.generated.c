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

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS

#include "rb_grpc_imports.generated.h"

census_initialize_type census_initialize_import;
census_shutdown_type census_shutdown_import;
census_supported_type census_supported_import;
census_enabled_type census_enabled_import;
census_context_create_type census_context_create_import;
census_context_destroy_type census_context_destroy_import;
census_context_get_status_type census_context_get_status_import;
census_context_initialize_iterator_type census_context_initialize_iterator_import;
census_context_next_tag_type census_context_next_tag_import;
census_context_get_tag_type census_context_get_tag_import;
census_context_encode_type census_context_encode_import;
census_context_decode_type census_context_decode_import;
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
census_define_resource_type census_define_resource_import;
census_delete_resource_type census_delete_resource_import;
census_resource_id_type census_resource_id_import;
census_record_values_type census_record_values_import;
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
grpc_set_ssl_roots_override_callback_type grpc_set_ssl_roots_override_callback_import;
grpc_ssl_credentials_create_type grpc_ssl_credentials_create_import;
grpc_call_credentials_release_type grpc_call_credentials_release_import;
grpc_composite_channel_credentials_create_type grpc_composite_channel_credentials_create_import;
grpc_composite_call_credentials_create_type grpc_composite_call_credentials_create_import;
grpc_google_compute_engine_credentials_create_type grpc_google_compute_engine_credentials_create_import;
grpc_max_auth_token_lifetime_type grpc_max_auth_token_lifetime_import;
grpc_service_account_jwt_access_credentials_create_type grpc_service_account_jwt_access_credentials_create_import;
grpc_google_refresh_token_credentials_create_type grpc_google_refresh_token_credentials_create_import;
grpc_access_token_credentials_create_type grpc_access_token_credentials_create_import;
grpc_google_iam_credentials_create_type grpc_google_iam_credentials_create_import;
grpc_metadata_credentials_create_from_plugin_type grpc_metadata_credentials_create_from_plugin_import;
grpc_secure_channel_create_type grpc_secure_channel_create_import;
grpc_server_credentials_release_type grpc_server_credentials_release_import;
grpc_ssl_server_credentials_create_type grpc_ssl_server_credentials_create_import;
grpc_ssl_server_credentials_create_ex_type grpc_ssl_server_credentials_create_ex_import;
grpc_server_add_secure_http2_port_type grpc_server_add_secure_http2_port_import;
grpc_call_set_credentials_type grpc_call_set_credentials_import;
grpc_server_credentials_set_auth_metadata_processor_type grpc_server_credentials_set_auth_metadata_processor_import;
gpr_malloc_type gpr_malloc_import;
gpr_zalloc_type gpr_zalloc_import;
gpr_free_type gpr_free_import;
gpr_realloc_type gpr_realloc_import;
gpr_malloc_aligned_type gpr_malloc_aligned_import;
gpr_free_aligned_type gpr_free_aligned_import;
gpr_set_allocation_functions_type gpr_set_allocation_functions_import;
gpr_get_allocation_functions_type gpr_get_allocation_functions_import;
gpr_avl_create_type gpr_avl_create_import;
gpr_avl_ref_type gpr_avl_ref_import;
gpr_avl_unref_type gpr_avl_unref_import;
gpr_avl_add_type gpr_avl_add_import;
gpr_avl_remove_type gpr_avl_remove_import;
gpr_avl_get_type gpr_avl_get_import;
gpr_avl_maybe_get_type gpr_avl_maybe_get_import;
gpr_avl_is_empty_type gpr_avl_is_empty_import;
gpr_cmdline_create_type gpr_cmdline_create_import;
gpr_cmdline_add_int_type gpr_cmdline_add_int_import;
gpr_cmdline_add_flag_type gpr_cmdline_add_flag_import;
gpr_cmdline_add_string_type gpr_cmdline_add_string_import;
gpr_cmdline_on_extra_arg_type gpr_cmdline_on_extra_arg_import;
gpr_cmdline_set_survive_failure_type gpr_cmdline_set_survive_failure_import;
gpr_cmdline_parse_type gpr_cmdline_parse_import;
gpr_cmdline_destroy_type gpr_cmdline_destroy_import;
gpr_cmdline_usage_string_type gpr_cmdline_usage_string_import;
gpr_cpu_num_cores_type gpr_cpu_num_cores_import;
gpr_cpu_current_cpu_type gpr_cpu_current_cpu_import;
gpr_histogram_create_type gpr_histogram_create_import;
gpr_histogram_destroy_type gpr_histogram_destroy_import;
gpr_histogram_add_type gpr_histogram_add_import;
gpr_histogram_merge_type gpr_histogram_merge_import;
gpr_histogram_percentile_type gpr_histogram_percentile_import;
gpr_histogram_mean_type gpr_histogram_mean_import;
gpr_histogram_stddev_type gpr_histogram_stddev_import;
gpr_histogram_variance_type gpr_histogram_variance_import;
gpr_histogram_maximum_type gpr_histogram_maximum_import;
gpr_histogram_minimum_type gpr_histogram_minimum_import;
gpr_histogram_count_type gpr_histogram_count_import;
gpr_histogram_sum_type gpr_histogram_sum_import;
gpr_histogram_sum_of_squares_type gpr_histogram_sum_of_squares_import;
gpr_histogram_get_contents_type gpr_histogram_get_contents_import;
gpr_histogram_merge_contents_type gpr_histogram_merge_contents_import;
gpr_join_host_port_type gpr_join_host_port_import;
gpr_split_host_port_type gpr_split_host_port_import;
gpr_log_type gpr_log_import;
gpr_log_message_type gpr_log_message_import;
gpr_set_log_verbosity_type gpr_set_log_verbosity_import;
gpr_log_verbosity_init_type gpr_log_verbosity_init_import;
gpr_set_log_function_type gpr_set_log_function_import;
gpr_format_message_type gpr_format_message_import;
gpr_strdup_type gpr_strdup_import;
gpr_asprintf_type gpr_asprintf_import;
gpr_subprocess_binary_extension_type gpr_subprocess_binary_extension_import;
gpr_subprocess_create_type gpr_subprocess_create_import;
gpr_subprocess_destroy_type gpr_subprocess_destroy_import;
gpr_subprocess_join_type gpr_subprocess_join_import;
gpr_subprocess_interrupt_type gpr_subprocess_interrupt_import;
gpr_mu_init_type gpr_mu_init_import;
gpr_mu_destroy_type gpr_mu_destroy_import;
gpr_mu_lock_type gpr_mu_lock_import;
gpr_mu_unlock_type gpr_mu_unlock_import;
gpr_mu_trylock_type gpr_mu_trylock_import;
gpr_cv_init_type gpr_cv_init_import;
gpr_cv_destroy_type gpr_cv_destroy_import;
gpr_cv_wait_type gpr_cv_wait_import;
gpr_cv_signal_type gpr_cv_signal_import;
gpr_cv_broadcast_type gpr_cv_broadcast_import;
gpr_once_init_type gpr_once_init_import;
gpr_event_init_type gpr_event_init_import;
gpr_event_set_type gpr_event_set_import;
gpr_event_get_type gpr_event_get_import;
gpr_event_wait_type gpr_event_wait_import;
gpr_ref_init_type gpr_ref_init_import;
gpr_ref_type gpr_ref_import;
gpr_ref_non_zero_type gpr_ref_non_zero_import;
gpr_refn_type gpr_refn_import;
gpr_unref_type gpr_unref_import;
gpr_ref_is_unique_type gpr_ref_is_unique_import;
gpr_stats_init_type gpr_stats_init_import;
gpr_stats_inc_type gpr_stats_inc_import;
gpr_stats_read_type gpr_stats_read_import;
gpr_thd_new_type gpr_thd_new_import;
gpr_thd_options_default_type gpr_thd_options_default_import;
gpr_thd_options_set_detached_type gpr_thd_options_set_detached_import;
gpr_thd_options_set_joinable_type gpr_thd_options_set_joinable_import;
gpr_thd_options_is_detached_type gpr_thd_options_is_detached_import;
gpr_thd_options_is_joinable_type gpr_thd_options_is_joinable_import;
gpr_thd_currentid_type gpr_thd_currentid_import;
gpr_thd_join_type gpr_thd_join_import;
gpr_time_0_type gpr_time_0_import;
gpr_inf_future_type gpr_inf_future_import;
gpr_inf_past_type gpr_inf_past_import;
gpr_time_init_type gpr_time_init_import;
gpr_now_type gpr_now_import;
gpr_convert_clock_type_type gpr_convert_clock_type_import;
gpr_time_cmp_type gpr_time_cmp_import;
gpr_time_max_type gpr_time_max_import;
gpr_time_min_type gpr_time_min_import;
gpr_time_add_type gpr_time_add_import;
gpr_time_sub_type gpr_time_sub_import;
gpr_time_from_micros_type gpr_time_from_micros_import;
gpr_time_from_nanos_type gpr_time_from_nanos_import;
gpr_time_from_millis_type gpr_time_from_millis_import;
gpr_time_from_seconds_type gpr_time_from_seconds_import;
gpr_time_from_minutes_type gpr_time_from_minutes_import;
gpr_time_from_hours_type gpr_time_from_hours_import;
gpr_time_to_millis_type gpr_time_to_millis_import;
gpr_time_similar_type gpr_time_similar_import;
gpr_sleep_until_type gpr_sleep_until_import;
gpr_timespec_to_micros_type gpr_timespec_to_micros_import;

void grpc_rb_load_imports(HMODULE library) {
  census_initialize_import = (census_initialize_type) GetProcAddress(library, "census_initialize");
  census_shutdown_import = (census_shutdown_type) GetProcAddress(library, "census_shutdown");
  census_supported_import = (census_supported_type) GetProcAddress(library, "census_supported");
  census_enabled_import = (census_enabled_type) GetProcAddress(library, "census_enabled");
  census_context_create_import = (census_context_create_type) GetProcAddress(library, "census_context_create");
  census_context_destroy_import = (census_context_destroy_type) GetProcAddress(library, "census_context_destroy");
  census_context_get_status_import = (census_context_get_status_type) GetProcAddress(library, "census_context_get_status");
  census_context_initialize_iterator_import = (census_context_initialize_iterator_type) GetProcAddress(library, "census_context_initialize_iterator");
  census_context_next_tag_import = (census_context_next_tag_type) GetProcAddress(library, "census_context_next_tag");
  census_context_get_tag_import = (census_context_get_tag_type) GetProcAddress(library, "census_context_get_tag");
  census_context_encode_import = (census_context_encode_type) GetProcAddress(library, "census_context_encode");
  census_context_decode_import = (census_context_decode_type) GetProcAddress(library, "census_context_decode");
  census_trace_mask_import = (census_trace_mask_type) GetProcAddress(library, "census_trace_mask");
  census_set_trace_mask_import = (census_set_trace_mask_type) GetProcAddress(library, "census_set_trace_mask");
  census_start_rpc_op_timestamp_import = (census_start_rpc_op_timestamp_type) GetProcAddress(library, "census_start_rpc_op_timestamp");
  census_start_client_rpc_op_import = (census_start_client_rpc_op_type) GetProcAddress(library, "census_start_client_rpc_op");
  census_set_rpc_client_peer_import = (census_set_rpc_client_peer_type) GetProcAddress(library, "census_set_rpc_client_peer");
  census_start_server_rpc_op_import = (census_start_server_rpc_op_type) GetProcAddress(library, "census_start_server_rpc_op");
  census_start_op_import = (census_start_op_type) GetProcAddress(library, "census_start_op");
  census_end_op_import = (census_end_op_type) GetProcAddress(library, "census_end_op");
  census_trace_print_import = (census_trace_print_type) GetProcAddress(library, "census_trace_print");
  census_trace_scan_start_import = (census_trace_scan_start_type) GetProcAddress(library, "census_trace_scan_start");
  census_get_trace_record_import = (census_get_trace_record_type) GetProcAddress(library, "census_get_trace_record");
  census_trace_scan_end_import = (census_trace_scan_end_type) GetProcAddress(library, "census_trace_scan_end");
  census_define_resource_import = (census_define_resource_type) GetProcAddress(library, "census_define_resource");
  census_delete_resource_import = (census_delete_resource_type) GetProcAddress(library, "census_delete_resource");
  census_resource_id_import = (census_resource_id_type) GetProcAddress(library, "census_resource_id");
  census_record_values_import = (census_record_values_type) GetProcAddress(library, "census_record_values");
  grpc_auth_property_iterator_next_import = (grpc_auth_property_iterator_next_type) GetProcAddress(library, "grpc_auth_property_iterator_next");
  grpc_auth_context_property_iterator_import = (grpc_auth_context_property_iterator_type) GetProcAddress(library, "grpc_auth_context_property_iterator");
  grpc_auth_context_peer_identity_import = (grpc_auth_context_peer_identity_type) GetProcAddress(library, "grpc_auth_context_peer_identity");
  grpc_auth_context_find_properties_by_name_import = (grpc_auth_context_find_properties_by_name_type) GetProcAddress(library, "grpc_auth_context_find_properties_by_name");
  grpc_auth_context_peer_identity_property_name_import = (grpc_auth_context_peer_identity_property_name_type) GetProcAddress(library, "grpc_auth_context_peer_identity_property_name");
  grpc_auth_context_peer_is_authenticated_import = (grpc_auth_context_peer_is_authenticated_type) GetProcAddress(library, "grpc_auth_context_peer_is_authenticated");
  grpc_call_auth_context_import = (grpc_call_auth_context_type) GetProcAddress(library, "grpc_call_auth_context");
  grpc_auth_context_release_import = (grpc_auth_context_release_type) GetProcAddress(library, "grpc_auth_context_release");
  grpc_auth_context_add_property_import = (grpc_auth_context_add_property_type) GetProcAddress(library, "grpc_auth_context_add_property");
  grpc_auth_context_add_cstring_property_import = (grpc_auth_context_add_cstring_property_type) GetProcAddress(library, "grpc_auth_context_add_cstring_property");
  grpc_auth_context_set_peer_identity_property_name_import = (grpc_auth_context_set_peer_identity_property_name_type) GetProcAddress(library, "grpc_auth_context_set_peer_identity_property_name");
  grpc_channel_credentials_release_import = (grpc_channel_credentials_release_type) GetProcAddress(library, "grpc_channel_credentials_release");
  grpc_google_default_credentials_create_import = (grpc_google_default_credentials_create_type) GetProcAddress(library, "grpc_google_default_credentials_create");
  grpc_set_ssl_roots_override_callback_import = (grpc_set_ssl_roots_override_callback_type) GetProcAddress(library, "grpc_set_ssl_roots_override_callback");
  grpc_ssl_credentials_create_import = (grpc_ssl_credentials_create_type) GetProcAddress(library, "grpc_ssl_credentials_create");
  grpc_call_credentials_release_import = (grpc_call_credentials_release_type) GetProcAddress(library, "grpc_call_credentials_release");
  grpc_composite_channel_credentials_create_import = (grpc_composite_channel_credentials_create_type) GetProcAddress(library, "grpc_composite_channel_credentials_create");
  grpc_composite_call_credentials_create_import = (grpc_composite_call_credentials_create_type) GetProcAddress(library, "grpc_composite_call_credentials_create");
  grpc_google_compute_engine_credentials_create_import = (grpc_google_compute_engine_credentials_create_type) GetProcAddress(library, "grpc_google_compute_engine_credentials_create");
  grpc_max_auth_token_lifetime_import = (grpc_max_auth_token_lifetime_type) GetProcAddress(library, "grpc_max_auth_token_lifetime");
  grpc_service_account_jwt_access_credentials_create_import = (grpc_service_account_jwt_access_credentials_create_type) GetProcAddress(library, "grpc_service_account_jwt_access_credentials_create");
  grpc_google_refresh_token_credentials_create_import = (grpc_google_refresh_token_credentials_create_type) GetProcAddress(library, "grpc_google_refresh_token_credentials_create");
  grpc_access_token_credentials_create_import = (grpc_access_token_credentials_create_type) GetProcAddress(library, "grpc_access_token_credentials_create");
  grpc_google_iam_credentials_create_import = (grpc_google_iam_credentials_create_type) GetProcAddress(library, "grpc_google_iam_credentials_create");
  grpc_metadata_credentials_create_from_plugin_import = (grpc_metadata_credentials_create_from_plugin_type) GetProcAddress(library, "grpc_metadata_credentials_create_from_plugin");
  grpc_secure_channel_create_import = (grpc_secure_channel_create_type) GetProcAddress(library, "grpc_secure_channel_create");
  grpc_server_credentials_release_import = (grpc_server_credentials_release_type) GetProcAddress(library, "grpc_server_credentials_release");
  grpc_ssl_server_credentials_create_import = (grpc_ssl_server_credentials_create_type) GetProcAddress(library, "grpc_ssl_server_credentials_create");
  grpc_ssl_server_credentials_create_ex_import = (grpc_ssl_server_credentials_create_ex_type) GetProcAddress(library, "grpc_ssl_server_credentials_create_ex");
  grpc_server_add_secure_http2_port_import = (grpc_server_add_secure_http2_port_type) GetProcAddress(library, "grpc_server_add_secure_http2_port");
  grpc_call_set_credentials_import = (grpc_call_set_credentials_type) GetProcAddress(library, "grpc_call_set_credentials");
  grpc_server_credentials_set_auth_metadata_processor_import = (grpc_server_credentials_set_auth_metadata_processor_type) GetProcAddress(library, "grpc_server_credentials_set_auth_metadata_processor");
  gpr_malloc_import = (gpr_malloc_type) GetProcAddress(library, "gpr_malloc");
  gpr_zalloc_import = (gpr_zalloc_type) GetProcAddress(library, "gpr_zalloc");
  gpr_free_import = (gpr_free_type) GetProcAddress(library, "gpr_free");
  gpr_realloc_import = (gpr_realloc_type) GetProcAddress(library, "gpr_realloc");
  gpr_malloc_aligned_import = (gpr_malloc_aligned_type) GetProcAddress(library, "gpr_malloc_aligned");
  gpr_free_aligned_import = (gpr_free_aligned_type) GetProcAddress(library, "gpr_free_aligned");
  gpr_set_allocation_functions_import = (gpr_set_allocation_functions_type) GetProcAddress(library, "gpr_set_allocation_functions");
  gpr_get_allocation_functions_import = (gpr_get_allocation_functions_type) GetProcAddress(library, "gpr_get_allocation_functions");
  gpr_avl_create_import = (gpr_avl_create_type) GetProcAddress(library, "gpr_avl_create");
  gpr_avl_ref_import = (gpr_avl_ref_type) GetProcAddress(library, "gpr_avl_ref");
  gpr_avl_unref_import = (gpr_avl_unref_type) GetProcAddress(library, "gpr_avl_unref");
  gpr_avl_add_import = (gpr_avl_add_type) GetProcAddress(library, "gpr_avl_add");
  gpr_avl_remove_import = (gpr_avl_remove_type) GetProcAddress(library, "gpr_avl_remove");
  gpr_avl_get_import = (gpr_avl_get_type) GetProcAddress(library, "gpr_avl_get");
  gpr_avl_maybe_get_import = (gpr_avl_maybe_get_type) GetProcAddress(library, "gpr_avl_maybe_get");
  gpr_avl_is_empty_import = (gpr_avl_is_empty_type) GetProcAddress(library, "gpr_avl_is_empty");
  gpr_cmdline_create_import = (gpr_cmdline_create_type) GetProcAddress(library, "gpr_cmdline_create");
  gpr_cmdline_add_int_import = (gpr_cmdline_add_int_type) GetProcAddress(library, "gpr_cmdline_add_int");
  gpr_cmdline_add_flag_import = (gpr_cmdline_add_flag_type) GetProcAddress(library, "gpr_cmdline_add_flag");
  gpr_cmdline_add_string_import = (gpr_cmdline_add_string_type) GetProcAddress(library, "gpr_cmdline_add_string");
  gpr_cmdline_on_extra_arg_import = (gpr_cmdline_on_extra_arg_type) GetProcAddress(library, "gpr_cmdline_on_extra_arg");
  gpr_cmdline_set_survive_failure_import = (gpr_cmdline_set_survive_failure_type) GetProcAddress(library, "gpr_cmdline_set_survive_failure");
  gpr_cmdline_parse_import = (gpr_cmdline_parse_type) GetProcAddress(library, "gpr_cmdline_parse");
  gpr_cmdline_destroy_import = (gpr_cmdline_destroy_type) GetProcAddress(library, "gpr_cmdline_destroy");
  gpr_cmdline_usage_string_import = (gpr_cmdline_usage_string_type) GetProcAddress(library, "gpr_cmdline_usage_string");
  gpr_cpu_num_cores_import = (gpr_cpu_num_cores_type) GetProcAddress(library, "gpr_cpu_num_cores");
  gpr_cpu_current_cpu_import = (gpr_cpu_current_cpu_type) GetProcAddress(library, "gpr_cpu_current_cpu");
  gpr_histogram_create_import = (gpr_histogram_create_type) GetProcAddress(library, "gpr_histogram_create");
  gpr_histogram_destroy_import = (gpr_histogram_destroy_type) GetProcAddress(library, "gpr_histogram_destroy");
  gpr_histogram_add_import = (gpr_histogram_add_type) GetProcAddress(library, "gpr_histogram_add");
  gpr_histogram_merge_import = (gpr_histogram_merge_type) GetProcAddress(library, "gpr_histogram_merge");
  gpr_histogram_percentile_import = (gpr_histogram_percentile_type) GetProcAddress(library, "gpr_histogram_percentile");
  gpr_histogram_mean_import = (gpr_histogram_mean_type) GetProcAddress(library, "gpr_histogram_mean");
  gpr_histogram_stddev_import = (gpr_histogram_stddev_type) GetProcAddress(library, "gpr_histogram_stddev");
  gpr_histogram_variance_import = (gpr_histogram_variance_type) GetProcAddress(library, "gpr_histogram_variance");
  gpr_histogram_maximum_import = (gpr_histogram_maximum_type) GetProcAddress(library, "gpr_histogram_maximum");
  gpr_histogram_minimum_import = (gpr_histogram_minimum_type) GetProcAddress(library, "gpr_histogram_minimum");
  gpr_histogram_count_import = (gpr_histogram_count_type) GetProcAddress(library, "gpr_histogram_count");
  gpr_histogram_sum_import = (gpr_histogram_sum_type) GetProcAddress(library, "gpr_histogram_sum");
  gpr_histogram_sum_of_squares_import = (gpr_histogram_sum_of_squares_type) GetProcAddress(library, "gpr_histogram_sum_of_squares");
  gpr_histogram_get_contents_import = (gpr_histogram_get_contents_type) GetProcAddress(library, "gpr_histogram_get_contents");
  gpr_histogram_merge_contents_import = (gpr_histogram_merge_contents_type) GetProcAddress(library, "gpr_histogram_merge_contents");
  gpr_join_host_port_import = (gpr_join_host_port_type) GetProcAddress(library, "gpr_join_host_port");
  gpr_split_host_port_import = (gpr_split_host_port_type) GetProcAddress(library, "gpr_split_host_port");
  gpr_log_import = (gpr_log_type) GetProcAddress(library, "gpr_log");
  gpr_log_message_import = (gpr_log_message_type) GetProcAddress(library, "gpr_log_message");
  gpr_set_log_verbosity_import = (gpr_set_log_verbosity_type) GetProcAddress(library, "gpr_set_log_verbosity");
  gpr_log_verbosity_init_import = (gpr_log_verbosity_init_type) GetProcAddress(library, "gpr_log_verbosity_init");
  gpr_set_log_function_import = (gpr_set_log_function_type) GetProcAddress(library, "gpr_set_log_function");
  gpr_format_message_import = (gpr_format_message_type) GetProcAddress(library, "gpr_format_message");
  gpr_strdup_import = (gpr_strdup_type) GetProcAddress(library, "gpr_strdup");
  gpr_asprintf_import = (gpr_asprintf_type) GetProcAddress(library, "gpr_asprintf");
  gpr_subprocess_binary_extension_import = (gpr_subprocess_binary_extension_type) GetProcAddress(library, "gpr_subprocess_binary_extension");
  gpr_subprocess_create_import = (gpr_subprocess_create_type) GetProcAddress(library, "gpr_subprocess_create");
  gpr_subprocess_destroy_import = (gpr_subprocess_destroy_type) GetProcAddress(library, "gpr_subprocess_destroy");
  gpr_subprocess_join_import = (gpr_subprocess_join_type) GetProcAddress(library, "gpr_subprocess_join");
  gpr_subprocess_interrupt_import = (gpr_subprocess_interrupt_type) GetProcAddress(library, "gpr_subprocess_interrupt");
  gpr_mu_init_import = (gpr_mu_init_type) GetProcAddress(library, "gpr_mu_init");
  gpr_mu_destroy_import = (gpr_mu_destroy_type) GetProcAddress(library, "gpr_mu_destroy");
  gpr_mu_lock_import = (gpr_mu_lock_type) GetProcAddress(library, "gpr_mu_lock");
  gpr_mu_unlock_import = (gpr_mu_unlock_type) GetProcAddress(library, "gpr_mu_unlock");
  gpr_mu_trylock_import = (gpr_mu_trylock_type) GetProcAddress(library, "gpr_mu_trylock");
  gpr_cv_init_import = (gpr_cv_init_type) GetProcAddress(library, "gpr_cv_init");
  gpr_cv_destroy_import = (gpr_cv_destroy_type) GetProcAddress(library, "gpr_cv_destroy");
  gpr_cv_wait_import = (gpr_cv_wait_type) GetProcAddress(library, "gpr_cv_wait");
  gpr_cv_signal_import = (gpr_cv_signal_type) GetProcAddress(library, "gpr_cv_signal");
  gpr_cv_broadcast_import = (gpr_cv_broadcast_type) GetProcAddress(library, "gpr_cv_broadcast");
  gpr_once_init_import = (gpr_once_init_type) GetProcAddress(library, "gpr_once_init");
  gpr_event_init_import = (gpr_event_init_type) GetProcAddress(library, "gpr_event_init");
  gpr_event_set_import = (gpr_event_set_type) GetProcAddress(library, "gpr_event_set");
  gpr_event_get_import = (gpr_event_get_type) GetProcAddress(library, "gpr_event_get");
  gpr_event_wait_import = (gpr_event_wait_type) GetProcAddress(library, "gpr_event_wait");
  gpr_ref_init_import = (gpr_ref_init_type) GetProcAddress(library, "gpr_ref_init");
  gpr_ref_import = (gpr_ref_type) GetProcAddress(library, "gpr_ref");
  gpr_ref_non_zero_import = (gpr_ref_non_zero_type) GetProcAddress(library, "gpr_ref_non_zero");
  gpr_refn_import = (gpr_refn_type) GetProcAddress(library, "gpr_refn");
  gpr_unref_import = (gpr_unref_type) GetProcAddress(library, "gpr_unref");
  gpr_ref_is_unique_import = (gpr_ref_is_unique_type) GetProcAddress(library, "gpr_ref_is_unique");
  gpr_stats_init_import = (gpr_stats_init_type) GetProcAddress(library, "gpr_stats_init");
  gpr_stats_inc_import = (gpr_stats_inc_type) GetProcAddress(library, "gpr_stats_inc");
  gpr_stats_read_import = (gpr_stats_read_type) GetProcAddress(library, "gpr_stats_read");
  gpr_thd_new_import = (gpr_thd_new_type) GetProcAddress(library, "gpr_thd_new");
  gpr_thd_options_default_import = (gpr_thd_options_default_type) GetProcAddress(library, "gpr_thd_options_default");
  gpr_thd_options_set_detached_import = (gpr_thd_options_set_detached_type) GetProcAddress(library, "gpr_thd_options_set_detached");
  gpr_thd_options_set_joinable_import = (gpr_thd_options_set_joinable_type) GetProcAddress(library, "gpr_thd_options_set_joinable");
  gpr_thd_options_is_detached_import = (gpr_thd_options_is_detached_type) GetProcAddress(library, "gpr_thd_options_is_detached");
  gpr_thd_options_is_joinable_import = (gpr_thd_options_is_joinable_type) GetProcAddress(library, "gpr_thd_options_is_joinable");
  gpr_thd_currentid_import = (gpr_thd_currentid_type) GetProcAddress(library, "gpr_thd_currentid");
  gpr_thd_join_import = (gpr_thd_join_type) GetProcAddress(library, "gpr_thd_join");
  gpr_time_0_import = (gpr_time_0_type) GetProcAddress(library, "gpr_time_0");
  gpr_inf_future_import = (gpr_inf_future_type) GetProcAddress(library, "gpr_inf_future");
  gpr_inf_past_import = (gpr_inf_past_type) GetProcAddress(library, "gpr_inf_past");
  gpr_time_init_import = (gpr_time_init_type) GetProcAddress(library, "gpr_time_init");
  gpr_now_import = (gpr_now_type) GetProcAddress(library, "gpr_now");
  gpr_convert_clock_type_import = (gpr_convert_clock_type_type) GetProcAddress(library, "gpr_convert_clock_type");
  gpr_time_cmp_import = (gpr_time_cmp_type) GetProcAddress(library, "gpr_time_cmp");
  gpr_time_max_import = (gpr_time_max_type) GetProcAddress(library, "gpr_time_max");
  gpr_time_min_import = (gpr_time_min_type) GetProcAddress(library, "gpr_time_min");
  gpr_time_add_import = (gpr_time_add_type) GetProcAddress(library, "gpr_time_add");
  gpr_time_sub_import = (gpr_time_sub_type) GetProcAddress(library, "gpr_time_sub");
  gpr_time_from_micros_import = (gpr_time_from_micros_type) GetProcAddress(library, "gpr_time_from_micros");
  gpr_time_from_nanos_import = (gpr_time_from_nanos_type) GetProcAddress(library, "gpr_time_from_nanos");
  gpr_time_from_millis_import = (gpr_time_from_millis_type) GetProcAddress(library, "gpr_time_from_millis");
  gpr_time_from_seconds_import = (gpr_time_from_seconds_type) GetProcAddress(library, "gpr_time_from_seconds");
  gpr_time_from_minutes_import = (gpr_time_from_minutes_type) GetProcAddress(library, "gpr_time_from_minutes");
  gpr_time_from_hours_import = (gpr_time_from_hours_type) GetProcAddress(library, "gpr_time_from_hours");
  gpr_time_to_millis_import = (gpr_time_to_millis_type) GetProcAddress(library, "gpr_time_to_millis");
  gpr_time_similar_import = (gpr_time_similar_type) GetProcAddress(library, "gpr_time_similar");
  gpr_sleep_until_import = (gpr_sleep_until_type) GetProcAddress(library, "gpr_sleep_until");
  gpr_timespec_to_micros_import = (gpr_timespec_to_micros_type) GetProcAddress(library, "gpr_timespec_to_micros");
}

#endif /* GPR_WINDOWS */
