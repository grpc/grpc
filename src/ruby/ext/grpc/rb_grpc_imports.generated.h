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

#ifndef GRPC_RB_GRPC_IMPORTS_H_
#define GRPC_RB_GRPC_IMPORTS_H_

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS

#include <grpc/byte_buffer.h>
#include <grpc/compression.h>
#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_posix.h>
#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/cpu.h>
#include <grpc/support/log.h>
#include <grpc/support/log_windows.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd_id.h>
#include <grpc/support/time.h>
#include <windows.h>

typedef grpc_byte_buffer* (*grpc_raw_byte_buffer_create_type)(
    grpc_slice* slices, size_t nslices);
extern grpc_raw_byte_buffer_create_type grpc_raw_byte_buffer_create_import;
#define grpc_raw_byte_buffer_create grpc_raw_byte_buffer_create_import
typedef grpc_byte_buffer* (*grpc_raw_compressed_byte_buffer_create_type)(
    grpc_slice* slices, size_t nslices, grpc_compression_algorithm compression);
extern grpc_raw_compressed_byte_buffer_create_type
    grpc_raw_compressed_byte_buffer_create_import;
#define grpc_raw_compressed_byte_buffer_create \
  grpc_raw_compressed_byte_buffer_create_import
typedef grpc_byte_buffer* (*grpc_byte_buffer_copy_type)(grpc_byte_buffer* bb);
extern grpc_byte_buffer_copy_type grpc_byte_buffer_copy_import;
#define grpc_byte_buffer_copy grpc_byte_buffer_copy_import
typedef size_t (*grpc_byte_buffer_length_type)(grpc_byte_buffer* bb);
extern grpc_byte_buffer_length_type grpc_byte_buffer_length_import;
#define grpc_byte_buffer_length grpc_byte_buffer_length_import
typedef void (*grpc_byte_buffer_destroy_type)(grpc_byte_buffer* bb);
extern grpc_byte_buffer_destroy_type grpc_byte_buffer_destroy_import;
#define grpc_byte_buffer_destroy grpc_byte_buffer_destroy_import
typedef int (*grpc_byte_buffer_reader_init_type)(
    grpc_byte_buffer_reader* reader, grpc_byte_buffer* buffer);
extern grpc_byte_buffer_reader_init_type grpc_byte_buffer_reader_init_import;
#define grpc_byte_buffer_reader_init grpc_byte_buffer_reader_init_import
typedef void (*grpc_byte_buffer_reader_destroy_type)(
    grpc_byte_buffer_reader* reader);
extern grpc_byte_buffer_reader_destroy_type
    grpc_byte_buffer_reader_destroy_import;
#define grpc_byte_buffer_reader_destroy grpc_byte_buffer_reader_destroy_import
typedef int (*grpc_byte_buffer_reader_next_type)(
    grpc_byte_buffer_reader* reader, grpc_slice* slice);
extern grpc_byte_buffer_reader_next_type grpc_byte_buffer_reader_next_import;
#define grpc_byte_buffer_reader_next grpc_byte_buffer_reader_next_import
typedef int (*grpc_byte_buffer_reader_peek_type)(
    grpc_byte_buffer_reader* reader, grpc_slice** slice);
extern grpc_byte_buffer_reader_peek_type grpc_byte_buffer_reader_peek_import;
#define grpc_byte_buffer_reader_peek grpc_byte_buffer_reader_peek_import
typedef grpc_slice (*grpc_byte_buffer_reader_readall_type)(
    grpc_byte_buffer_reader* reader);
extern grpc_byte_buffer_reader_readall_type
    grpc_byte_buffer_reader_readall_import;
#define grpc_byte_buffer_reader_readall grpc_byte_buffer_reader_readall_import
typedef grpc_byte_buffer* (*grpc_raw_byte_buffer_from_reader_type)(
    grpc_byte_buffer_reader* reader);
extern grpc_raw_byte_buffer_from_reader_type
    grpc_raw_byte_buffer_from_reader_import;
#define grpc_raw_byte_buffer_from_reader grpc_raw_byte_buffer_from_reader_import
typedef int (*grpc_compression_algorithm_is_message_type)(
    grpc_compression_algorithm algorithm);
extern grpc_compression_algorithm_is_message_type
    grpc_compression_algorithm_is_message_import;
#define grpc_compression_algorithm_is_message \
  grpc_compression_algorithm_is_message_import
typedef int (*grpc_compression_algorithm_is_stream_type)(
    grpc_compression_algorithm algorithm);
extern grpc_compression_algorithm_is_stream_type
    grpc_compression_algorithm_is_stream_import;
#define grpc_compression_algorithm_is_stream \
  grpc_compression_algorithm_is_stream_import
typedef int (*grpc_compression_algorithm_parse_type)(
    grpc_slice name, grpc_compression_algorithm* algorithm);
extern grpc_compression_algorithm_parse_type
    grpc_compression_algorithm_parse_import;
#define grpc_compression_algorithm_parse grpc_compression_algorithm_parse_import
typedef int (*grpc_compression_algorithm_name_type)(
    grpc_compression_algorithm algorithm, const char** name);
extern grpc_compression_algorithm_name_type
    grpc_compression_algorithm_name_import;
#define grpc_compression_algorithm_name grpc_compression_algorithm_name_import
typedef grpc_compression_algorithm (*grpc_compression_algorithm_for_level_type)(
    grpc_compression_level level, uint32_t accepted_encodings);
extern grpc_compression_algorithm_for_level_type
    grpc_compression_algorithm_for_level_import;
#define grpc_compression_algorithm_for_level \
  grpc_compression_algorithm_for_level_import
typedef void (*grpc_compression_options_init_type)(
    grpc_compression_options* opts);
extern grpc_compression_options_init_type grpc_compression_options_init_import;
#define grpc_compression_options_init grpc_compression_options_init_import
typedef void (*grpc_compression_options_enable_algorithm_type)(
    grpc_compression_options* opts, grpc_compression_algorithm algorithm);
extern grpc_compression_options_enable_algorithm_type
    grpc_compression_options_enable_algorithm_import;
#define grpc_compression_options_enable_algorithm \
  grpc_compression_options_enable_algorithm_import
typedef void (*grpc_compression_options_disable_algorithm_type)(
    grpc_compression_options* opts, grpc_compression_algorithm algorithm);
extern grpc_compression_options_disable_algorithm_type
    grpc_compression_options_disable_algorithm_import;
#define grpc_compression_options_disable_algorithm \
  grpc_compression_options_disable_algorithm_import
typedef int (*grpc_compression_options_is_algorithm_enabled_type)(
    const grpc_compression_options* opts, grpc_compression_algorithm algorithm);
extern grpc_compression_options_is_algorithm_enabled_type
    grpc_compression_options_is_algorithm_enabled_import;
#define grpc_compression_options_is_algorithm_enabled \
  grpc_compression_options_is_algorithm_enabled_import
typedef grpc_call_credentials* (
    *grpc_service_account_jwt_access_credentials_create_type)(
    const char* json_key, gpr_timespec token_lifetime, void* reserved);
extern grpc_service_account_jwt_access_credentials_create_type
    grpc_service_account_jwt_access_credentials_create_import;
#define grpc_service_account_jwt_access_credentials_create \
  grpc_service_account_jwt_access_credentials_create_import
typedef grpc_call_credentials* (*grpc_external_account_credentials_create_type)(
    const char* json_string, const char* scopes_string);
extern grpc_external_account_credentials_create_type
    grpc_external_account_credentials_create_import;
#define grpc_external_account_credentials_create \
  grpc_external_account_credentials_create_import
typedef grpc_call_credentials* (
    *grpc_google_refresh_token_credentials_create_type)(
    const char* json_refresh_token, void* reserved);
extern grpc_google_refresh_token_credentials_create_type
    grpc_google_refresh_token_credentials_create_import;
#define grpc_google_refresh_token_credentials_create \
  grpc_google_refresh_token_credentials_create_import
typedef grpc_call_credentials* (*grpc_access_token_credentials_create_type)(
    const char* access_token, void* reserved);
extern grpc_access_token_credentials_create_type
    grpc_access_token_credentials_create_import;
#define grpc_access_token_credentials_create \
  grpc_access_token_credentials_create_import
typedef grpc_call_credentials* (*grpc_google_iam_credentials_create_type)(
    const char* authorization_token, const char* authority_selector,
    void* reserved);
extern grpc_google_iam_credentials_create_type
    grpc_google_iam_credentials_create_import;
#define grpc_google_iam_credentials_create \
  grpc_google_iam_credentials_create_import
typedef grpc_call_credentials* (*grpc_sts_credentials_create_type)(
    const grpc_sts_credentials_options* options, void* reserved);
extern grpc_sts_credentials_create_type grpc_sts_credentials_create_import;
#define grpc_sts_credentials_create grpc_sts_credentials_create_import
typedef void (*grpc_auth_metadata_context_copy_type)(
    grpc_auth_metadata_context* from, grpc_auth_metadata_context* to);
extern grpc_auth_metadata_context_copy_type
    grpc_auth_metadata_context_copy_import;
#define grpc_auth_metadata_context_copy grpc_auth_metadata_context_copy_import
typedef void (*grpc_auth_metadata_context_reset_type)(
    grpc_auth_metadata_context* context);
extern grpc_auth_metadata_context_reset_type
    grpc_auth_metadata_context_reset_import;
#define grpc_auth_metadata_context_reset grpc_auth_metadata_context_reset_import
typedef grpc_call_credentials* (
    *grpc_metadata_credentials_create_from_plugin_type)(
    grpc_metadata_credentials_plugin plugin,
    grpc_security_level min_security_level, void* reserved);
extern grpc_metadata_credentials_create_from_plugin_type
    grpc_metadata_credentials_create_from_plugin_import;
#define grpc_metadata_credentials_create_from_plugin \
  grpc_metadata_credentials_create_from_plugin_import
typedef void (*grpc_call_credentials_release_type)(
    grpc_call_credentials* creds);
extern grpc_call_credentials_release_type grpc_call_credentials_release_import;
#define grpc_call_credentials_release grpc_call_credentials_release_import
typedef grpc_channel_credentials* (
    *grpc_google_default_credentials_create_type)(
    grpc_call_credentials* call_credentials);
