/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/census.h>
#include <grpc/compression.h>
#include <grpc/fork.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/impl/codegen/atm.h>
#include <grpc/impl/codegen/byte_buffer.h>
#include <grpc/impl/codegen/byte_buffer_reader.h>
#include <grpc/impl/codegen/compression_types.h>
#include <grpc/impl/codegen/connectivity_state.h>
#include <grpc/impl/codegen/fork.h>
#include <grpc/impl/codegen/gpr_slice.h>
#include <grpc/impl/codegen/gpr_types.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/impl/codegen/port_platform.h>
#include <grpc/impl/codegen/propagation_bits.h>
#include <grpc/impl/codegen/slice.h>
#include <grpc/impl/codegen/status.h>
#include <grpc/impl/codegen/sync.h>
#include <grpc/impl/codegen/sync_custom.h>
#include <grpc/impl/codegen/sync_generic.h>
#include <grpc/load_reporting.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/cpu.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/sync_custom.h>
#include <grpc/support/sync_generic.h>
#include <grpc/support/thd_id.h>
#include <grpc/support/time.h>
#include <grpc/support/workaround_list.h>

#include <stdio.h>

int main(int argc, char **argv) {
  printf("%lx", (unsigned long) grpc_compression_algorithm_is_message);
  printf("%lx", (unsigned long) grpc_compression_algorithm_is_stream);
  printf("%lx", (unsigned long) grpc_compression_algorithm_parse);
  printf("%lx", (unsigned long) grpc_compression_algorithm_name);
  printf("%lx", (unsigned long) grpc_compression_algorithm_for_level);
  printf("%lx", (unsigned long) grpc_compression_options_init);
  printf("%lx", (unsigned long) grpc_compression_options_enable_algorithm);
  printf("%lx", (unsigned long) grpc_compression_options_disable_algorithm);
  printf("%lx", (unsigned long) grpc_compression_options_is_algorithm_enabled);
  printf("%lx", (unsigned long) grpc_metadata_array_init);
  printf("%lx", (unsigned long) grpc_metadata_array_destroy);
  printf("%lx", (unsigned long) grpc_call_details_init);
  printf("%lx", (unsigned long) grpc_call_details_destroy);
  printf("%lx", (unsigned long) grpc_register_plugin);
  printf("%lx", (unsigned long) grpc_init);
  printf("%lx", (unsigned long) grpc_shutdown);
  printf("%lx", (unsigned long) grpc_version_string);
  printf("%lx", (unsigned long) grpc_g_stands_for);
  printf("%lx", (unsigned long) grpc_completion_queue_factory_lookup);
  printf("%lx", (unsigned long) grpc_completion_queue_create_for_next);
  printf("%lx", (unsigned long) grpc_completion_queue_create_for_pluck);
  printf("%lx", (unsigned long) grpc_completion_queue_create);
  printf("%lx", (unsigned long) grpc_completion_queue_next);
  printf("%lx", (unsigned long) grpc_completion_queue_pluck);
  printf("%lx", (unsigned long) grpc_completion_queue_shutdown);
  printf("%lx", (unsigned long) grpc_completion_queue_destroy);
  printf("%lx", (unsigned long) grpc_completion_queue_thread_local_cache_init);
  printf("%lx", (unsigned long) grpc_completion_queue_thread_local_cache_flush);
  printf("%lx", (unsigned long) grpc_channel_check_connectivity_state);
  printf("%lx", (unsigned long) grpc_channel_num_external_connectivity_watchers);
  printf("%lx", (unsigned long) grpc_channel_watch_connectivity_state);
  printf("%lx", (unsigned long) grpc_channel_support_connectivity_watcher);
  printf("%lx", (unsigned long) grpc_channel_create_call);
  printf("%lx", (unsigned long) grpc_channel_ping);
  printf("%lx", (unsigned long) grpc_channel_register_call);
  printf("%lx", (unsigned long) grpc_channel_create_registered_call);
  printf("%lx", (unsigned long) grpc_call_arena_alloc);
  printf("%lx", (unsigned long) grpc_call_start_batch);
  printf("%lx", (unsigned long) grpc_call_get_peer);
  printf("%lx", (unsigned long) grpc_census_call_set_context);
  printf("%lx", (unsigned long) grpc_census_call_get_context);
  printf("%lx", (unsigned long) grpc_channel_get_target);
  printf("%lx", (unsigned long) grpc_channel_get_info);
  printf("%lx", (unsigned long) grpc_insecure_channel_create);
  printf("%lx", (unsigned long) grpc_lame_client_channel_create);
  printf("%lx", (unsigned long) grpc_channel_destroy);
  printf("%lx", (unsigned long) grpc_channel_get_trace);
  printf("%lx", (unsigned long) grpc_channel_get_uuid);
  printf("%lx", (unsigned long) grpc_call_cancel);
  printf("%lx", (unsigned long) grpc_call_cancel_with_status);
  printf("%lx", (unsigned long) grpc_call_ref);
  printf("%lx", (unsigned long) grpc_call_unref);
  printf("%lx", (unsigned long) grpc_server_request_call);
  printf("%lx", (unsigned long) grpc_server_register_method);
  printf("%lx", (unsigned long) grpc_server_request_registered_call);
  printf("%lx", (unsigned long) grpc_server_create);
  printf("%lx", (unsigned long) grpc_server_register_completion_queue);
  printf("%lx", (unsigned long) grpc_server_add_insecure_http2_port);
  printf("%lx", (unsigned long) grpc_server_start);
  printf("%lx", (unsigned long) grpc_server_shutdown_and_notify);
  printf("%lx", (unsigned long) grpc_server_cancel_all_calls);
  printf("%lx", (unsigned long) grpc_server_destroy);
  printf("%lx", (unsigned long) grpc_tracer_set_enabled);
  printf("%lx", (unsigned long) grpc_header_key_is_legal);
  printf("%lx", (unsigned long) grpc_header_nonbin_value_is_legal);
  printf("%lx", (unsigned long) grpc_is_binary_header);
  printf("%lx", (unsigned long) grpc_call_error_to_string);
  printf("%lx", (unsigned long) grpc_resource_quota_create);
  printf("%lx", (unsigned long) grpc_resource_quota_ref);
  printf("%lx", (unsigned long) grpc_resource_quota_unref);
  printf("%lx", (unsigned long) grpc_resource_quota_resize);
  printf("%lx", (unsigned long) grpc_resource_quota_arg_vtable);
  printf("%lx", (unsigned long) grpc_auth_property_iterator_next);
  printf("%lx", (unsigned long) grpc_auth_context_property_iterator);
  printf("%lx", (unsigned long) grpc_auth_context_peer_identity);
  printf("%lx", (unsigned long) grpc_auth_context_find_properties_by_name);
  printf("%lx", (unsigned long) grpc_auth_context_peer_identity_property_name);
  printf("%lx", (unsigned long) grpc_auth_context_peer_is_authenticated);
  printf("%lx", (unsigned long) grpc_call_auth_context);
  printf("%lx", (unsigned long) grpc_auth_context_release);
  printf("%lx", (unsigned long) grpc_auth_context_add_property);
  printf("%lx", (unsigned long) grpc_auth_context_add_cstring_property);
  printf("%lx", (unsigned long) grpc_auth_context_set_peer_identity_property_name);
  printf("%lx", (unsigned long) grpc_ssl_session_cache_create_lru);
  printf("%lx", (unsigned long) grpc_ssl_session_cache_destroy);
  printf("%lx", (unsigned long) grpc_ssl_session_cache_create_channel_arg);
  printf("%lx", (unsigned long) grpc_channel_credentials_release);
  printf("%lx", (unsigned long) grpc_google_default_credentials_create);
  printf("%lx", (unsigned long) grpc_set_ssl_roots_override_callback);
  printf("%lx", (unsigned long) grpc_ssl_credentials_create);
  printf("%lx", (unsigned long) grpc_call_credentials_release);
  printf("%lx", (unsigned long) grpc_composite_channel_credentials_create);
  printf("%lx", (unsigned long) grpc_composite_call_credentials_create);
  printf("%lx", (unsigned long) grpc_google_compute_engine_credentials_create);
  printf("%lx", (unsigned long) grpc_max_auth_token_lifetime);
  printf("%lx", (unsigned long) grpc_service_account_jwt_access_credentials_create);
  printf("%lx", (unsigned long) grpc_google_refresh_token_credentials_create);
  printf("%lx", (unsigned long) grpc_access_token_credentials_create);
  printf("%lx", (unsigned long) grpc_google_iam_credentials_create);
  printf("%lx", (unsigned long) grpc_metadata_credentials_create_from_plugin);
  printf("%lx", (unsigned long) grpc_secure_channel_create);
  printf("%lx", (unsigned long) grpc_server_credentials_release);
  printf("%lx", (unsigned long) grpc_ssl_server_certificate_config_create);
  printf("%lx", (unsigned long) grpc_ssl_server_certificate_config_destroy);
  printf("%lx", (unsigned long) grpc_ssl_server_credentials_create);
  printf("%lx", (unsigned long) grpc_ssl_server_credentials_create_ex);
  printf("%lx", (unsigned long) grpc_ssl_server_credentials_create_options_using_config);
  printf("%lx", (unsigned long) grpc_ssl_server_credentials_create_options_using_config_fetcher);
  printf("%lx", (unsigned long) grpc_ssl_server_credentials_options_destroy);
  printf("%lx", (unsigned long) grpc_ssl_server_credentials_create_with_options);
  printf("%lx", (unsigned long) grpc_server_add_secure_http2_port);
  printf("%lx", (unsigned long) grpc_call_set_credentials);
  printf("%lx", (unsigned long) grpc_server_credentials_set_auth_metadata_processor);
  printf("%lx", (unsigned long) grpc_alts_credentials_client_options_create);
  printf("%lx", (unsigned long) grpc_alts_credentials_server_options_create);
  printf("%lx", (unsigned long) grpc_alts_credentials_client_options_add_target_service_account);
  printf("%lx", (unsigned long) grpc_alts_credentials_options_destroy);
  printf("%lx", (unsigned long) grpc_alts_credentials_create);
  printf("%lx", (unsigned long) grpc_alts_server_credentials_create);
  printf("%lx", (unsigned long) grpc_raw_byte_buffer_create);
  printf("%lx", (unsigned long) grpc_raw_compressed_byte_buffer_create);
  printf("%lx", (unsigned long) grpc_byte_buffer_copy);
  printf("%lx", (unsigned long) grpc_byte_buffer_length);
  printf("%lx", (unsigned long) grpc_byte_buffer_destroy);
  printf("%lx", (unsigned long) grpc_byte_buffer_reader_init);
  printf("%lx", (unsigned long) grpc_byte_buffer_reader_destroy);
  printf("%lx", (unsigned long) grpc_byte_buffer_reader_next);
  printf("%lx", (unsigned long) grpc_byte_buffer_reader_readall);
  printf("%lx", (unsigned long) grpc_raw_byte_buffer_from_reader);
  printf("%lx", (unsigned long) grpc_slice_ref);
  printf("%lx", (unsigned long) grpc_slice_unref);
  printf("%lx", (unsigned long) grpc_slice_copy);
  printf("%lx", (unsigned long) grpc_slice_new);
  printf("%lx", (unsigned long) grpc_slice_new_with_user_data);
  printf("%lx", (unsigned long) grpc_slice_new_with_len);
  printf("%lx", (unsigned long) grpc_slice_malloc);
  printf("%lx", (unsigned long) grpc_slice_malloc_large);
  printf("%lx", (unsigned long) grpc_slice_intern);
  printf("%lx", (unsigned long) grpc_slice_from_copied_string);
  printf("%lx", (unsigned long) grpc_slice_from_copied_buffer);
  printf("%lx", (unsigned long) grpc_slice_from_static_string);
  printf("%lx", (unsigned long) grpc_slice_from_static_buffer);
  printf("%lx", (unsigned long) grpc_slice_sub);
  printf("%lx", (unsigned long) grpc_slice_sub_no_ref);
  printf("%lx", (unsigned long) grpc_slice_split_tail);
  printf("%lx", (unsigned long) grpc_slice_split_tail_maybe_ref);
  printf("%lx", (unsigned long) grpc_slice_split_head);
  printf("%lx", (unsigned long) grpc_empty_slice);
  printf("%lx", (unsigned long) grpc_slice_default_hash_impl);
  printf("%lx", (unsigned long) grpc_slice_default_eq_impl);
  printf("%lx", (unsigned long) grpc_slice_eq);
  printf("%lx", (unsigned long) grpc_slice_cmp);
  printf("%lx", (unsigned long) grpc_slice_str_cmp);
  printf("%lx", (unsigned long) grpc_slice_buf_start_eq);
  printf("%lx", (unsigned long) grpc_slice_rchr);
  printf("%lx", (unsigned long) grpc_slice_chr);
  printf("%lx", (unsigned long) grpc_slice_slice);
  printf("%lx", (unsigned long) grpc_slice_hash);
  printf("%lx", (unsigned long) grpc_slice_is_equivalent);
  printf("%lx", (unsigned long) grpc_slice_dup);
  printf("%lx", (unsigned long) grpc_slice_to_c_string);
  printf("%lx", (unsigned long) grpc_slice_buffer_init);
  printf("%lx", (unsigned long) grpc_slice_buffer_destroy);
  printf("%lx", (unsigned long) grpc_slice_buffer_add);
  printf("%lx", (unsigned long) grpc_slice_buffer_add_indexed);
  printf("%lx", (unsigned long) grpc_slice_buffer_addn);
  printf("%lx", (unsigned long) grpc_slice_buffer_tiny_add);
  printf("%lx", (unsigned long) grpc_slice_buffer_pop);
  printf("%lx", (unsigned long) grpc_slice_buffer_reset_and_unref);
  printf("%lx", (unsigned long) grpc_slice_buffer_swap);
  printf("%lx", (unsigned long) grpc_slice_buffer_move_into);
  printf("%lx", (unsigned long) grpc_slice_buffer_trim_end);
  printf("%lx", (unsigned long) grpc_slice_buffer_move_first);
  printf("%lx", (unsigned long) grpc_slice_buffer_move_first_no_ref);
  printf("%lx", (unsigned long) grpc_slice_buffer_move_first_into_buffer);
  printf("%lx", (unsigned long) grpc_slice_buffer_take_first);
  printf("%lx", (unsigned long) grpc_slice_buffer_undo_take_first);
  printf("%lx", (unsigned long) gpr_malloc);
  printf("%lx", (unsigned long) gpr_zalloc);
  printf("%lx", (unsigned long) gpr_free);
  printf("%lx", (unsigned long) gpr_realloc);
  printf("%lx", (unsigned long) gpr_malloc_aligned);
  printf("%lx", (unsigned long) gpr_free_aligned);
  printf("%lx", (unsigned long) gpr_set_allocation_functions);
  printf("%lx", (unsigned long) gpr_get_allocation_functions);
  printf("%lx", (unsigned long) gpr_cpu_num_cores);
  printf("%lx", (unsigned long) gpr_cpu_current_cpu);
  printf("%lx", (unsigned long) gpr_log_severity_string);
  printf("%lx", (unsigned long) gpr_log);
  printf("%lx", (unsigned long) gpr_should_log);
  printf("%lx", (unsigned long) gpr_log_message);
  printf("%lx", (unsigned long) gpr_set_log_verbosity);
  printf("%lx", (unsigned long) gpr_log_verbosity_init);
  printf("%lx", (unsigned long) gpr_set_log_function);
  printf("%lx", (unsigned long) gpr_strdup);
  printf("%lx", (unsigned long) gpr_asprintf);
  printf("%lx", (unsigned long) gpr_mu_init);
  printf("%lx", (unsigned long) gpr_mu_destroy);
  printf("%lx", (unsigned long) gpr_mu_lock);
  printf("%lx", (unsigned long) gpr_mu_unlock);
  printf("%lx", (unsigned long) gpr_mu_trylock);
  printf("%lx", (unsigned long) gpr_cv_init);
  printf("%lx", (unsigned long) gpr_cv_destroy);
  printf("%lx", (unsigned long) gpr_cv_wait);
  printf("%lx", (unsigned long) gpr_cv_signal);
  printf("%lx", (unsigned long) gpr_cv_broadcast);
  printf("%lx", (unsigned long) gpr_once_init);
  printf("%lx", (unsigned long) gpr_event_init);
  printf("%lx", (unsigned long) gpr_event_set);
  printf("%lx", (unsigned long) gpr_event_get);
  printf("%lx", (unsigned long) gpr_event_wait);
  printf("%lx", (unsigned long) gpr_ref_init);
  printf("%lx", (unsigned long) gpr_ref);
  printf("%lx", (unsigned long) gpr_ref_non_zero);
  printf("%lx", (unsigned long) gpr_refn);
  printf("%lx", (unsigned long) gpr_unref);
  printf("%lx", (unsigned long) gpr_ref_is_unique);
  printf("%lx", (unsigned long) gpr_stats_init);
  printf("%lx", (unsigned long) gpr_stats_inc);
  printf("%lx", (unsigned long) gpr_stats_read);
  printf("%lx", (unsigned long) gpr_thd_currentid);
  printf("%lx", (unsigned long) gpr_time_0);
  printf("%lx", (unsigned long) gpr_inf_future);
  printf("%lx", (unsigned long) gpr_inf_past);
  printf("%lx", (unsigned long) gpr_time_init);
  printf("%lx", (unsigned long) gpr_now);
  printf("%lx", (unsigned long) gpr_convert_clock_type);
  printf("%lx", (unsigned long) gpr_time_cmp);
  printf("%lx", (unsigned long) gpr_time_max);
  printf("%lx", (unsigned long) gpr_time_min);
  printf("%lx", (unsigned long) gpr_time_add);
  printf("%lx", (unsigned long) gpr_time_sub);
  printf("%lx", (unsigned long) gpr_time_from_micros);
  printf("%lx", (unsigned long) gpr_time_from_nanos);
  printf("%lx", (unsigned long) gpr_time_from_millis);
  printf("%lx", (unsigned long) gpr_time_from_seconds);
  printf("%lx", (unsigned long) gpr_time_from_minutes);
  printf("%lx", (unsigned long) gpr_time_from_hours);
  printf("%lx", (unsigned long) gpr_time_to_millis);
  printf("%lx", (unsigned long) gpr_time_similar);
  printf("%lx", (unsigned long) gpr_sleep_until);
  printf("%lx", (unsigned long) gpr_timespec_to_micros);
  return 0;
}
