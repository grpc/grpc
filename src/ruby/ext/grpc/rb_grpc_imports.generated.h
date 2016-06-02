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

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS

#include <windows.h>

#include <grpc/census.h>
#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/grpc_posix.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/codegen/alloc.h>
#include <grpc/impl/codegen/byte_buffer.h>
#include <grpc/impl/codegen/log.h>
#include <grpc/impl/codegen/slice.h>
#include <grpc/impl/codegen/slice_buffer.h>
#include <grpc/impl/codegen/sync.h>
#include <grpc/impl/codegen/time.h>
#include <grpc/support/avl.h>
#include <grpc/support/cmdline.h>
#include <grpc/support/cpu.h>
#include <grpc/support/histogram.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log_windows.h>
#include <grpc/support/string_util.h>
#include <grpc/support/subprocess.h>
#include <grpc/support/thd.h>

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
typedef census_context *(*census_context_create_type)(const census_context *base, const census_tag *tags, int ntags, census_context_status const **status);
extern census_context_create_type census_context_create_import;
#define census_context_create census_context_create_import
typedef void(*census_context_destroy_type)(census_context *context);
extern census_context_destroy_type census_context_destroy_import;
#define census_context_destroy census_context_destroy_import
typedef const census_context_status *(*census_context_get_status_type)(const census_context *context);
extern census_context_get_status_type census_context_get_status_import;
#define census_context_get_status census_context_get_status_import
typedef void(*census_context_initialize_iterator_type)(const census_context *context, census_context_iterator *iterator);
extern census_context_initialize_iterator_type census_context_initialize_iterator_import;
#define census_context_initialize_iterator census_context_initialize_iterator_import
typedef int(*census_context_next_tag_type)(census_context_iterator *iterator, census_tag *tag);
extern census_context_next_tag_type census_context_next_tag_import;
#define census_context_next_tag census_context_next_tag_import
typedef int(*census_context_get_tag_type)(const census_context *context, const char *key, census_tag *tag);
extern census_context_get_tag_type census_context_get_tag_import;
#define census_context_get_tag census_context_get_tag_import
typedef size_t(*census_context_encode_type)(const census_context *context, char *buffer, size_t buf_size);
extern census_context_encode_type census_context_encode_import;
#define census_context_encode census_context_encode_import
typedef census_context *(*census_context_decode_type)(const char *buffer, size_t size);
extern census_context_decode_type census_context_decode_import;
#define census_context_decode census_context_decode_import
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
typedef void(*census_record_values_type)(census_context *context, census_value *values, size_t nvalues);
extern census_record_values_type census_record_values_import;
#define census_record_values census_record_values_import
typedef census_view *(*census_view_create_type)(uint32_t metric_id, const census_context *tags, const census_aggregation *aggregations, size_t naggregations);
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
typedef const census_context *(*census_view_tags_type)(const census_view *view);
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
typedef int(*grpc_compression_algorithm_parse_type)(const char *name, size_t name_length, grpc_compression_algorithm *algorithm);
extern grpc_compression_algorithm_parse_type grpc_compression_algorithm_parse_import;
#define grpc_compression_algorithm_parse grpc_compression_algorithm_parse_import
typedef int(*grpc_compression_algorithm_name_type)(grpc_compression_algorithm algorithm, char **name);
extern grpc_compression_algorithm_name_type grpc_compression_algorithm_name_import;
#define grpc_compression_algorithm_name grpc_compression_algorithm_name_import
typedef grpc_compression_algorithm(*grpc_compression_algorithm_for_level_type)(grpc_compression_level level, uint32_t accepted_encodings);
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
typedef void *(*grpc_server_register_method_type)(grpc_server *server, const char *method, const char *host, grpc_server_register_method_payload_handling payload_handling, uint32_t flags);
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
typedef void(*grpc_server_register_non_listening_completion_queue_type)(grpc_server *server, grpc_completion_queue *q, void *reserved);
extern grpc_server_register_non_listening_completion_queue_type grpc_server_register_non_listening_completion_queue_import;
#define grpc_server_register_non_listening_completion_queue grpc_server_register_non_listening_completion_queue_import
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
typedef const char *(*grpc_call_error_to_string_type)(grpc_call_error error);
extern grpc_call_error_to_string_type grpc_call_error_to_string_import;
#define grpc_call_error_to_string grpc_call_error_to_string_import
typedef grpc_channel *(*grpc_insecure_channel_create_from_fd_type)(const char *target, int fd, const grpc_channel_args *args);
extern grpc_insecure_channel_create_from_fd_type grpc_insecure_channel_create_from_fd_import;
#define grpc_insecure_channel_create_from_fd grpc_insecure_channel_create_from_fd_import
typedef void(*grpc_server_add_insecure_channel_from_fd_type)(grpc_server *server, grpc_completion_queue *cq, int fd);
extern grpc_server_add_insecure_channel_from_fd_type grpc_server_add_insecure_channel_from_fd_import;
#define grpc_server_add_insecure_channel_from_fd grpc_server_add_insecure_channel_from_fd_import
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
typedef void(*grpc_set_ssl_roots_override_callback_type)(grpc_ssl_roots_override_callback cb);
extern grpc_set_ssl_roots_override_callback_type grpc_set_ssl_roots_override_callback_import;
#define grpc_set_ssl_roots_override_callback grpc_set_ssl_roots_override_callback_import
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
typedef gpr_timespec(*grpc_max_auth_token_lifetime_type)();
extern grpc_max_auth_token_lifetime_type grpc_max_auth_token_lifetime_import;
#define grpc_max_auth_token_lifetime grpc_max_auth_token_lifetime_import
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
typedef grpc_server_credentials *(*grpc_ssl_server_credentials_create_ex_type)(const char *pem_root_certs, grpc_ssl_pem_key_cert_pair *pem_key_cert_pairs, size_t num_key_cert_pairs, grpc_ssl_client_certificate_request_type client_certificate_request, void *reserved);
extern grpc_ssl_server_credentials_create_ex_type grpc_ssl_server_credentials_create_ex_import;
#define grpc_ssl_server_credentials_create_ex grpc_ssl_server_credentials_create_ex_import
typedef int(*grpc_server_add_secure_http2_port_type)(grpc_server *server, const char *addr, grpc_server_credentials *creds);
extern grpc_server_add_secure_http2_port_type grpc_server_add_secure_http2_port_import;
#define grpc_server_add_secure_http2_port grpc_server_add_secure_http2_port_import
typedef grpc_call_error(*grpc_call_set_credentials_type)(grpc_call *call, grpc_call_credentials *creds);
extern grpc_call_set_credentials_type grpc_call_set_credentials_import;
#define grpc_call_set_credentials grpc_call_set_credentials_import
typedef void(*grpc_server_credentials_set_auth_metadata_processor_type)(grpc_server_credentials *creds, grpc_auth_metadata_processor processor);
extern grpc_server_credentials_set_auth_metadata_processor_type grpc_server_credentials_set_auth_metadata_processor_import;
#define grpc_server_credentials_set_auth_metadata_processor grpc_server_credentials_set_auth_metadata_processor_import
typedef void *(*gpr_malloc_type)(size_t size);
extern gpr_malloc_type gpr_malloc_import;
#define gpr_malloc gpr_malloc_import
typedef void(*gpr_free_type)(void *ptr);
extern gpr_free_type gpr_free_import;
#define gpr_free gpr_free_import
typedef void *(*gpr_realloc_type)(void *p, size_t size);
extern gpr_realloc_type gpr_realloc_import;
#define gpr_realloc gpr_realloc_import
typedef void *(*gpr_malloc_aligned_type)(size_t size, size_t alignment_log);
extern gpr_malloc_aligned_type gpr_malloc_aligned_import;
#define gpr_malloc_aligned gpr_malloc_aligned_import
typedef void(*gpr_free_aligned_type)(void *ptr);
extern gpr_free_aligned_type gpr_free_aligned_import;
#define gpr_free_aligned gpr_free_aligned_import
typedef void(*gpr_set_allocation_functions_type)(gpr_allocation_functions functions);
extern gpr_set_allocation_functions_type gpr_set_allocation_functions_import;
#define gpr_set_allocation_functions gpr_set_allocation_functions_import
typedef gpr_allocation_functions(*gpr_get_allocation_functions_type)();
extern gpr_get_allocation_functions_type gpr_get_allocation_functions_import;
#define gpr_get_allocation_functions gpr_get_allocation_functions_import
typedef grpc_byte_buffer *(*grpc_raw_byte_buffer_create_type)(gpr_slice *slices, size_t nslices);
extern grpc_raw_byte_buffer_create_type grpc_raw_byte_buffer_create_import;
#define grpc_raw_byte_buffer_create grpc_raw_byte_buffer_create_import
typedef grpc_byte_buffer *(*grpc_raw_compressed_byte_buffer_create_type)(gpr_slice *slices, size_t nslices, grpc_compression_algorithm compression);
extern grpc_raw_compressed_byte_buffer_create_type grpc_raw_compressed_byte_buffer_create_import;
#define grpc_raw_compressed_byte_buffer_create grpc_raw_compressed_byte_buffer_create_import
typedef grpc_byte_buffer *(*grpc_byte_buffer_copy_type)(grpc_byte_buffer *bb);
extern grpc_byte_buffer_copy_type grpc_byte_buffer_copy_import;
#define grpc_byte_buffer_copy grpc_byte_buffer_copy_import
typedef size_t(*grpc_byte_buffer_length_type)(grpc_byte_buffer *bb);
extern grpc_byte_buffer_length_type grpc_byte_buffer_length_import;
#define grpc_byte_buffer_length grpc_byte_buffer_length_import
typedef void(*grpc_byte_buffer_destroy_type)(grpc_byte_buffer *byte_buffer);
extern grpc_byte_buffer_destroy_type grpc_byte_buffer_destroy_import;
#define grpc_byte_buffer_destroy grpc_byte_buffer_destroy_import
typedef void(*grpc_byte_buffer_reader_init_type)(grpc_byte_buffer_reader *reader, grpc_byte_buffer *buffer);
extern grpc_byte_buffer_reader_init_type grpc_byte_buffer_reader_init_import;
#define grpc_byte_buffer_reader_init grpc_byte_buffer_reader_init_import
typedef void(*grpc_byte_buffer_reader_destroy_type)(grpc_byte_buffer_reader *reader);
extern grpc_byte_buffer_reader_destroy_type grpc_byte_buffer_reader_destroy_import;
#define grpc_byte_buffer_reader_destroy grpc_byte_buffer_reader_destroy_import
typedef int(*grpc_byte_buffer_reader_next_type)(grpc_byte_buffer_reader *reader, gpr_slice *slice);
extern grpc_byte_buffer_reader_next_type grpc_byte_buffer_reader_next_import;
#define grpc_byte_buffer_reader_next grpc_byte_buffer_reader_next_import
typedef gpr_slice(*grpc_byte_buffer_reader_readall_type)(grpc_byte_buffer_reader *reader);
extern grpc_byte_buffer_reader_readall_type grpc_byte_buffer_reader_readall_import;
#define grpc_byte_buffer_reader_readall grpc_byte_buffer_reader_readall_import
typedef grpc_byte_buffer *(*grpc_raw_byte_buffer_from_reader_type)(grpc_byte_buffer_reader *reader);
extern grpc_raw_byte_buffer_from_reader_type grpc_raw_byte_buffer_from_reader_import;
#define grpc_raw_byte_buffer_from_reader grpc_raw_byte_buffer_from_reader_import
typedef void(*gpr_log_type)(const char *file, int line, gpr_log_severity severity, const char *format, ...);
extern gpr_log_type gpr_log_import;
#define gpr_log gpr_log_import
typedef void(*gpr_log_message_type)(const char *file, int line, gpr_log_severity severity, const char *message);
extern gpr_log_message_type gpr_log_message_import;
#define gpr_log_message gpr_log_message_import
typedef void(*gpr_set_log_verbosity_type)(gpr_log_severity min_severity_to_print);
extern gpr_set_log_verbosity_type gpr_set_log_verbosity_import;
#define gpr_set_log_verbosity gpr_set_log_verbosity_import
typedef void(*gpr_log_verbosity_init_type)();
extern gpr_log_verbosity_init_type gpr_log_verbosity_init_import;
#define gpr_log_verbosity_init gpr_log_verbosity_init_import
typedef void(*gpr_set_log_function_type)(gpr_log_func func);
extern gpr_set_log_function_type gpr_set_log_function_import;
#define gpr_set_log_function gpr_set_log_function_import
typedef gpr_slice(*gpr_slice_ref_type)(gpr_slice s);
extern gpr_slice_ref_type gpr_slice_ref_import;
#define gpr_slice_ref gpr_slice_ref_import
typedef void(*gpr_slice_unref_type)(gpr_slice s);
extern gpr_slice_unref_type gpr_slice_unref_import;
#define gpr_slice_unref gpr_slice_unref_import
typedef gpr_slice(*gpr_slice_new_type)(void *p, size_t len, void (*destroy)(void *));
extern gpr_slice_new_type gpr_slice_new_import;
#define gpr_slice_new gpr_slice_new_import
typedef gpr_slice(*gpr_slice_new_with_len_type)(void *p, size_t len, void (*destroy)(void *, size_t));
extern gpr_slice_new_with_len_type gpr_slice_new_with_len_import;
#define gpr_slice_new_with_len gpr_slice_new_with_len_import
typedef gpr_slice(*gpr_slice_malloc_type)(size_t length);
extern gpr_slice_malloc_type gpr_slice_malloc_import;
#define gpr_slice_malloc gpr_slice_malloc_import
typedef gpr_slice(*gpr_slice_from_copied_string_type)(const char *source);
extern gpr_slice_from_copied_string_type gpr_slice_from_copied_string_import;
#define gpr_slice_from_copied_string gpr_slice_from_copied_string_import
typedef gpr_slice(*gpr_slice_from_copied_buffer_type)(const char *source, size_t len);
extern gpr_slice_from_copied_buffer_type gpr_slice_from_copied_buffer_import;
#define gpr_slice_from_copied_buffer gpr_slice_from_copied_buffer_import
typedef gpr_slice(*gpr_slice_from_static_string_type)(const char *source);
extern gpr_slice_from_static_string_type gpr_slice_from_static_string_import;
#define gpr_slice_from_static_string gpr_slice_from_static_string_import
typedef gpr_slice(*gpr_slice_sub_type)(gpr_slice s, size_t begin, size_t end);
extern gpr_slice_sub_type gpr_slice_sub_import;
#define gpr_slice_sub gpr_slice_sub_import
typedef gpr_slice(*gpr_slice_sub_no_ref_type)(gpr_slice s, size_t begin, size_t end);
extern gpr_slice_sub_no_ref_type gpr_slice_sub_no_ref_import;
#define gpr_slice_sub_no_ref gpr_slice_sub_no_ref_import
typedef gpr_slice(*gpr_slice_split_tail_type)(gpr_slice *s, size_t split);
extern gpr_slice_split_tail_type gpr_slice_split_tail_import;
#define gpr_slice_split_tail gpr_slice_split_tail_import
typedef gpr_slice(*gpr_slice_split_head_type)(gpr_slice *s, size_t split);
extern gpr_slice_split_head_type gpr_slice_split_head_import;
#define gpr_slice_split_head gpr_slice_split_head_import
typedef gpr_slice(*gpr_empty_slice_type)(void);
extern gpr_empty_slice_type gpr_empty_slice_import;
#define gpr_empty_slice gpr_empty_slice_import
typedef int(*gpr_slice_cmp_type)(gpr_slice a, gpr_slice b);
extern gpr_slice_cmp_type gpr_slice_cmp_import;
#define gpr_slice_cmp gpr_slice_cmp_import
typedef int(*gpr_slice_str_cmp_type)(gpr_slice a, const char *b);
extern gpr_slice_str_cmp_type gpr_slice_str_cmp_import;
#define gpr_slice_str_cmp gpr_slice_str_cmp_import
typedef void(*gpr_slice_buffer_init_type)(gpr_slice_buffer *sb);
extern gpr_slice_buffer_init_type gpr_slice_buffer_init_import;
#define gpr_slice_buffer_init gpr_slice_buffer_init_import
typedef void(*gpr_slice_buffer_destroy_type)(gpr_slice_buffer *sb);
extern gpr_slice_buffer_destroy_type gpr_slice_buffer_destroy_import;
#define gpr_slice_buffer_destroy gpr_slice_buffer_destroy_import
typedef void(*gpr_slice_buffer_add_type)(gpr_slice_buffer *sb, gpr_slice slice);
extern gpr_slice_buffer_add_type gpr_slice_buffer_add_import;
#define gpr_slice_buffer_add gpr_slice_buffer_add_import
typedef size_t(*gpr_slice_buffer_add_indexed_type)(gpr_slice_buffer *sb, gpr_slice slice);
extern gpr_slice_buffer_add_indexed_type gpr_slice_buffer_add_indexed_import;
#define gpr_slice_buffer_add_indexed gpr_slice_buffer_add_indexed_import
typedef void(*gpr_slice_buffer_addn_type)(gpr_slice_buffer *sb, gpr_slice *slices, size_t n);
extern gpr_slice_buffer_addn_type gpr_slice_buffer_addn_import;
#define gpr_slice_buffer_addn gpr_slice_buffer_addn_import
typedef uint8_t *(*gpr_slice_buffer_tiny_add_type)(gpr_slice_buffer *sb, size_t len);
extern gpr_slice_buffer_tiny_add_type gpr_slice_buffer_tiny_add_import;
#define gpr_slice_buffer_tiny_add gpr_slice_buffer_tiny_add_import
typedef void(*gpr_slice_buffer_pop_type)(gpr_slice_buffer *sb);
extern gpr_slice_buffer_pop_type gpr_slice_buffer_pop_import;
#define gpr_slice_buffer_pop gpr_slice_buffer_pop_import
typedef void(*gpr_slice_buffer_reset_and_unref_type)(gpr_slice_buffer *sb);
extern gpr_slice_buffer_reset_and_unref_type gpr_slice_buffer_reset_and_unref_import;
#define gpr_slice_buffer_reset_and_unref gpr_slice_buffer_reset_and_unref_import
typedef void(*gpr_slice_buffer_swap_type)(gpr_slice_buffer *a, gpr_slice_buffer *b);
extern gpr_slice_buffer_swap_type gpr_slice_buffer_swap_import;
#define gpr_slice_buffer_swap gpr_slice_buffer_swap_import
typedef void(*gpr_slice_buffer_move_into_type)(gpr_slice_buffer *src, gpr_slice_buffer *dst);
extern gpr_slice_buffer_move_into_type gpr_slice_buffer_move_into_import;
#define gpr_slice_buffer_move_into gpr_slice_buffer_move_into_import
typedef void(*gpr_slice_buffer_trim_end_type)(gpr_slice_buffer *src, size_t n, gpr_slice_buffer *garbage);
extern gpr_slice_buffer_trim_end_type gpr_slice_buffer_trim_end_import;
#define gpr_slice_buffer_trim_end gpr_slice_buffer_trim_end_import
typedef void(*gpr_slice_buffer_move_first_type)(gpr_slice_buffer *src, size_t n, gpr_slice_buffer *dst);
extern gpr_slice_buffer_move_first_type gpr_slice_buffer_move_first_import;
#define gpr_slice_buffer_move_first gpr_slice_buffer_move_first_import
typedef gpr_slice(*gpr_slice_buffer_take_first_type)(gpr_slice_buffer *src);
extern gpr_slice_buffer_take_first_type gpr_slice_buffer_take_first_import;
#define gpr_slice_buffer_take_first gpr_slice_buffer_take_first_import
typedef void(*gpr_mu_init_type)(gpr_mu *mu);
extern gpr_mu_init_type gpr_mu_init_import;
#define gpr_mu_init gpr_mu_init_import
typedef void(*gpr_mu_destroy_type)(gpr_mu *mu);
extern gpr_mu_destroy_type gpr_mu_destroy_import;
#define gpr_mu_destroy gpr_mu_destroy_import
typedef void(*gpr_mu_lock_type)(gpr_mu *mu);
extern gpr_mu_lock_type gpr_mu_lock_import;
#define gpr_mu_lock gpr_mu_lock_import
typedef void(*gpr_mu_unlock_type)(gpr_mu *mu);
extern gpr_mu_unlock_type gpr_mu_unlock_import;
#define gpr_mu_unlock gpr_mu_unlock_import
typedef int(*gpr_mu_trylock_type)(gpr_mu *mu);
extern gpr_mu_trylock_type gpr_mu_trylock_import;
#define gpr_mu_trylock gpr_mu_trylock_import
typedef void(*gpr_cv_init_type)(gpr_cv *cv);
extern gpr_cv_init_type gpr_cv_init_import;
#define gpr_cv_init gpr_cv_init_import
typedef void(*gpr_cv_destroy_type)(gpr_cv *cv);
extern gpr_cv_destroy_type gpr_cv_destroy_import;
#define gpr_cv_destroy gpr_cv_destroy_import
typedef int(*gpr_cv_wait_type)(gpr_cv *cv, gpr_mu *mu, gpr_timespec abs_deadline);
extern gpr_cv_wait_type gpr_cv_wait_import;
#define gpr_cv_wait gpr_cv_wait_import
typedef void(*gpr_cv_signal_type)(gpr_cv *cv);
extern gpr_cv_signal_type gpr_cv_signal_import;
#define gpr_cv_signal gpr_cv_signal_import
typedef void(*gpr_cv_broadcast_type)(gpr_cv *cv);
extern gpr_cv_broadcast_type gpr_cv_broadcast_import;
#define gpr_cv_broadcast gpr_cv_broadcast_import
typedef void(*gpr_once_init_type)(gpr_once *once, void (*init_routine)(void));
extern gpr_once_init_type gpr_once_init_import;
#define gpr_once_init gpr_once_init_import
typedef void(*gpr_event_init_type)(gpr_event *ev);
extern gpr_event_init_type gpr_event_init_import;
#define gpr_event_init gpr_event_init_import
typedef void(*gpr_event_set_type)(gpr_event *ev, void *value);
extern gpr_event_set_type gpr_event_set_import;
#define gpr_event_set gpr_event_set_import
typedef void *(*gpr_event_get_type)(gpr_event *ev);
extern gpr_event_get_type gpr_event_get_import;
#define gpr_event_get gpr_event_get_import
typedef void *(*gpr_event_wait_type)(gpr_event *ev, gpr_timespec abs_deadline);
extern gpr_event_wait_type gpr_event_wait_import;
#define gpr_event_wait gpr_event_wait_import
typedef void(*gpr_ref_init_type)(gpr_refcount *r, int n);
extern gpr_ref_init_type gpr_ref_init_import;
#define gpr_ref_init gpr_ref_init_import
typedef void(*gpr_ref_type)(gpr_refcount *r);
extern gpr_ref_type gpr_ref_import;
#define gpr_ref gpr_ref_import
typedef void(*gpr_ref_non_zero_type)(gpr_refcount *r);
extern gpr_ref_non_zero_type gpr_ref_non_zero_import;
#define gpr_ref_non_zero gpr_ref_non_zero_import
typedef void(*gpr_refn_type)(gpr_refcount *r, int n);
extern gpr_refn_type gpr_refn_import;
#define gpr_refn gpr_refn_import
typedef int(*gpr_unref_type)(gpr_refcount *r);
extern gpr_unref_type gpr_unref_import;
#define gpr_unref gpr_unref_import
typedef void(*gpr_stats_init_type)(gpr_stats_counter *c, intptr_t n);
extern gpr_stats_init_type gpr_stats_init_import;
#define gpr_stats_init gpr_stats_init_import
typedef void(*gpr_stats_inc_type)(gpr_stats_counter *c, intptr_t inc);
extern gpr_stats_inc_type gpr_stats_inc_import;
#define gpr_stats_inc gpr_stats_inc_import
typedef intptr_t(*gpr_stats_read_type)(const gpr_stats_counter *c);
extern gpr_stats_read_type gpr_stats_read_import;
#define gpr_stats_read gpr_stats_read_import
typedef gpr_timespec(*gpr_time_0_type)(gpr_clock_type type);
extern gpr_time_0_type gpr_time_0_import;
#define gpr_time_0 gpr_time_0_import
typedef gpr_timespec(*gpr_inf_future_type)(gpr_clock_type type);
extern gpr_inf_future_type gpr_inf_future_import;
#define gpr_inf_future gpr_inf_future_import
typedef gpr_timespec(*gpr_inf_past_type)(gpr_clock_type type);
extern gpr_inf_past_type gpr_inf_past_import;
#define gpr_inf_past gpr_inf_past_import
typedef void(*gpr_time_init_type)(void);
extern gpr_time_init_type gpr_time_init_import;
#define gpr_time_init gpr_time_init_import
typedef gpr_timespec(*gpr_now_type)(gpr_clock_type clock);
extern gpr_now_type gpr_now_import;
#define gpr_now gpr_now_import
typedef gpr_timespec(*gpr_convert_clock_type_type)(gpr_timespec t, gpr_clock_type target_clock);
extern gpr_convert_clock_type_type gpr_convert_clock_type_import;
#define gpr_convert_clock_type gpr_convert_clock_type_import
typedef int(*gpr_time_cmp_type)(gpr_timespec a, gpr_timespec b);
extern gpr_time_cmp_type gpr_time_cmp_import;
#define gpr_time_cmp gpr_time_cmp_import
typedef gpr_timespec(*gpr_time_max_type)(gpr_timespec a, gpr_timespec b);
extern gpr_time_max_type gpr_time_max_import;
#define gpr_time_max gpr_time_max_import
typedef gpr_timespec(*gpr_time_min_type)(gpr_timespec a, gpr_timespec b);
extern gpr_time_min_type gpr_time_min_import;
#define gpr_time_min gpr_time_min_import
typedef gpr_timespec(*gpr_time_add_type)(gpr_timespec a, gpr_timespec b);
extern gpr_time_add_type gpr_time_add_import;
#define gpr_time_add gpr_time_add_import
typedef gpr_timespec(*gpr_time_sub_type)(gpr_timespec a, gpr_timespec b);
extern gpr_time_sub_type gpr_time_sub_import;
#define gpr_time_sub gpr_time_sub_import
typedef gpr_timespec(*gpr_time_from_micros_type)(int64_t x, gpr_clock_type clock_type);
extern gpr_time_from_micros_type gpr_time_from_micros_import;
#define gpr_time_from_micros gpr_time_from_micros_import
typedef gpr_timespec(*gpr_time_from_nanos_type)(int64_t x, gpr_clock_type clock_type);
extern gpr_time_from_nanos_type gpr_time_from_nanos_import;
#define gpr_time_from_nanos gpr_time_from_nanos_import
typedef gpr_timespec(*gpr_time_from_millis_type)(int64_t x, gpr_clock_type clock_type);
extern gpr_time_from_millis_type gpr_time_from_millis_import;
#define gpr_time_from_millis gpr_time_from_millis_import
typedef gpr_timespec(*gpr_time_from_seconds_type)(int64_t x, gpr_clock_type clock_type);
extern gpr_time_from_seconds_type gpr_time_from_seconds_import;
#define gpr_time_from_seconds gpr_time_from_seconds_import
typedef gpr_timespec(*gpr_time_from_minutes_type)(int64_t x, gpr_clock_type clock_type);
extern gpr_time_from_minutes_type gpr_time_from_minutes_import;
#define gpr_time_from_minutes gpr_time_from_minutes_import
typedef gpr_timespec(*gpr_time_from_hours_type)(int64_t x, gpr_clock_type clock_type);
extern gpr_time_from_hours_type gpr_time_from_hours_import;
#define gpr_time_from_hours gpr_time_from_hours_import
typedef int32_t(*gpr_time_to_millis_type)(gpr_timespec timespec);
extern gpr_time_to_millis_type gpr_time_to_millis_import;
#define gpr_time_to_millis gpr_time_to_millis_import
typedef int(*gpr_time_similar_type)(gpr_timespec a, gpr_timespec b, gpr_timespec threshold);
extern gpr_time_similar_type gpr_time_similar_import;
#define gpr_time_similar gpr_time_similar_import
typedef void(*gpr_sleep_until_type)(gpr_timespec until);
extern gpr_sleep_until_type gpr_sleep_until_import;
#define gpr_sleep_until gpr_sleep_until_import
typedef double(*gpr_timespec_to_micros_type)(gpr_timespec t);
extern gpr_timespec_to_micros_type gpr_timespec_to_micros_import;
#define gpr_timespec_to_micros gpr_timespec_to_micros_import
typedef gpr_avl(*gpr_avl_create_type)(const gpr_avl_vtable *vtable);
extern gpr_avl_create_type gpr_avl_create_import;
#define gpr_avl_create gpr_avl_create_import
typedef gpr_avl(*gpr_avl_ref_type)(gpr_avl avl);
extern gpr_avl_ref_type gpr_avl_ref_import;
#define gpr_avl_ref gpr_avl_ref_import
typedef void(*gpr_avl_unref_type)(gpr_avl avl);
extern gpr_avl_unref_type gpr_avl_unref_import;
#define gpr_avl_unref gpr_avl_unref_import
typedef gpr_avl(*gpr_avl_add_type)(gpr_avl avl, void *key, void *value);
extern gpr_avl_add_type gpr_avl_add_import;
#define gpr_avl_add gpr_avl_add_import
typedef gpr_avl(*gpr_avl_remove_type)(gpr_avl avl, void *key);
extern gpr_avl_remove_type gpr_avl_remove_import;
#define gpr_avl_remove gpr_avl_remove_import
typedef void *(*gpr_avl_get_type)(gpr_avl avl, void *key);
extern gpr_avl_get_type gpr_avl_get_import;
#define gpr_avl_get gpr_avl_get_import
typedef gpr_cmdline *(*gpr_cmdline_create_type)(const char *description);
extern gpr_cmdline_create_type gpr_cmdline_create_import;
#define gpr_cmdline_create gpr_cmdline_create_import
typedef void(*gpr_cmdline_add_int_type)(gpr_cmdline *cl, const char *name, const char *help, int *value);
extern gpr_cmdline_add_int_type gpr_cmdline_add_int_import;
#define gpr_cmdline_add_int gpr_cmdline_add_int_import
typedef void(*gpr_cmdline_add_flag_type)(gpr_cmdline *cl, const char *name, const char *help, int *value);
extern gpr_cmdline_add_flag_type gpr_cmdline_add_flag_import;
#define gpr_cmdline_add_flag gpr_cmdline_add_flag_import
typedef void(*gpr_cmdline_add_string_type)(gpr_cmdline *cl, const char *name, const char *help, char **value);
extern gpr_cmdline_add_string_type gpr_cmdline_add_string_import;
#define gpr_cmdline_add_string gpr_cmdline_add_string_import
typedef void(*gpr_cmdline_on_extra_arg_type)(gpr_cmdline *cl, const char *name, const char *help, void (*on_extra_arg)(void *user_data, const char *arg), void *user_data);
extern gpr_cmdline_on_extra_arg_type gpr_cmdline_on_extra_arg_import;
#define gpr_cmdline_on_extra_arg gpr_cmdline_on_extra_arg_import
typedef void(*gpr_cmdline_set_survive_failure_type)(gpr_cmdline *cl);
extern gpr_cmdline_set_survive_failure_type gpr_cmdline_set_survive_failure_import;
#define gpr_cmdline_set_survive_failure gpr_cmdline_set_survive_failure_import
typedef int(*gpr_cmdline_parse_type)(gpr_cmdline *cl, int argc, char **argv);
extern gpr_cmdline_parse_type gpr_cmdline_parse_import;
#define gpr_cmdline_parse gpr_cmdline_parse_import
typedef void(*gpr_cmdline_destroy_type)(gpr_cmdline *cl);
extern gpr_cmdline_destroy_type gpr_cmdline_destroy_import;
#define gpr_cmdline_destroy gpr_cmdline_destroy_import
typedef char *(*gpr_cmdline_usage_string_type)(gpr_cmdline *cl, const char *argv0);
extern gpr_cmdline_usage_string_type gpr_cmdline_usage_string_import;
#define gpr_cmdline_usage_string gpr_cmdline_usage_string_import
typedef unsigned(*gpr_cpu_num_cores_type)(void);
extern gpr_cpu_num_cores_type gpr_cpu_num_cores_import;
#define gpr_cpu_num_cores gpr_cpu_num_cores_import
typedef unsigned(*gpr_cpu_current_cpu_type)(void);
extern gpr_cpu_current_cpu_type gpr_cpu_current_cpu_import;
#define gpr_cpu_current_cpu gpr_cpu_current_cpu_import
typedef gpr_histogram *(*gpr_histogram_create_type)(double resolution, double max_bucket_start);
extern gpr_histogram_create_type gpr_histogram_create_import;
#define gpr_histogram_create gpr_histogram_create_import
typedef void(*gpr_histogram_destroy_type)(gpr_histogram *h);
extern gpr_histogram_destroy_type gpr_histogram_destroy_import;
#define gpr_histogram_destroy gpr_histogram_destroy_import
typedef void(*gpr_histogram_add_type)(gpr_histogram *h, double x);
extern gpr_histogram_add_type gpr_histogram_add_import;
#define gpr_histogram_add gpr_histogram_add_import
typedef int(*gpr_histogram_merge_type)(gpr_histogram *dst, const gpr_histogram *src);
extern gpr_histogram_merge_type gpr_histogram_merge_import;
#define gpr_histogram_merge gpr_histogram_merge_import
typedef double(*gpr_histogram_percentile_type)(gpr_histogram *histogram, double percentile);
extern gpr_histogram_percentile_type gpr_histogram_percentile_import;
#define gpr_histogram_percentile gpr_histogram_percentile_import
typedef double(*gpr_histogram_mean_type)(gpr_histogram *histogram);
extern gpr_histogram_mean_type gpr_histogram_mean_import;
#define gpr_histogram_mean gpr_histogram_mean_import
typedef double(*gpr_histogram_stddev_type)(gpr_histogram *histogram);
extern gpr_histogram_stddev_type gpr_histogram_stddev_import;
#define gpr_histogram_stddev gpr_histogram_stddev_import
typedef double(*gpr_histogram_variance_type)(gpr_histogram *histogram);
extern gpr_histogram_variance_type gpr_histogram_variance_import;
#define gpr_histogram_variance gpr_histogram_variance_import
typedef double(*gpr_histogram_maximum_type)(gpr_histogram *histogram);
extern gpr_histogram_maximum_type gpr_histogram_maximum_import;
#define gpr_histogram_maximum gpr_histogram_maximum_import
typedef double(*gpr_histogram_minimum_type)(gpr_histogram *histogram);
extern gpr_histogram_minimum_type gpr_histogram_minimum_import;
#define gpr_histogram_minimum gpr_histogram_minimum_import
typedef double(*gpr_histogram_count_type)(gpr_histogram *histogram);
extern gpr_histogram_count_type gpr_histogram_count_import;
#define gpr_histogram_count gpr_histogram_count_import
typedef double(*gpr_histogram_sum_type)(gpr_histogram *histogram);
extern gpr_histogram_sum_type gpr_histogram_sum_import;
#define gpr_histogram_sum gpr_histogram_sum_import
typedef double(*gpr_histogram_sum_of_squares_type)(gpr_histogram *histogram);
extern gpr_histogram_sum_of_squares_type gpr_histogram_sum_of_squares_import;
#define gpr_histogram_sum_of_squares gpr_histogram_sum_of_squares_import
typedef const uint32_t *(*gpr_histogram_get_contents_type)(gpr_histogram *histogram, size_t *count);
extern gpr_histogram_get_contents_type gpr_histogram_get_contents_import;
#define gpr_histogram_get_contents gpr_histogram_get_contents_import
typedef void(*gpr_histogram_merge_contents_type)(gpr_histogram *histogram, const uint32_t *data, size_t data_count, double min_seen, double max_seen, double sum, double sum_of_squares, double count);
extern gpr_histogram_merge_contents_type gpr_histogram_merge_contents_import;
#define gpr_histogram_merge_contents gpr_histogram_merge_contents_import
typedef int(*gpr_join_host_port_type)(char **out, const char *host, int port);
extern gpr_join_host_port_type gpr_join_host_port_import;
#define gpr_join_host_port gpr_join_host_port_import
typedef int(*gpr_split_host_port_type)(const char *name, char **host, char **port);
extern gpr_split_host_port_type gpr_split_host_port_import;
#define gpr_split_host_port gpr_split_host_port_import
typedef char *(*gpr_format_message_type)(int messageid);
extern gpr_format_message_type gpr_format_message_import;
#define gpr_format_message gpr_format_message_import
typedef char *(*gpr_strdup_type)(const char *src);
extern gpr_strdup_type gpr_strdup_import;
#define gpr_strdup gpr_strdup_import
typedef int(*gpr_asprintf_type)(char **strp, const char *format, ...);
extern gpr_asprintf_type gpr_asprintf_import;
#define gpr_asprintf gpr_asprintf_import
typedef const char *(*gpr_subprocess_binary_extension_type)();
extern gpr_subprocess_binary_extension_type gpr_subprocess_binary_extension_import;
#define gpr_subprocess_binary_extension gpr_subprocess_binary_extension_import
typedef gpr_subprocess *(*gpr_subprocess_create_type)(int argc, const char **argv);
extern gpr_subprocess_create_type gpr_subprocess_create_import;
#define gpr_subprocess_create gpr_subprocess_create_import
typedef void(*gpr_subprocess_destroy_type)(gpr_subprocess *p);
extern gpr_subprocess_destroy_type gpr_subprocess_destroy_import;
#define gpr_subprocess_destroy gpr_subprocess_destroy_import
typedef int(*gpr_subprocess_join_type)(gpr_subprocess *p);
extern gpr_subprocess_join_type gpr_subprocess_join_import;
#define gpr_subprocess_join gpr_subprocess_join_import
typedef void(*gpr_subprocess_interrupt_type)(gpr_subprocess *p);
extern gpr_subprocess_interrupt_type gpr_subprocess_interrupt_import;
#define gpr_subprocess_interrupt gpr_subprocess_interrupt_import
typedef int(*gpr_thd_new_type)(gpr_thd_id *t, void (*thd_body)(void *arg), void *arg, const gpr_thd_options *options);
extern gpr_thd_new_type gpr_thd_new_import;
#define gpr_thd_new gpr_thd_new_import
typedef gpr_thd_options(*gpr_thd_options_default_type)(void);
extern gpr_thd_options_default_type gpr_thd_options_default_import;
#define gpr_thd_options_default gpr_thd_options_default_import
typedef void(*gpr_thd_options_set_detached_type)(gpr_thd_options *options);
extern gpr_thd_options_set_detached_type gpr_thd_options_set_detached_import;
#define gpr_thd_options_set_detached gpr_thd_options_set_detached_import
typedef void(*gpr_thd_options_set_joinable_type)(gpr_thd_options *options);
extern gpr_thd_options_set_joinable_type gpr_thd_options_set_joinable_import;
#define gpr_thd_options_set_joinable gpr_thd_options_set_joinable_import
typedef int(*gpr_thd_options_is_detached_type)(const gpr_thd_options *options);
extern gpr_thd_options_is_detached_type gpr_thd_options_is_detached_import;
#define gpr_thd_options_is_detached gpr_thd_options_is_detached_import
typedef int(*gpr_thd_options_is_joinable_type)(const gpr_thd_options *options);
extern gpr_thd_options_is_joinable_type gpr_thd_options_is_joinable_import;
#define gpr_thd_options_is_joinable gpr_thd_options_is_joinable_import
typedef gpr_thd_id(*gpr_thd_currentid_type)(void);
extern gpr_thd_currentid_type gpr_thd_currentid_import;
#define gpr_thd_currentid gpr_thd_currentid_import
typedef void(*gpr_thd_join_type)(gpr_thd_id t);
extern gpr_thd_join_type gpr_thd_join_import;
#define gpr_thd_join gpr_thd_join_import

void grpc_rb_load_imports(HMODULE library);

#endif /* GPR_WINDOWS */

#endif