extern grpc_google_default_credentials_create_type
    grpc_google_default_credentials_create_import;
#define grpc_google_default_credentials_create \
  grpc_google_default_credentials_create_import
typedef grpc_ssl_server_certificate_config* (
    *grpc_ssl_server_certificate_config_create_type)(
    const char* pem_root_certs,
    const grpc_ssl_pem_key_cert_pair* pem_key_cert_pairs,
    size_t num_key_cert_pairs);
extern grpc_ssl_server_certificate_config_create_type
    grpc_ssl_server_certificate_config_create_import;
#define grpc_ssl_server_certificate_config_create \
  grpc_ssl_server_certificate_config_create_import
typedef void (*grpc_ssl_server_certificate_config_destroy_type)(
    grpc_ssl_server_certificate_config* config);
extern grpc_ssl_server_certificate_config_destroy_type
    grpc_ssl_server_certificate_config_destroy_import;
#define grpc_ssl_server_certificate_config_destroy \
  grpc_ssl_server_certificate_config_destroy_import
typedef grpc_channel_credentials* (*grpc_ssl_credentials_create_type)(
    const char* pem_root_certs, grpc_ssl_pem_key_cert_pair* pem_key_cert_pair,
    const verify_peer_options* verify_options, void* reserved);
extern grpc_ssl_credentials_create_type grpc_ssl_credentials_create_import;
#define grpc_ssl_credentials_create grpc_ssl_credentials_create_import
typedef grpc_channel_credentials* (*grpc_ssl_credentials_create_ex_type)(
    const char* pem_root_certs, grpc_ssl_pem_key_cert_pair* pem_key_cert_pair,
    const grpc_ssl_verify_peer_options* verify_options, void* reserved);
extern grpc_ssl_credentials_create_ex_type
    grpc_ssl_credentials_create_ex_import;
#define grpc_ssl_credentials_create_ex grpc_ssl_credentials_create_ex_import
typedef grpc_server_credentials* (*grpc_ssl_server_credentials_create_type)(
    const char* pem_root_certs, grpc_ssl_pem_key_cert_pair* pem_key_cert_pairs,
    size_t num_key_cert_pairs, int force_client_auth, void* reserved);
extern grpc_ssl_server_credentials_create_type
    grpc_ssl_server_credentials_create_import;
#define grpc_ssl_server_credentials_create \
  grpc_ssl_server_credentials_create_import
typedef grpc_server_credentials* (*grpc_ssl_server_credentials_create_ex_type)(
    const char* pem_root_certs, grpc_ssl_pem_key_cert_pair* pem_key_cert_pairs,
    size_t num_key_cert_pairs,
    grpc_ssl_client_certificate_request_type client_certificate_request,
    void* reserved);
extern grpc_ssl_server_credentials_create_ex_type
    grpc_ssl_server_credentials_create_ex_import;
#define grpc_ssl_server_credentials_create_ex \
  grpc_ssl_server_credentials_create_ex_import
typedef grpc_ssl_server_credentials_options* (
    *grpc_ssl_server_credentials_create_options_using_config_type)(
    grpc_ssl_client_certificate_request_type client_certificate_request,
    grpc_ssl_server_certificate_config* certificate_config);
extern grpc_ssl_server_credentials_create_options_using_config_type
    grpc_ssl_server_credentials_create_options_using_config_import;
#define grpc_ssl_server_credentials_create_options_using_config \
  grpc_ssl_server_credentials_create_options_using_config_import
typedef grpc_ssl_server_credentials_options* (
    *grpc_ssl_server_credentials_create_options_using_config_fetcher_type)(
    grpc_ssl_client_certificate_request_type client_certificate_request,
    grpc_ssl_server_certificate_config_callback cb, void* user_data);
extern grpc_ssl_server_credentials_create_options_using_config_fetcher_type
    grpc_ssl_server_credentials_create_options_using_config_fetcher_import;
#define grpc_ssl_server_credentials_create_options_using_config_fetcher \
  grpc_ssl_server_credentials_create_options_using_config_fetcher_import
typedef void (*grpc_ssl_server_credentials_options_destroy_type)(
    grpc_ssl_server_credentials_options* options);
extern grpc_ssl_server_credentials_options_destroy_type
    grpc_ssl_server_credentials_options_destroy_import;
#define grpc_ssl_server_credentials_options_destroy \
  grpc_ssl_server_credentials_options_destroy_import
typedef grpc_server_credentials* (
    *grpc_ssl_server_credentials_create_with_options_type)(
    grpc_ssl_server_credentials_options* options);
extern grpc_ssl_server_credentials_create_with_options_type
    grpc_ssl_server_credentials_create_with_options_import;
#define grpc_ssl_server_credentials_create_with_options \
  grpc_ssl_server_credentials_create_with_options_import
typedef void (*grpc_server_credentials_set_auth_metadata_processor_type)(
    grpc_server_credentials* creds, grpc_auth_metadata_processor processor);
extern grpc_server_credentials_set_auth_metadata_processor_type
    grpc_server_credentials_set_auth_metadata_processor_import;
#define grpc_server_credentials_set_auth_metadata_processor \
  grpc_server_credentials_set_auth_metadata_processor_import
typedef grpc_call_credentials* (*grpc_composite_call_credentials_create_type)(
    grpc_call_credentials* creds1, grpc_call_credentials* creds2,
    void* reserved);
extern grpc_composite_call_credentials_create_type
    grpc_composite_call_credentials_create_import;
#define grpc_composite_call_credentials_create \
  grpc_composite_call_credentials_create_import
typedef grpc_call_credentials* (
    *grpc_google_compute_engine_credentials_create_type)(void* reserved);
extern grpc_google_compute_engine_credentials_create_type
    grpc_google_compute_engine_credentials_create_import;
#define grpc_google_compute_engine_credentials_create \
  grpc_google_compute_engine_credentials_create_import
typedef grpc_channel_credentials* (
    *grpc_composite_channel_credentials_create_type)(
    grpc_channel_credentials* channel_creds, grpc_call_credentials* call_creds,
    void* reserved);
extern grpc_composite_channel_credentials_create_type
    grpc_composite_channel_credentials_create_import;
#define grpc_composite_channel_credentials_create \
  grpc_composite_channel_credentials_create_import
typedef grpc_alts_credentials_options* (
    *grpc_alts_credentials_client_options_create_type)(void);
extern grpc_alts_credentials_client_options_create_type
    grpc_alts_credentials_client_options_create_import;
#define grpc_alts_credentials_client_options_create \
  grpc_alts_credentials_client_options_create_import
typedef grpc_alts_credentials_options* (
    *grpc_alts_credentials_server_options_create_type)(void);
extern grpc_alts_credentials_server_options_create_type
    grpc_alts_credentials_server_options_create_import;
#define grpc_alts_credentials_server_options_create \
  grpc_alts_credentials_server_options_create_import
typedef void (
    *grpc_alts_credentials_client_options_add_target_service_account_type)(
    grpc_alts_credentials_options* options, const char* service_account);
extern grpc_alts_credentials_client_options_add_target_service_account_type
    grpc_alts_credentials_client_options_add_target_service_account_import;
#define grpc_alts_credentials_client_options_add_target_service_account \
  grpc_alts_credentials_client_options_add_target_service_account_import
typedef void (*grpc_alts_credentials_options_destroy_type)(
    grpc_alts_credentials_options* options);
extern grpc_alts_credentials_options_destroy_type
    grpc_alts_credentials_options_destroy_import;
#define grpc_alts_credentials_options_destroy \
  grpc_alts_credentials_options_destroy_import
typedef grpc_channel_credentials* (*grpc_alts_credentials_create_type)(
    const grpc_alts_credentials_options* options);
extern grpc_alts_credentials_create_type grpc_alts_credentials_create_import;
#define grpc_alts_credentials_create grpc_alts_credentials_create_import
typedef grpc_server_credentials* (*grpc_alts_server_credentials_create_type)(
    const grpc_alts_credentials_options* options);
extern grpc_alts_server_credentials_create_type
    grpc_alts_server_credentials_create_import;
#define grpc_alts_server_credentials_create \
  grpc_alts_server_credentials_create_import
typedef grpc_tls_identity_pairs* (*grpc_tls_identity_pairs_create_type)();
extern grpc_tls_identity_pairs_create_type
    grpc_tls_identity_pairs_create_import;
#define grpc_tls_identity_pairs_create grpc_tls_identity_pairs_create_import
typedef void (*grpc_tls_identity_pairs_add_pair_type)(
    grpc_tls_identity_pairs* pairs, const char* private_key,
    const char* cert_chain);
extern grpc_tls_identity_pairs_add_pair_type
    grpc_tls_identity_pairs_add_pair_import;
#define grpc_tls_identity_pairs_add_pair grpc_tls_identity_pairs_add_pair_import
typedef void (*grpc_tls_identity_pairs_destroy_type)(
    grpc_tls_identity_pairs* pairs);
extern grpc_tls_identity_pairs_destroy_type
    grpc_tls_identity_pairs_destroy_import;
#define grpc_tls_identity_pairs_destroy grpc_tls_identity_pairs_destroy_import
typedef grpc_tls_certificate_provider* (
    *grpc_tls_certificate_provider_static_data_create_type)(
    const char* root_certificate, grpc_tls_identity_pairs* pem_key_cert_pairs);
extern grpc_tls_certificate_provider_static_data_create_type
    grpc_tls_certificate_provider_static_data_create_import;
#define grpc_tls_certificate_provider_static_data_create \
  grpc_tls_certificate_provider_static_data_create_import
typedef grpc_tls_certificate_provider* (
    *grpc_tls_certificate_provider_file_watcher_create_type)(
    const char* private_key_path, const char* identity_certificate_path,
    const char* root_cert_path, unsigned int refresh_interval_sec);
extern grpc_tls_certificate_provider_file_watcher_create_type
    grpc_tls_certificate_provider_file_watcher_create_import;
#define grpc_tls_certificate_provider_file_watcher_create \
  grpc_tls_certificate_provider_file_watcher_create_import
typedef void (*grpc_tls_certificate_provider_release_type)(
    grpc_tls_certificate_provider* provider);
extern grpc_tls_certificate_provider_release_type
    grpc_tls_certificate_provider_release_import;
#define grpc_tls_certificate_provider_release \
  grpc_tls_certificate_provider_release_import
typedef grpc_tls_credentials_options* (
    *grpc_tls_credentials_options_create_type)(void);
extern grpc_tls_credentials_options_create_type
    grpc_tls_credentials_options_create_import;
#define grpc_tls_credentials_options_create \
  grpc_tls_credentials_options_create_import
typedef void (*grpc_tls_credentials_options_set_min_tls_version_type)(
    grpc_tls_credentials_options* options, grpc_tls_version min_tls_version);
extern grpc_tls_credentials_options_set_min_tls_version_type
    grpc_tls_credentials_options_set_min_tls_version_import;
#define grpc_tls_credentials_options_set_min_tls_version \
  grpc_tls_credentials_options_set_min_tls_version_import
typedef void (*grpc_tls_credentials_options_set_max_tls_version_type)(
    grpc_tls_credentials_options* options, grpc_tls_version max_tls_version);
extern grpc_tls_credentials_options_set_max_tls_version_type
    grpc_tls_credentials_options_set_max_tls_version_import;
#define grpc_tls_credentials_options_set_max_tls_version \
  grpc_tls_credentials_options_set_max_tls_version_import
typedef grpc_tls_credentials_options* (*grpc_tls_credentials_options_copy_type)(
    grpc_tls_credentials_options* options);
extern grpc_tls_credentials_options_copy_type
    grpc_tls_credentials_options_copy_import;
#define grpc_tls_credentials_options_copy \
  grpc_tls_credentials_options_copy_import
typedef void (*grpc_tls_credentials_options_destroy_type)(
    grpc_tls_credentials_options* options);
extern grpc_tls_credentials_options_destroy_type
    grpc_tls_credentials_options_destroy_import;
#define grpc_tls_credentials_options_destroy \
  grpc_tls_credentials_options_destroy_import
typedef void (*grpc_tls_credentials_options_set_certificate_provider_type)(
    grpc_tls_credentials_options* options,
    grpc_tls_certificate_provider* provider);
extern grpc_tls_credentials_options_set_certificate_provider_type
    grpc_tls_credentials_options_set_certificate_provider_import;
#define grpc_tls_credentials_options_set_certificate_provider \
  grpc_tls_credentials_options_set_certificate_provider_import
typedef void (*grpc_tls_credentials_options_watch_root_certs_type)(
    grpc_tls_credentials_options* options);
extern grpc_tls_credentials_options_watch_root_certs_type
    grpc_tls_credentials_options_watch_root_certs_import;
#define grpc_tls_credentials_options_watch_root_certs \
  grpc_tls_credentials_options_watch_root_certs_import
typedef void (*grpc_tls_credentials_options_set_root_cert_name_type)(
    grpc_tls_credentials_options* options, const char* root_cert_name);
extern grpc_tls_credentials_options_set_root_cert_name_type
    grpc_tls_credentials_options_set_root_cert_name_import;
#define grpc_tls_credentials_options_set_root_cert_name \
  grpc_tls_credentials_options_set_root_cert_name_import
typedef void (*grpc_tls_credentials_options_watch_identity_key_cert_pairs_type)(
    grpc_tls_credentials_options* options);
extern grpc_tls_credentials_options_watch_identity_key_cert_pairs_type
    grpc_tls_credentials_options_watch_identity_key_cert_pairs_import;
#define grpc_tls_credentials_options_watch_identity_key_cert_pairs \
  grpc_tls_credentials_options_watch_identity_key_cert_pairs_import
typedef void (*grpc_tls_credentials_options_set_identity_cert_name_type)(
    grpc_tls_credentials_options* options, const char* identity_cert_name);
extern grpc_tls_credentials_options_set_identity_cert_name_type
    grpc_tls_credentials_options_set_identity_cert_name_import;
#define grpc_tls_credentials_options_set_identity_cert_name \
  grpc_tls_credentials_options_set_identity_cert_name_import
typedef void (*grpc_tls_credentials_options_set_cert_request_type_type)(
    grpc_tls_credentials_options* options,
    grpc_ssl_client_certificate_request_type type);
extern grpc_tls_credentials_options_set_cert_request_type_type
    grpc_tls_credentials_options_set_cert_request_type_import;
#define grpc_tls_credentials_options_set_cert_request_type \
  grpc_tls_credentials_options_set_cert_request_type_import
typedef void (*grpc_tls_credentials_options_set_crl_directory_type)(
    grpc_tls_credentials_options* options, const char* crl_directory);
extern grpc_tls_credentials_options_set_crl_directory_type
    grpc_tls_credentials_options_set_crl_directory_import;
#define grpc_tls_credentials_options_set_crl_directory \
  grpc_tls_credentials_options_set_crl_directory_import
typedef void (*grpc_tls_credentials_options_set_verify_server_cert_type)(
    grpc_tls_credentials_options* options, int verify_server_cert);
extern grpc_tls_credentials_options_set_verify_server_cert_type
    grpc_tls_credentials_options_set_verify_server_cert_import;
#define grpc_tls_credentials_options_set_verify_server_cert \
  grpc_tls_credentials_options_set_verify_server_cert_import
typedef void (*grpc_tls_credentials_options_set_send_client_ca_list_type)(
    grpc_tls_credentials_options* options, bool send_client_ca_list);
extern grpc_tls_credentials_options_set_send_client_ca_list_type
    grpc_tls_credentials_options_set_send_client_ca_list_import;
#define grpc_tls_credentials_options_set_send_client_ca_list \
  grpc_tls_credentials_options_set_send_client_ca_list_import
typedef grpc_ssl_session_cache* (*grpc_ssl_session_cache_create_lru_type)(
    size_t capacity);
extern grpc_ssl_session_cache_create_lru_type
    grpc_ssl_session_cache_create_lru_import;
#define grpc_ssl_session_cache_create_lru \
  grpc_ssl_session_cache_create_lru_import
typedef void (*grpc_ssl_session_cache_destroy_type)(
    grpc_ssl_session_cache* cache);
extern grpc_ssl_session_cache_destroy_type
    grpc_ssl_session_cache_destroy_import;
#define grpc_ssl_session_cache_destroy grpc_ssl_session_cache_destroy_import
typedef grpc_arg (*grpc_ssl_session_cache_create_channel_arg_type)(
    grpc_ssl_session_cache* cache);
extern grpc_ssl_session_cache_create_channel_arg_type
    grpc_ssl_session_cache_create_channel_arg_import;
#define grpc_ssl_session_cache_create_channel_arg \
  grpc_ssl_session_cache_create_channel_arg_import
typedef void (*grpc_set_ssl_roots_override_callback_type)(
    grpc_ssl_roots_override_callback cb);
extern grpc_set_ssl_roots_override_callback_type
    grpc_set_ssl_roots_override_callback_import;
#define grpc_set_ssl_roots_override_callback \
  grpc_set_ssl_roots_override_callback_import
typedef gpr_timespec (*grpc_max_auth_token_lifetime_type)(void);
extern grpc_max_auth_token_lifetime_type grpc_max_auth_token_lifetime_import;
#define grpc_max_auth_token_lifetime grpc_max_auth_token_lifetime_import
typedef grpc_channel_credentials* (*grpc_insecure_credentials_create_type)();
extern grpc_insecure_credentials_create_type
    grpc_insecure_credentials_create_import;
#define grpc_insecure_credentials_create grpc_insecure_credentials_create_import
typedef grpc_server_credentials* (
    *grpc_insecure_server_credentials_create_type)();
extern grpc_insecure_server_credentials_create_type
    grpc_insecure_server_credentials_create_import;
#define grpc_insecure_server_credentials_create \
  grpc_insecure_server_credentials_create_import
typedef grpc_channel_credentials* (*grpc_xds_credentials_create_type)(
    grpc_channel_credentials* fallback_credentials);
extern grpc_xds_credentials_create_type grpc_xds_credentials_create_import;
#define grpc_xds_credentials_create grpc_xds_credentials_create_import
typedef grpc_server_credentials* (*grpc_xds_server_credentials_create_type)(
    grpc_server_credentials* fallback_credentials);
extern grpc_xds_server_credentials_create_type
    grpc_xds_server_credentials_create_import;
#define grpc_xds_server_credentials_create \
  grpc_xds_server_credentials_create_import
typedef grpc_channel_credentials* (*grpc_local_credentials_create_type)(
    grpc_local_connect_type type);
extern grpc_local_credentials_create_type grpc_local_credentials_create_import;
#define grpc_local_credentials_create grpc_local_credentials_create_import
typedef grpc_server_credentials* (*grpc_local_server_credentials_create_type)(
    grpc_local_connect_type type);
extern grpc_local_server_credentials_create_type
    grpc_local_server_credentials_create_import;
#define grpc_local_server_credentials_create \
  grpc_local_server_credentials_create_import
typedef void (*grpc_tls_credentials_options_set_check_call_host_type)(
    grpc_tls_credentials_options* options, int check_call_host);
extern grpc_tls_credentials_options_set_check_call_host_type
    grpc_tls_credentials_options_set_check_call_host_import;
#define grpc_tls_credentials_options_set_check_call_host \
  grpc_tls_credentials_options_set_check_call_host_import
typedef void (
    *grpc_tls_credentials_options_set_tls_session_key_log_file_path_type)(
    grpc_tls_credentials_options* options, const char* path);
extern grpc_tls_credentials_options_set_tls_session_key_log_file_path_type
    grpc_tls_credentials_options_set_tls_session_key_log_file_path_import;
#define grpc_tls_credentials_options_set_tls_session_key_log_file_path \
  grpc_tls_credentials_options_set_tls_session_key_log_file_path_import
typedef void (*grpc_metadata_array_init_type)(grpc_metadata_array* array);
extern grpc_metadata_array_init_type grpc_metadata_array_init_import;
#define grpc_metadata_array_init grpc_metadata_array_init_import
typedef void (*grpc_metadata_array_destroy_type)(grpc_metadata_array* array);
extern grpc_metadata_array_destroy_type grpc_metadata_array_destroy_import;
#define grpc_metadata_array_destroy grpc_metadata_array_destroy_import
typedef void (*grpc_call_details_init_type)(grpc_call_details* details);
extern grpc_call_details_init_type grpc_call_details_init_import;
#define grpc_call_details_init grpc_call_details_init_import
typedef void (*grpc_call_details_destroy_type)(grpc_call_details* details);
extern grpc_call_details_destroy_type grpc_call_details_destroy_import;
#define grpc_call_details_destroy grpc_call_details_destroy_import
typedef void (*grpc_init_type)(void);
extern grpc_init_type grpc_init_import;
#define grpc_init grpc_init_import
typedef void (*grpc_shutdown_type)(void);
extern grpc_shutdown_type grpc_shutdown_import;
#define grpc_shutdown grpc_shutdown_import
typedef int (*grpc_is_initialized_type)(void);
extern grpc_is_initialized_type grpc_is_initialized_import;
#define grpc_is_initialized grpc_is_initialized_import
typedef void (*grpc_shutdown_blocking_type)(void);
extern grpc_shutdown_blocking_type grpc_shutdown_blocking_import;
#define grpc_shutdown_blocking grpc_shutdown_blocking_import
typedef const char* (*grpc_version_string_type)(void);
extern grpc_version_string_type grpc_version_string_import;
#define grpc_version_string grpc_version_string_import
typedef const char* (*grpc_g_stands_for_type)(void);
extern grpc_g_stands_for_type grpc_g_stands_for_import;
#define grpc_g_stands_for grpc_g_stands_for_import
typedef const grpc_completion_queue_factory* (
    *grpc_completion_queue_factory_lookup_type)(
    const grpc_completion_queue_attributes* attributes);
extern grpc_completion_queue_factory_lookup_type
    grpc_completion_queue_factory_lookup_import;
#define grpc_completion_queue_factory_lookup \
  grpc_completion_queue_factory_lookup_import
typedef grpc_completion_queue* (*grpc_completion_queue_create_for_next_type)(
    void* reserved);
extern grpc_completion_queue_create_for_next_type
    grpc_completion_queue_create_for_next_import;
#define grpc_completion_queue_create_for_next \
  grpc_completion_queue_create_for_next_import
typedef grpc_completion_queue* (*grpc_completion_queue_create_for_pluck_type)(
    void* reserved);
extern grpc_completion_queue_create_for_pluck_type
    grpc_completion_queue_create_for_pluck_import;
#define grpc_completion_queue_create_for_pluck \
  grpc_completion_queue_create_for_pluck_import
typedef grpc_completion_queue* (
    *grpc_completion_queue_create_for_callback_type)(
    grpc_completion_queue_functor* shutdown_callback, void* reserved);
extern grpc_completion_queue_create_for_callback_type
    grpc_completion_queue_create_for_callback_import;
#define grpc_completion_queue_create_for_callback \
  grpc_completion_queue_create_for_callback_import
typedef grpc_completion_queue* (*grpc_completion_queue_create_type)(
    const grpc_completion_queue_factory* factory,
    const grpc_completion_queue_attributes* attributes, void* reserved);
extern grpc_completion_queue_create_type grpc_completion_queue_create_import;
#define grpc_completion_queue_create grpc_completion_queue_create_import
typedef grpc_event (*grpc_completion_queue_next_type)(grpc_completion_queue* cq,
                                                      gpr_timespec deadline,
                                                      void* reserved);
extern grpc_completion_queue_next_type grpc_completion_queue_next_import;
#define grpc_completion_queue_next grpc_completion_queue_next_import
typedef grpc_event (*grpc_completion_queue_pluck_type)(
    grpc_completion_queue* cq, void* tag, gpr_timespec deadline,
    void* reserved);
extern grpc_completion_queue_pluck_type grpc_completion_queue_pluck_import;
#define grpc_completion_queue_pluck grpc_completion_queue_pluck_import
typedef void (*grpc_completion_queue_shutdown_type)(grpc_completion_queue* cq);
extern grpc_completion_queue_shutdown_type
    grpc_completion_queue_shutdown_import;
#define grpc_completion_queue_shutdown grpc_completion_queue_shutdown_import
typedef void (*grpc_completion_queue_destroy_type)(grpc_completion_queue* cq);
extern grpc_completion_queue_destroy_type grpc_completion_queue_destroy_import;
#define grpc_completion_queue_destroy grpc_completion_queue_destroy_import
typedef void (*grpc_completion_queue_thread_local_cache_init_type)(
    grpc_completion_queue* cq);
extern grpc_completion_queue_thread_local_cache_init_type
    grpc_completion_queue_thread_local_cache_init_import;
#define grpc_completion_queue_thread_local_cache_init \
  grpc_completion_queue_thread_local_cache_init_import
typedef int (*grpc_completion_queue_thread_local_cache_flush_type)(
    grpc_completion_queue* cq, void** tag, int* ok);
extern grpc_completion_queue_thread_local_cache_flush_type
    grpc_completion_queue_thread_local_cache_flush_import;
#define grpc_completion_queue_thread_local_cache_flush \
  grpc_completion_queue_thread_local_cache_flush_import
typedef grpc_connectivity_state (*grpc_channel_check_connectivity_state_type)(
    grpc_channel* channel, int try_to_connect);
extern grpc_channel_check_connectivity_state_type
    grpc_channel_check_connectivity_state_import;
#define grpc_channel_check_connectivity_state \
  grpc_channel_check_connectivity_state_import
typedef void (*grpc_channel_watch_connectivity_state_type)(
    grpc_channel* channel, grpc_connectivity_state last_observed_state,
    gpr_timespec deadline, grpc_completion_queue* cq, void* tag);
extern grpc_channel_watch_connectivity_state_type
    grpc_channel_watch_connectivity_state_import;
#define grpc_channel_watch_connectivity_state \
  grpc_channel_watch_connectivity_state_import
typedef int (*grpc_channel_support_connectivity_watcher_type)(
    grpc_channel* channel);
extern grpc_channel_support_connectivity_watcher_type
    grpc_channel_support_connectivity_watcher_import;
#define grpc_channel_support_connectivity_watcher \
  grpc_channel_support_connectivity_watcher_import
typedef grpc_call* (*grpc_channel_create_call_type)(
    grpc_channel* channel, grpc_call* parent_call, uint32_t propagation_mask,
    grpc_completion_queue* completion_queue, grpc_slice method,
    const grpc_slice* host, gpr_timespec deadline, void* reserved);
extern grpc_channel_create_call_type grpc_channel_create_call_import;
#define grpc_channel_create_call grpc_channel_create_call_import
typedef void* (*grpc_channel_register_call_type)(grpc_channel* channel,
                                                 const char* method,
                                                 const char* host,
                                                 void* reserved);
extern grpc_channel_register_call_type grpc_channel_register_call_import;
#define grpc_channel_register_call grpc_channel_register_call_import
typedef grpc_call* (*grpc_channel_create_registered_call_type)(
    grpc_channel* channel, grpc_call* parent_call, uint32_t propagation_mask,
    grpc_completion_queue* completion_queue, void* registered_call_handle,
    gpr_timespec deadline, void* reserved);
extern grpc_channel_create_registered_call_type
    grpc_channel_create_registered_call_import;
#define grpc_channel_create_registered_call \
  grpc_channel_create_registered_call_import
typedef void* (*grpc_call_arena_alloc_type)(grpc_call* call, size_t size);
extern grpc_call_arena_alloc_type grpc_call_arena_alloc_import;
#define grpc_call_arena_alloc grpc_call_arena_alloc_import
typedef grpc_call_error (*grpc_call_start_batch_type)(grpc_call* call,
                                                      const grpc_op* ops,
                                                      size_t nops, void* tag,
                                                      void* reserved);
extern grpc_call_start_batch_type grpc_call_start_batch_import;
#define grpc_call_start_batch grpc_call_start_batch_import
typedef char* (*grpc_call_get_peer_type)(grpc_call* call);
extern grpc_call_get_peer_type grpc_call_get_peer_import;
#define grpc_call_get_peer grpc_call_get_peer_import
typedef void (*grpc_census_call_set_context_type)(
    grpc_call* call, struct census_context* context);
extern grpc_census_call_set_context_type grpc_census_call_set_context_import;
#define grpc_census_call_set_context grpc_census_call_set_context_import
typedef struct census_context* (*grpc_census_call_get_context_type)(
    grpc_call* call);
extern grpc_census_call_get_context_type grpc_census_call_get_context_import;
#define grpc_census_call_get_context grpc_census_call_get_context_import
typedef char* (*grpc_channel_get_target_type)(grpc_channel* channel);
extern grpc_channel_get_target_type grpc_channel_get_target_import;
#define grpc_channel_get_target grpc_channel_get_target_import
typedef void (*grpc_channel_get_info_type)(
    grpc_channel* channel, const grpc_channel_info* channel_info);
extern grpc_channel_get_info_type grpc_channel_get_info_import;
#define grpc_channel_get_info grpc_channel_get_info_import
typedef void (*grpc_channel_reset_connect_backoff_type)(grpc_channel* channel);
extern grpc_channel_reset_connect_backoff_type
    grpc_channel_reset_connect_backoff_import;
#define grpc_channel_reset_connect_backoff \
  grpc_channel_reset_connect_backoff_import
typedef void (*grpc_channel_credentials_release_type)(
    grpc_channel_credentials* creds);
extern grpc_channel_credentials_release_type
    grpc_channel_credentials_release_import;
#define grpc_channel_credentials_release grpc_channel_credentials_release_import
typedef void (*grpc_server_credentials_release_type)(
    grpc_server_credentials* creds);
extern grpc_server_credentials_release_type
    grpc_server_credentials_release_import;
#define grpc_server_credentials_release grpc_server_credentials_release_import
typedef grpc_channel* (*grpc_channel_create_type)(
    const char* target, grpc_channel_credentials* creds,
    const grpc_channel_args* args);
extern grpc_channel_create_type grpc_channel_create_import;
#define grpc_channel_create grpc_channel_create_import
typedef grpc_channel* (*grpc_lame_client_channel_create_type)(
    const char* target, grpc_status_code error_code, const char* error_message);
extern grpc_lame_client_channel_create_type
    grpc_lame_client_channel_create_import;
#define grpc_lame_client_channel_create grpc_lame_client_channel_create_import
typedef void (*grpc_channel_destroy_type)(grpc_channel* channel);
extern grpc_channel_destroy_type grpc_channel_destroy_import;
#define grpc_channel_destroy grpc_channel_destroy_import
typedef grpc_call_error (*grpc_call_cancel_type)(grpc_call* call,
                                                 void* reserved);
extern grpc_call_cancel_type grpc_call_cancel_import;
#define grpc_call_cancel grpc_call_cancel_import
typedef grpc_call_error (*grpc_call_cancel_with_status_type)(
    grpc_call* call, grpc_status_code status, const char* description,
    void* reserved);
extern grpc_call_cancel_with_status_type grpc_call_cancel_with_status_import;
#define grpc_call_cancel_with_status grpc_call_cancel_with_status_import
typedef int (*grpc_call_failed_before_recv_message_type)(const grpc_call* c);
extern grpc_call_failed_before_recv_message_type
    grpc_call_failed_before_recv_message_import;
#define grpc_call_failed_before_recv_message \
  grpc_call_failed_before_recv_message_import
typedef void (*grpc_call_ref_type)(grpc_call* call);
extern grpc_call_ref_type grpc_call_ref_import;
#define grpc_call_ref grpc_call_ref_import
typedef void (*grpc_call_unref_type)(grpc_call* call);
extern grpc_call_unref_type grpc_call_unref_import;
#define grpc_call_unref grpc_call_unref_import
typedef grpc_call_error (*grpc_call_set_credentials_type)(
    grpc_call* call, grpc_call_credentials* creds);
extern grpc_call_set_credentials_type grpc_call_set_credentials_import;
#define grpc_call_set_credentials grpc_call_set_credentials_import
typedef grpc_call_error (*grpc_server_request_call_type)(
    grpc_server* server, grpc_call** call, grpc_call_details* details,
    grpc_metadata_array* request_metadata,
    grpc_completion_queue* cq_bound_to_call,
    grpc_completion_queue* cq_for_notification, void* tag_new);
extern grpc_server_request_call_type grpc_server_request_call_import;
#define grpc_server_request_call grpc_server_request_call_import
typedef void* (*grpc_server_register_method_type)(
    grpc_server* server, const char* method, const char* host,
    grpc_server_register_method_payload_handling payload_handling,
    uint32_t flags);
extern grpc_server_register_method_type grpc_server_register_method_import;
#define grpc_server_register_method grpc_server_register_method_import
typedef grpc_call_error (*grpc_server_request_registered_call_type)(
    grpc_server* server, void* registered_method, grpc_call** call,
    gpr_timespec* deadline, grpc_metadata_array* request_metadata,
    grpc_byte_buffer** optional_payload,
    grpc_completion_queue* cq_bound_to_call,
    grpc_completion_queue* cq_for_notification, void* tag_new);
extern grpc_server_request_registered_call_type
    grpc_server_request_registered_call_import;
#define grpc_server_request_registered_call \
  grpc_server_request_registered_call_import
typedef grpc_server* (*grpc_server_create_type)(const grpc_channel_args* args,
                                                void* reserved);
extern grpc_server_create_type grpc_server_create_import;
#define grpc_server_create grpc_server_create_import
typedef void (*grpc_server_register_completion_queue_type)(
    grpc_server* server, grpc_completion_queue* cq, void* reserved);
extern grpc_server_register_completion_queue_type
    grpc_server_register_completion_queue_import;
#define grpc_server_register_completion_queue \
  grpc_server_register_completion_queue_import
typedef grpc_server_config_fetcher* (
    *grpc_server_config_fetcher_xds_create_type)(
    grpc_server_xds_status_notifier notifier, const grpc_channel_args* args);
extern grpc_server_config_fetcher_xds_create_type
    grpc_server_config_fetcher_xds_create_import;
#define grpc_server_config_fetcher_xds_create \
  grpc_server_config_fetcher_xds_create_import
typedef void (*grpc_server_config_fetcher_destroy_type)(
    grpc_server_config_fetcher* config_fetcher);
extern grpc_server_config_fetcher_destroy_type
    grpc_server_config_fetcher_destroy_import;
#define grpc_server_config_fetcher_destroy \
  grpc_server_config_fetcher_destroy_import
typedef void (*grpc_server_set_config_fetcher_type)(
    grpc_server* server, grpc_server_config_fetcher* config_fetcher);
extern grpc_server_set_config_fetcher_type
    grpc_server_set_config_fetcher_import;
#define grpc_server_set_config_fetcher grpc_server_set_config_fetcher_import
typedef int (*grpc_server_add_http2_port_type)(grpc_server* server,
                                               const char* addr,
                                               grpc_server_credentials* creds);
extern grpc_server_add_http2_port_type grpc_server_add_http2_port_import;
#define grpc_server_add_http2_port grpc_server_add_http2_port_import
typedef void (*grpc_server_start_type)(grpc_server* server);
extern grpc_server_start_type grpc_server_start_import;
#define grpc_server_start grpc_server_start_import
typedef void (*grpc_server_shutdown_and_notify_type)(grpc_server* server,
                                                     grpc_completion_queue* cq,
                                                     void* tag);
extern grpc_server_shutdown_and_notify_type
    grpc_server_shutdown_and_notify_import;
#define grpc_server_shutdown_and_notify grpc_server_shutdown_and_notify_import
typedef void (*grpc_server_cancel_all_calls_type)(grpc_server* server);
extern grpc_server_cancel_all_calls_type grpc_server_cancel_all_calls_import;
#define grpc_server_cancel_all_calls grpc_server_cancel_all_calls_import
typedef void (*grpc_server_destroy_type)(grpc_server* server);
extern grpc_server_destroy_type grpc_server_destroy_import;
#define grpc_server_destroy grpc_server_destroy_import
typedef int (*grpc_tracer_set_enabled_type)(const char* name, int enabled);
extern grpc_tracer_set_enabled_type grpc_tracer_set_enabled_import;
#define grpc_tracer_set_enabled grpc_tracer_set_enabled_import
typedef int (*grpc_header_key_is_legal_type)(grpc_slice slice);
extern grpc_header_key_is_legal_type grpc_header_key_is_legal_import;
#define grpc_header_key_is_legal grpc_header_key_is_legal_import
typedef int (*grpc_header_nonbin_value_is_legal_type)(grpc_slice slice);
extern grpc_header_nonbin_value_is_legal_type
    grpc_header_nonbin_value_is_legal_import;
#define grpc_header_nonbin_value_is_legal \
  grpc_header_nonbin_value_is_legal_import
typedef int (*grpc_is_binary_header_type)(grpc_slice slice);
extern grpc_is_binary_header_type grpc_is_binary_header_import;
#define grpc_is_binary_header grpc_is_binary_header_import
typedef const char* (*grpc_call_error_to_string_type)(grpc_call_error error);
extern grpc_call_error_to_string_type grpc_call_error_to_string_import;
#define grpc_call_error_to_string grpc_call_error_to_string_import
typedef grpc_resource_quota* (*grpc_resource_quota_create_type)(
    const char* trace_name);
extern grpc_resource_quota_create_type grpc_resource_quota_create_import;
#define grpc_resource_quota_create grpc_resource_quota_create_import
typedef void (*grpc_resource_quota_ref_type)(
    grpc_resource_quota* resource_quota);
extern grpc_resource_quota_ref_type grpc_resource_quota_ref_import;
#define grpc_resource_quota_ref grpc_resource_quota_ref_import
typedef void (*grpc_resource_quota_unref_type)(
    grpc_resource_quota* resource_quota);
extern grpc_resource_quota_unref_type grpc_resource_quota_unref_import;
#define grpc_resource_quota_unref grpc_resource_quota_unref_import
typedef void (*grpc_resource_quota_resize_type)(
    grpc_resource_quota* resource_quota, size_t new_size);
extern grpc_resource_quota_resize_type grpc_resource_quota_resize_import;
#define grpc_resource_quota_resize grpc_resource_quota_resize_import
typedef void (*grpc_resource_quota_set_max_threads_type)(
    grpc_resource_quota* resource_quota, int new_max_threads);
extern grpc_resource_quota_set_max_threads_type
    grpc_resource_quota_set_max_threads_import;
#define grpc_resource_quota_set_max_threads \
  grpc_resource_quota_set_max_threads_import
typedef grpc_slice (*grpc_dump_xds_configs_type)(void);
extern grpc_dump_xds_configs_type grpc_dump_xds_configs_import;
#define grpc_dump_xds_configs grpc_dump_xds_configs_import
typedef const grpc_arg_pointer_vtable* (*grpc_resource_quota_arg_vtable_type)(
    void);
extern grpc_resource_quota_arg_vtable_type
    grpc_resource_quota_arg_vtable_import;
#define grpc_resource_quota_arg_vtable grpc_resource_quota_arg_vtable_import
typedef char* (*grpc_channelz_get_top_channels_type)(intptr_t start_channel_id);
extern grpc_channelz_get_top_channels_type
    grpc_channelz_get_top_channels_import;
#define grpc_channelz_get_top_channels grpc_channelz_get_top_channels_import
typedef char* (*grpc_channelz_get_servers_type)(intptr_t start_server_id);
extern grpc_channelz_get_servers_type grpc_channelz_get_servers_import;
#define grpc_channelz_get_servers grpc_channelz_get_servers_import
typedef char* (*grpc_channelz_get_server_type)(intptr_t server_id);
extern grpc_channelz_get_server_type grpc_channelz_get_server_import;
#define grpc_channelz_get_server grpc_channelz_get_server_import
typedef char* (*grpc_channelz_get_server_sockets_type)(intptr_t server_id,
                                                       intptr_t start_socket_id,
                                                       intptr_t max_results);
extern grpc_channelz_get_server_sockets_type
    grpc_channelz_get_server_sockets_import;
#define grpc_channelz_get_server_sockets grpc_channelz_get_server_sockets_import
typedef char* (*grpc_channelz_get_channel_type)(intptr_t channel_id);
extern grpc_channelz_get_channel_type grpc_channelz_get_channel_import;
#define grpc_channelz_get_channel grpc_channelz_get_channel_import
typedef char* (*grpc_channelz_get_subchannel_type)(intptr_t subchannel_id);
extern grpc_channelz_get_subchannel_type grpc_channelz_get_subchannel_import;
#define grpc_channelz_get_subchannel grpc_channelz_get_subchannel_import
typedef char* (*grpc_channelz_get_socket_type)(intptr_t socket_id);
extern grpc_channelz_get_socket_type grpc_channelz_get_socket_import;
#define grpc_channelz_get_socket grpc_channelz_get_socket_import
typedef const grpc_arg_pointer_vtable* (
    *grpc_authorization_policy_provider_arg_vtable_type)(void);
extern grpc_authorization_policy_provider_arg_vtable_type
    grpc_authorization_policy_provider_arg_vtable_import;
#define grpc_authorization_policy_provider_arg_vtable \
  grpc_authorization_policy_provider_arg_vtable_import
typedef grpc_channel* (*grpc_channel_create_from_fd_type)(
    const char* target, int fd, grpc_channel_credentials* creds,
    const grpc_channel_args* args);
extern grpc_channel_create_from_fd_type grpc_channel_create_from_fd_import;
#define grpc_channel_create_from_fd grpc_channel_create_from_fd_import
typedef void (*grpc_server_add_channel_from_fd_type)(
    grpc_server* server, int fd, grpc_server_credentials* creds);
extern grpc_server_add_channel_from_fd_type
    grpc_server_add_channel_from_fd_import;
#define grpc_server_add_channel_from_fd grpc_server_add_channel_from_fd_import
typedef const grpc_auth_property* (*grpc_auth_property_iterator_next_type)(
    grpc_auth_property_iterator* it);
extern grpc_auth_property_iterator_next_type
    grpc_auth_property_iterator_next_import;
#define grpc_auth_property_iterator_next grpc_auth_property_iterator_next_import
typedef grpc_auth_property_iterator (*grpc_auth_context_property_iterator_type)(
    const grpc_auth_context* ctx);
extern grpc_auth_context_property_iterator_type
    grpc_auth_context_property_iterator_import;
#define grpc_auth_context_property_iterator \
  grpc_auth_context_property_iterator_import
typedef grpc_auth_property_iterator (*grpc_auth_context_peer_identity_type)(
    const grpc_auth_context* ctx);
extern grpc_auth_context_peer_identity_type
    grpc_auth_context_peer_identity_import;
#define grpc_auth_context_peer_identity grpc_auth_context_peer_identity_import
typedef grpc_auth_property_iterator (
    *grpc_auth_context_find_properties_by_name_type)(
    const grpc_auth_context* ctx, const char* name);
extern grpc_auth_context_find_properties_by_name_type
    grpc_auth_context_find_properties_by_name_import;
#define grpc_auth_context_find_properties_by_name \
  grpc_auth_context_find_properties_by_name_import
typedef const char* (*grpc_auth_context_peer_identity_property_name_type)(
    const grpc_auth_context* ctx);
extern grpc_auth_context_peer_identity_property_name_type
    grpc_auth_context_peer_identity_property_name_import;
#define grpc_auth_context_peer_identity_property_name \
  grpc_auth_context_peer_identity_property_name_import
typedef int (*grpc_auth_context_peer_is_authenticated_type)(
    const grpc_auth_context* ctx);
extern grpc_auth_context_peer_is_authenticated_type
    grpc_auth_context_peer_is_authenticated_import;
#define grpc_auth_context_peer_is_authenticated \
  grpc_auth_context_peer_is_authenticated_import
typedef grpc_auth_context* (*grpc_call_auth_context_type)(grpc_call* call);
extern grpc_call_auth_context_type grpc_call_auth_context_import;
#define grpc_call_auth_context grpc_call_auth_context_import
typedef void (*grpc_auth_context_release_type)(grpc_auth_context* context);
extern grpc_auth_context_release_type grpc_auth_context_release_import;
#define grpc_auth_context_release grpc_auth_context_release_import
typedef void (*grpc_auth_context_add_property_type)(grpc_auth_context* ctx,
                                                    const char* name,
                                                    const char* value,
                                                    size_t value_length);
extern grpc_auth_context_add_property_type
    grpc_auth_context_add_property_import;
#define grpc_auth_context_add_property grpc_auth_context_add_property_import
typedef void (*grpc_auth_context_add_cstring_property_type)(
    grpc_auth_context* ctx, const char* name, const char* value);
extern grpc_auth_context_add_cstring_property_type
    grpc_auth_context_add_cstring_property_import;
#define grpc_auth_context_add_cstring_property \
  grpc_auth_context_add_cstring_property_import
typedef int (*grpc_auth_context_set_peer_identity_property_name_type)(
    grpc_auth_context* ctx, const char* name);
extern grpc_auth_context_set_peer_identity_property_name_type
    grpc_auth_context_set_peer_identity_property_name_import;
#define grpc_auth_context_set_peer_identity_property_name \
  grpc_auth_context_set_peer_identity_property_name_import
typedef grpc_authorization_policy_provider* (
    *grpc_authorization_policy_provider_static_data_create_type)(
    const char* authz_policy, grpc_status_code* code,
    const char** error_details);
extern grpc_authorization_policy_provider_static_data_create_type
    grpc_authorization_policy_provider_static_data_create_import;
#define grpc_authorization_policy_provider_static_data_create \
  grpc_authorization_policy_provider_static_data_create_import
typedef grpc_authorization_policy_provider* (
    *grpc_authorization_policy_provider_file_watcher_create_type)(
    const char* authz_policy_path, unsigned int refresh_interval_sec,
    grpc_status_code* code, const char** error_details);
extern grpc_authorization_policy_provider_file_watcher_create_type
    grpc_authorization_policy_provider_file_watcher_create_import;
#define grpc_authorization_policy_provider_file_watcher_create \
  grpc_authorization_policy_provider_file_watcher_create_import
typedef void (*grpc_authorization_policy_provider_release_type)(
    grpc_authorization_policy_provider* provider);
extern grpc_authorization_policy_provider_release_type
    grpc_authorization_policy_provider_release_import;
#define grpc_authorization_policy_provider_release \
  grpc_authorization_policy_provider_release_import
typedef grpc_slice (*grpc_slice_ref_type)(grpc_slice s);
extern grpc_slice_ref_type grpc_slice_ref_import;
#define grpc_slice_ref grpc_slice_ref_import
typedef void (*grpc_slice_unref_type)(grpc_slice s);
extern grpc_slice_unref_type grpc_slice_unref_import;
#define grpc_slice_unref grpc_slice_unref_import
typedef grpc_slice (*grpc_slice_copy_type)(grpc_slice s);
extern grpc_slice_copy_type grpc_slice_copy_import;
#define grpc_slice_copy grpc_slice_copy_import
typedef grpc_slice (*grpc_slice_new_type)(void* p, size_t len,
                                          void (*destroy)(void*));
extern grpc_slice_new_type grpc_slice_new_import;
#define grpc_slice_new grpc_slice_new_import
typedef grpc_slice (*grpc_slice_new_with_user_data_type)(void* p, size_t len,
                                                         void (*destroy)(void*),
                                                         void* user_data);
extern grpc_slice_new_with_user_data_type grpc_slice_new_with_user_data_import;
#define grpc_slice_new_with_user_data grpc_slice_new_with_user_data_import
typedef grpc_slice (*grpc_slice_new_with_len_type)(void* p, size_t len,
                                                   void (*destroy)(void*,
                                                                   size_t));
extern grpc_slice_new_with_len_type grpc_slice_new_with_len_import;
#define grpc_slice_new_with_len grpc_slice_new_with_len_import
typedef grpc_slice (*grpc_slice_malloc_type)(size_t length);
extern grpc_slice_malloc_type grpc_slice_malloc_import;
#define grpc_slice_malloc grpc_slice_malloc_import
typedef grpc_slice (*grpc_slice_malloc_large_type)(size_t length);
extern grpc_slice_malloc_large_type grpc_slice_malloc_large_import;
#define grpc_slice_malloc_large grpc_slice_malloc_large_import
typedef grpc_slice (*grpc_slice_from_copied_string_type)(const char* source);
extern grpc_slice_from_copied_string_type grpc_slice_from_copied_string_import;
#define grpc_slice_from_copied_string grpc_slice_from_copied_string_import
typedef grpc_slice (*grpc_slice_from_copied_buffer_type)(const char* source,
                                                         size_t len);
extern grpc_slice_from_copied_buffer_type grpc_slice_from_copied_buffer_import;
#define grpc_slice_from_copied_buffer grpc_slice_from_copied_buffer_import
typedef grpc_slice (*grpc_slice_from_static_string_type)(const char* source);
extern grpc_slice_from_static_string_type grpc_slice_from_static_string_import;
#define grpc_slice_from_static_string grpc_slice_from_static_string_import
typedef grpc_slice (*grpc_slice_from_static_buffer_type)(const void* source,
                                                         size_t len);
extern grpc_slice_from_static_buffer_type grpc_slice_from_static_buffer_import;
#define grpc_slice_from_static_buffer grpc_slice_from_static_buffer_import
typedef grpc_slice (*grpc_slice_sub_type)(grpc_slice s, size_t begin,
                                          size_t end);
extern grpc_slice_sub_type grpc_slice_sub_import;
#define grpc_slice_sub grpc_slice_sub_import
typedef grpc_slice (*grpc_slice_sub_no_ref_type)(grpc_slice s, size_t begin,
                                                 size_t end);
extern grpc_slice_sub_no_ref_type grpc_slice_sub_no_ref_import;
#define grpc_slice_sub_no_ref grpc_slice_sub_no_ref_import
typedef grpc_slice (*grpc_slice_split_tail_type)(grpc_slice* s, size_t split);
extern grpc_slice_split_tail_type grpc_slice_split_tail_import;
#define grpc_slice_split_tail grpc_slice_split_tail_import
typedef grpc_slice (*grpc_slice_split_tail_maybe_ref_type)(
    grpc_slice* s, size_t split, grpc_slice_ref_whom ref_whom);
extern grpc_slice_split_tail_maybe_ref_type
    grpc_slice_split_tail_maybe_ref_import;
#define grpc_slice_split_tail_maybe_ref grpc_slice_split_tail_maybe_ref_import
typedef grpc_slice (*grpc_slice_split_head_type)(grpc_slice* s, size_t split);
extern grpc_slice_split_head_type grpc_slice_split_head_import;
#define grpc_slice_split_head grpc_slice_split_head_import
typedef grpc_slice (*grpc_empty_slice_type)(void);
extern grpc_empty_slice_type grpc_empty_slice_import;
#define grpc_empty_slice grpc_empty_slice_import
typedef int (*grpc_slice_eq_type)(grpc_slice a, grpc_slice b);
extern grpc_slice_eq_type grpc_slice_eq_import;
#define grpc_slice_eq grpc_slice_eq_import
typedef int (*grpc_slice_cmp_type)(grpc_slice a, grpc_slice b);
extern grpc_slice_cmp_type grpc_slice_cmp_import;
#define grpc_slice_cmp grpc_slice_cmp_import
typedef int (*grpc_slice_str_cmp_type)(grpc_slice a, const char* b);
extern grpc_slice_str_cmp_type grpc_slice_str_cmp_import;
#define grpc_slice_str_cmp grpc_slice_str_cmp_import
typedef int (*grpc_slice_buf_start_eq_type)(grpc_slice a, const void* b,
                                            size_t blen);
extern grpc_slice_buf_start_eq_type grpc_slice_buf_start_eq_import;
#define grpc_slice_buf_start_eq grpc_slice_buf_start_eq_import
typedef int (*grpc_slice_rchr_type)(grpc_slice s, char c);
extern grpc_slice_rchr_type grpc_slice_rchr_import;
#define grpc_slice_rchr grpc_slice_rchr_import
typedef int (*grpc_slice_chr_type)(grpc_slice s, char c);
extern grpc_slice_chr_type grpc_slice_chr_import;
#define grpc_slice_chr grpc_slice_chr_import
typedef int (*grpc_slice_slice_type)(grpc_slice haystack, grpc_slice needle);
extern grpc_slice_slice_type grpc_slice_slice_import;
#define grpc_slice_slice grpc_slice_slice_import
typedef int (*grpc_slice_is_equivalent_type)(grpc_slice a, grpc_slice b);
extern grpc_slice_is_equivalent_type grpc_slice_is_equivalent_import;
#define grpc_slice_is_equivalent grpc_slice_is_equivalent_import
typedef grpc_slice (*grpc_slice_dup_type)(grpc_slice a);
extern grpc_slice_dup_type grpc_slice_dup_import;
#define grpc_slice_dup grpc_slice_dup_import
typedef char* (*grpc_slice_to_c_string_type)(grpc_slice s);
extern grpc_slice_to_c_string_type grpc_slice_to_c_string_import;
#define grpc_slice_to_c_string grpc_slice_to_c_string_import
typedef void (*grpc_slice_buffer_init_type)(grpc_slice_buffer* sb);
extern grpc_slice_buffer_init_type grpc_slice_buffer_init_import;
#define grpc_slice_buffer_init grpc_slice_buffer_init_import
typedef void (*grpc_slice_buffer_destroy_type)(grpc_slice_buffer* sb);
extern grpc_slice_buffer_destroy_type grpc_slice_buffer_destroy_import;
#define grpc_slice_buffer_destroy grpc_slice_buffer_destroy_import
typedef void (*grpc_slice_buffer_add_type)(grpc_slice_buffer* sb,
                                           grpc_slice slice);
extern grpc_slice_buffer_add_type grpc_slice_buffer_add_import;
#define grpc_slice_buffer_add grpc_slice_buffer_add_import
typedef size_t (*grpc_slice_buffer_add_indexed_type)(grpc_slice_buffer* sb,
                                                     grpc_slice slice);
extern grpc_slice_buffer_add_indexed_type grpc_slice_buffer_add_indexed_import;
#define grpc_slice_buffer_add_indexed grpc_slice_buffer_add_indexed_import
typedef void (*grpc_slice_buffer_addn_type)(grpc_slice_buffer* sb,
                                            grpc_slice* slices, size_t n);
extern grpc_slice_buffer_addn_type grpc_slice_buffer_addn_import;
#define grpc_slice_buffer_addn grpc_slice_buffer_addn_import
typedef uint8_t* (*grpc_slice_buffer_tiny_add_type)(grpc_slice_buffer* sb,
                                                    size_t len);
extern grpc_slice_buffer_tiny_add_type grpc_slice_buffer_tiny_add_import;
#define grpc_slice_buffer_tiny_add grpc_slice_buffer_tiny_add_import
typedef void (*grpc_slice_buffer_pop_type)(grpc_slice_buffer* sb);
extern grpc_slice_buffer_pop_type grpc_slice_buffer_pop_import;
#define grpc_slice_buffer_pop grpc_slice_buffer_pop_import
typedef void (*grpc_slice_buffer_reset_and_unref_type)(grpc_slice_buffer* sb);
extern grpc_slice_buffer_reset_and_unref_type
    grpc_slice_buffer_reset_and_unref_import;
#define grpc_slice_buffer_reset_and_unref \
  grpc_slice_buffer_reset_and_unref_import
typedef void (*grpc_slice_buffer_swap_type)(grpc_slice_buffer* a,
                                            grpc_slice_buffer* b);
extern grpc_slice_buffer_swap_type grpc_slice_buffer_swap_import;
#define grpc_slice_buffer_swap grpc_slice_buffer_swap_import
typedef void (*grpc_slice_buffer_move_into_type)(grpc_slice_buffer* src,
                                                 grpc_slice_buffer* dst);
extern grpc_slice_buffer_move_into_type grpc_slice_buffer_move_into_import;
#define grpc_slice_buffer_move_into grpc_slice_buffer_move_into_import
typedef void (*grpc_slice_buffer_trim_end_type)(grpc_slice_buffer* sb, size_t n,
                                                grpc_slice_buffer* garbage);
extern grpc_slice_buffer_trim_end_type grpc_slice_buffer_trim_end_import;
#define grpc_slice_buffer_trim_end grpc_slice_buffer_trim_end_import
typedef void (*grpc_slice_buffer_move_first_type)(grpc_slice_buffer* src,
                                                  size_t n,
                                                  grpc_slice_buffer* dst);
extern grpc_slice_buffer_move_first_type grpc_slice_buffer_move_first_import;
#define grpc_slice_buffer_move_first grpc_slice_buffer_move_first_import
typedef void (*grpc_slice_buffer_move_first_no_ref_type)(
    grpc_slice_buffer* src, size_t n, grpc_slice_buffer* dst);
extern grpc_slice_buffer_move_first_no_ref_type
    grpc_slice_buffer_move_first_no_ref_import;
#define grpc_slice_buffer_move_first_no_ref \
  grpc_slice_buffer_move_first_no_ref_import
typedef void (*grpc_slice_buffer_move_first_into_buffer_type)(
    grpc_slice_buffer* src, size_t n, void* dst);
extern grpc_slice_buffer_move_first_into_buffer_type
    grpc_slice_buffer_move_first_into_buffer_import;
#define grpc_slice_buffer_move_first_into_buffer \
  grpc_slice_buffer_move_first_into_buffer_import
typedef grpc_slice (*grpc_slice_buffer_take_first_type)(grpc_slice_buffer* sb);
extern grpc_slice_buffer_take_first_type grpc_slice_buffer_take_first_import;
#define grpc_slice_buffer_take_first grpc_slice_buffer_take_first_import
typedef void (*grpc_slice_buffer_undo_take_first_type)(grpc_slice_buffer* sb,
                                                       grpc_slice slice);
extern grpc_slice_buffer_undo_take_first_type
    grpc_slice_buffer_undo_take_first_import;
#define grpc_slice_buffer_undo_take_first \
  grpc_slice_buffer_undo_take_first_import
typedef void* (*gpr_malloc_type)(size_t size);
extern gpr_malloc_type gpr_malloc_import;
#define gpr_malloc gpr_malloc_import
typedef void* (*gpr_zalloc_type)(size_t size);
extern gpr_zalloc_type gpr_zalloc_import;
#define gpr_zalloc gpr_zalloc_import
typedef void (*gpr_free_type)(void* ptr);
extern gpr_free_type gpr_free_import;
#define gpr_free gpr_free_import
typedef void* (*gpr_realloc_type)(void* p, size_t size);
extern gpr_realloc_type gpr_realloc_import;
#define gpr_realloc gpr_realloc_import
typedef void* (*gpr_malloc_aligned_type)(size_t size, size_t alignment);
extern gpr_malloc_aligned_type gpr_malloc_aligned_import;
#define gpr_malloc_aligned gpr_malloc_aligned_import
typedef void (*gpr_free_aligned_type)(void* ptr);
extern gpr_free_aligned_type gpr_free_aligned_import;
#define gpr_free_aligned gpr_free_aligned_import
typedef unsigned (*gpr_cpu_num_cores_type)(void);
extern gpr_cpu_num_cores_type gpr_cpu_num_cores_import;
#define gpr_cpu_num_cores gpr_cpu_num_cores_import
typedef unsigned (*gpr_cpu_current_cpu_type)(void);
extern gpr_cpu_current_cpu_type gpr_cpu_current_cpu_import;
#define gpr_cpu_current_cpu gpr_cpu_current_cpu_import
typedef void (*grpc_absl_log_type)(const char* file, int line,
                                   gpr_log_severity severity,
                                   const char* message_str);
extern grpc_absl_log_type grpc_absl_log_import;
#define grpc_absl_log grpc_absl_log_import
typedef void (*grpc_absl_log_int_type)(const char* file, int line,
                                       gpr_log_severity severity,
                                       const char* message_str, intptr_t num);
extern grpc_absl_log_int_type grpc_absl_log_int_import;
#define grpc_absl_log_int grpc_absl_log_int_import
typedef void (*grpc_absl_log_str_type)(const char* file, int line,
                                       gpr_log_severity severity,
                                       const char* message_str1,
                                       const char* message_str2);
extern grpc_absl_log_str_type grpc_absl_log_str_import;
#define grpc_absl_log_str grpc_absl_log_str_import
typedef void (*gpr_log_verbosity_init_type)(void);
extern gpr_log_verbosity_init_type gpr_log_verbosity_init_import;
#define gpr_log_verbosity_init gpr_log_verbosity_init_import
typedef char* (*gpr_format_message_type)(int messageid);
extern gpr_format_message_type gpr_format_message_import;
#define gpr_format_message gpr_format_message_import
typedef char* (*gpr_strdup_type)(const char* src);
extern gpr_strdup_type gpr_strdup_import;
#define gpr_strdup gpr_strdup_import
typedef int (*gpr_asprintf_type)(char** strp, const char* format, ...)
    GPR_PRINT_FORMAT_CHECK(2, 3);
extern gpr_asprintf_type gpr_asprintf_import;
#define gpr_asprintf gpr_asprintf_import
typedef void (*gpr_mu_init_type)(gpr_mu* mu);
extern gpr_mu_init_type gpr_mu_init_import;
#define gpr_mu_init gpr_mu_init_import
typedef void (*gpr_mu_destroy_type)(gpr_mu* mu);
extern gpr_mu_destroy_type gpr_mu_destroy_import;
#define gpr_mu_destroy gpr_mu_destroy_import
typedef void (*gpr_mu_lock_type)(gpr_mu* mu);
extern gpr_mu_lock_type gpr_mu_lock_import;
#define gpr_mu_lock gpr_mu_lock_import
typedef void (*gpr_mu_unlock_type)(gpr_mu* mu);
extern gpr_mu_unlock_type gpr_mu_unlock_import;
#define gpr_mu_unlock gpr_mu_unlock_import
typedef int (*gpr_mu_trylock_type)(gpr_mu* mu);
extern gpr_mu_trylock_type gpr_mu_trylock_import;
#define gpr_mu_trylock gpr_mu_trylock_import
typedef void (*gpr_cv_init_type)(gpr_cv* cv);
extern gpr_cv_init_type gpr_cv_init_import;
#define gpr_cv_init gpr_cv_init_import
typedef void (*gpr_cv_destroy_type)(gpr_cv* cv);
extern gpr_cv_destroy_type gpr_cv_destroy_import;
#define gpr_cv_destroy gpr_cv_destroy_import
typedef int (*gpr_cv_wait_type)(gpr_cv* cv, gpr_mu* mu,
                                gpr_timespec abs_deadline);
extern gpr_cv_wait_type gpr_cv_wait_import;
#define gpr_cv_wait gpr_cv_wait_import
typedef void (*gpr_cv_signal_type)(gpr_cv* cv);
extern gpr_cv_signal_type gpr_cv_signal_import;
#define gpr_cv_signal gpr_cv_signal_import
typedef void (*gpr_cv_broadcast_type)(gpr_cv* cv);
extern gpr_cv_broadcast_type gpr_cv_broadcast_import;
#define gpr_cv_broadcast gpr_cv_broadcast_import
typedef void (*gpr_once_init_type)(gpr_once* once, void (*init_function)(void));
extern gpr_once_init_type gpr_once_init_import;
#define gpr_once_init gpr_once_init_import
typedef void (*gpr_event_init_type)(gpr_event* ev);
extern gpr_event_init_type gpr_event_init_import;
#define gpr_event_init gpr_event_init_import
typedef void (*gpr_event_set_type)(gpr_event* ev, void* value);
extern gpr_event_set_type gpr_event_set_import;
#define gpr_event_set gpr_event_set_import
typedef void* (*gpr_event_get_type)(gpr_event* ev);
extern gpr_event_get_type gpr_event_get_import;
#define gpr_event_get gpr_event_get_import
typedef void* (*gpr_event_wait_type)(gpr_event* ev, gpr_timespec abs_deadline);
extern gpr_event_wait_type gpr_event_wait_import;
#define gpr_event_wait gpr_event_wait_import
typedef void (*gpr_ref_init_type)(gpr_refcount* r, int n);
extern gpr_ref_init_type gpr_ref_init_import;
#define gpr_ref_init gpr_ref_init_import
typedef void (*gpr_ref_type)(gpr_refcount* r);
extern gpr_ref_type gpr_ref_import;
#define gpr_ref gpr_ref_import
typedef void (*gpr_ref_non_zero_type)(gpr_refcount* r);
extern gpr_ref_non_zero_type gpr_ref_non_zero_import;
#define gpr_ref_non_zero gpr_ref_non_zero_import
typedef void (*gpr_refn_type)(gpr_refcount* r, int n);
extern gpr_refn_type gpr_refn_import;
#define gpr_refn gpr_refn_import
typedef int (*gpr_unref_type)(gpr_refcount* r);
extern gpr_unref_type gpr_unref_import;
#define gpr_unref gpr_unref_import
typedef int (*gpr_ref_is_unique_type)(gpr_refcount* r);
extern gpr_ref_is_unique_type gpr_ref_is_unique_import;
#define gpr_ref_is_unique gpr_ref_is_unique_import
typedef void (*gpr_stats_init_type)(gpr_stats_counter* c, intptr_t n);
extern gpr_stats_init_type gpr_stats_init_import;
#define gpr_stats_init gpr_stats_init_import
typedef void (*gpr_stats_inc_type)(gpr_stats_counter* c, intptr_t inc);
extern gpr_stats_inc_type gpr_stats_inc_import;
#define gpr_stats_inc gpr_stats_inc_import
typedef intptr_t (*gpr_stats_read_type)(const gpr_stats_counter* c);
extern gpr_stats_read_type gpr_stats_read_import;
#define gpr_stats_read gpr_stats_read_import
typedef gpr_thd_id (*gpr_thd_currentid_type)(void);
extern gpr_thd_currentid_type gpr_thd_currentid_import;
#define gpr_thd_currentid gpr_thd_currentid_import
typedef gpr_timespec (*gpr_time_0_type)(gpr_clock_type type);
extern gpr_time_0_type gpr_time_0_import;
#define gpr_time_0 gpr_time_0_import
typedef gpr_timespec (*gpr_inf_future_type)(gpr_clock_type type);
extern gpr_inf_future_type gpr_inf_future_import;
#define gpr_inf_future gpr_inf_future_import
typedef gpr_timespec (*gpr_inf_past_type)(gpr_clock_type type);
extern gpr_inf_past_type gpr_inf_past_import;
#define gpr_inf_past gpr_inf_past_import
typedef void (*gpr_time_init_type)(void);
extern gpr_time_init_type gpr_time_init_import;
#define gpr_time_init gpr_time_init_import
typedef gpr_timespec (*gpr_now_type)(gpr_clock_type clock);
extern gpr_now_type gpr_now_import;
#define gpr_now gpr_now_import
typedef gpr_timespec (*gpr_convert_clock_type_type)(gpr_timespec t,
                                                    gpr_clock_type clock_type);
extern gpr_convert_clock_type_type gpr_convert_clock_type_import;
#define gpr_convert_clock_type gpr_convert_clock_type_import
typedef int (*gpr_time_cmp_type)(gpr_timespec a, gpr_timespec b);
extern gpr_time_cmp_type gpr_time_cmp_import;
#define gpr_time_cmp gpr_time_cmp_import
typedef gpr_timespec (*gpr_time_max_type)(gpr_timespec a, gpr_timespec b);
extern gpr_time_max_type gpr_time_max_import;
#define gpr_time_max gpr_time_max_import
typedef gpr_timespec (*gpr_time_min_type)(gpr_timespec a, gpr_timespec b);
extern gpr_time_min_type gpr_time_min_import;
#define gpr_time_min gpr_time_min_import
typedef gpr_timespec (*gpr_time_add_type)(gpr_timespec a, gpr_timespec b);
extern gpr_time_add_type gpr_time_add_import;
#define gpr_time_add gpr_time_add_import
typedef gpr_timespec (*gpr_time_sub_type)(gpr_timespec a, gpr_timespec b);
extern gpr_time_sub_type gpr_time_sub_import;
#define gpr_time_sub gpr_time_sub_import
typedef gpr_timespec (*gpr_time_from_micros_type)(int64_t us,
                                                  gpr_clock_type clock_type);
extern gpr_time_from_micros_type gpr_time_from_micros_import;
#define gpr_time_from_micros gpr_time_from_micros_import
typedef gpr_timespec (*gpr_time_from_nanos_type)(int64_t ns,
                                                 gpr_clock_type clock_type);
extern gpr_time_from_nanos_type gpr_time_from_nanos_import;
#define gpr_time_from_nanos gpr_time_from_nanos_import
typedef gpr_timespec (*gpr_time_from_millis_type)(int64_t ms,
                                                  gpr_clock_type clock_type);
extern gpr_time_from_millis_type gpr_time_from_millis_import;
#define gpr_time_from_millis gpr_time_from_millis_import
typedef gpr_timespec (*gpr_time_from_seconds_type)(int64_t s,
                                                   gpr_clock_type clock_type);
extern gpr_time_from_seconds_type gpr_time_from_seconds_import;
#define gpr_time_from_seconds gpr_time_from_seconds_import
typedef gpr_timespec (*gpr_time_from_minutes_type)(int64_t m,
                                                   gpr_clock_type clock_type);
extern gpr_time_from_minutes_type gpr_time_from_minutes_import;
#define gpr_time_from_minutes gpr_time_from_minutes_import
typedef gpr_timespec (*gpr_time_from_hours_type)(int64_t h,
                                                 gpr_clock_type clock_type);
extern gpr_time_from_hours_type gpr_time_from_hours_import;
#define gpr_time_from_hours gpr_time_from_hours_import
typedef int32_t (*gpr_time_to_millis_type)(gpr_timespec timespec);
extern gpr_time_to_millis_type gpr_time_to_millis_import;
#define gpr_time_to_millis gpr_time_to_millis_import
typedef int (*gpr_time_similar_type)(gpr_timespec a, gpr_timespec b,
                                     gpr_timespec threshold);
extern gpr_time_similar_type gpr_time_similar_import;
#define gpr_time_similar gpr_time_similar_import
typedef void (*gpr_sleep_until_type)(gpr_timespec until);
extern gpr_sleep_until_type gpr_sleep_until_import;
#define gpr_sleep_until gpr_sleep_until_import
typedef double (*gpr_timespec_to_micros_type)(gpr_timespec t);
extern gpr_timespec_to_micros_type gpr_timespec_to_micros_import;
#define gpr_timespec_to_micros gpr_timespec_to_micros_import

void grpc_rb_load_imports(HMODULE library);

#endif /* GPR_WINDOWS */

#endif
