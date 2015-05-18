# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# NMake file to build secondary gRPC targets on Windows.
# Use grpc.sln to solution to build the gRPC libraries.

OUT_DIR=test_bin

CC=cl.exe /nologo
LINK=link.exe /nologo
LIBTOOL=lib.exe /nologo /nodefaultlib

REPO_ROOT=..
OPENSSL_INCLUDES = .\packages\grpc.dependencies.openssl.1.0.2.2\build\native\include
ZLIB_INCLUDES = .\packages\grpc.dependencies.zlib.1.2.8.9\build\native\include
INCLUDES=/I$(REPO_ROOT) /I$(REPO_ROOT)\include /I$(OPENSSL_INCLUDES) /I$(ZLIB_INCLUDES)
DEFINES=/D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /D _CRT_SECURE_NO_WARNINGS
CFLAGS=/c $(INCLUDES) /Z7 /W3 /WX- /sdl $(DEFINES) /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze-
LFLAGS=/DEBUG /INCREMENTAL /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86

OPENSSL_LIBS=.\packages\grpc.dependencies.openssl.1.0.2.2\build\native\lib\v120\Win32\Debug\static\ssleay32.lib .\packages\grpc.dependencies.openssl.1.0.2.2\build\native\lib\v120\Win32\Debug\static\libeay32.lib
WINSOCK_LIBS=ws2_32.lib
GENERAL_LIBS=advapi32.lib comdlg32.lib gdi32.lib kernel32.lib odbc32.lib odbccp32.lib ole32.lib oleaut32.lib shell32.lib user32.lib uuid.lib winspool.lib
ZLIB_LIBS=.\packages\grpc.dependencies.zlib.1.2.8.9\build\native\lib\v120\Win32\Debug\static\cdecl\zlib.lib
LIBS=$(OPENSSL_LIBS) $(ZLIB_LIBS) $(GENERAL_LIBS) $(WINSOCK_LIBS)

all: buildtests

$(OUT_DIR):
	mkdir $(OUT_DIR)

build_libs: build_gpr build_gpr_test_util build_grpc build_grpc_test_util build_grpc_test_util_unsecure build_grpc_unsecure Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_bad_hostname.lib Debug\end2end_test_cancel_after_accept.lib Debug\end2end_test_cancel_after_accept_and_writes_closed.lib Debug\end2end_test_cancel_after_invoke.lib Debug\end2end_test_cancel_before_invoke.lib Debug\end2end_test_cancel_in_a_vacuum.lib Debug\end2end_test_census_simple_request.lib Debug\end2end_test_disappearing_server.lib Debug\end2end_test_early_server_shutdown_finishes_inflight_calls.lib Debug\end2end_test_early_server_shutdown_finishes_tags.lib Debug\end2end_test_empty_batch.lib Debug\end2end_test_graceful_server_shutdown.lib Debug\end2end_test_invoke_large_request.lib Debug\end2end_test_max_concurrent_streams.lib Debug\end2end_test_max_message_length.lib Debug\end2end_test_no_op.lib Debug\end2end_test_ping_pong_streaming.lib Debug\end2end_test_registered_call.lib Debug\end2end_test_request_response_with_binary_metadata_and_payload.lib Debug\end2end_test_request_response_with_metadata_and_payload.lib Debug\end2end_test_request_response_with_payload.lib Debug\end2end_test_request_response_with_payload_and_call_creds.lib Debug\end2end_test_request_response_with_trailing_metadata_and_payload.lib Debug\end2end_test_request_with_large_metadata.lib Debug\end2end_test_request_with_payload.lib Debug\end2end_test_simple_delayed_request.lib Debug\end2end_test_simple_request.lib Debug\end2end_test_simple_request_with_high_initial_sequence_number.lib Debug\end2end_certs.lib 
buildtests: buildtests_c buildtests_cxx

buildtests_c: alarm_heap_test.exe alarm_list_test.exe alarm_test.exe alpn_test.exe bin_encoder_test.exe census_hash_table_test.exe census_statistics_multiple_writers_circular_buffer_test.exe census_statistics_multiple_writers_test.exe census_statistics_performance_test.exe census_statistics_quick_test.exe census_statistics_small_log_test.exe census_stub_test.exe census_window_stats_test.exe chttp2_status_conversion_test.exe chttp2_stream_encoder_test.exe chttp2_stream_map_test.exe fling_client.exe fling_server.exe gpr_cancellable_test.exe gpr_cmdline_test.exe gpr_env_test.exe gpr_file_test.exe gpr_histogram_test.exe gpr_host_port_test.exe gpr_log_test.exe gpr_slice_buffer_test.exe gpr_slice_test.exe gpr_string_test.exe gpr_sync_test.exe gpr_thd_test.exe gpr_time_test.exe gpr_tls_test.exe gpr_useful_test.exe grpc_base64_test.exe grpc_byte_buffer_reader_test.exe grpc_channel_stack_test.exe grpc_completion_queue_test.exe grpc_credentials_test.exe grpc_json_token_test.exe grpc_stream_op_test.exe hpack_parser_test.exe hpack_table_test.exe httpcli_format_request_test.exe httpcli_parser_test.exe httpcli_test.exe json_rewrite.exe json_rewrite_test.exe json_test.exe lame_client_test.exe message_compress_test.exe multi_init_test.exe murmur_hash_test.exe no_server_test.exe resolve_address_test.exe secure_endpoint_test.exe sockaddr_utils_test.exe time_averaged_stats_test.exe time_test.exe timeout_encoding_test.exe timers_test.exe transport_metadata_test.exe transport_security_test.exe chttp2_fake_security_bad_hostname_test.exe chttp2_fake_security_cancel_after_accept_test.exe chttp2_fake_security_cancel_after_accept_and_writes_closed_test.exe chttp2_fake_security_cancel_after_invoke_test.exe chttp2_fake_security_cancel_before_invoke_test.exe chttp2_fake_security_cancel_in_a_vacuum_test.exe chttp2_fake_security_census_simple_request_test.exe chttp2_fake_security_disappearing_server_test.exe chttp2_fake_security_early_server_shutdown_finishes_inflight_calls_test.exe chttp2_fake_security_early_server_shutdown_finishes_tags_test.exe chttp2_fake_security_empty_batch_test.exe chttp2_fake_security_graceful_server_shutdown_test.exe chttp2_fake_security_invoke_large_request_test.exe chttp2_fake_security_max_concurrent_streams_test.exe chttp2_fake_security_max_message_length_test.exe chttp2_fake_security_no_op_test.exe chttp2_fake_security_ping_pong_streaming_test.exe chttp2_fake_security_registered_call_test.exe chttp2_fake_security_request_response_with_binary_metadata_and_payload_test.exe chttp2_fake_security_request_response_with_metadata_and_payload_test.exe chttp2_fake_security_request_response_with_payload_test.exe chttp2_fake_security_request_response_with_payload_and_call_creds_test.exe chttp2_fake_security_request_response_with_trailing_metadata_and_payload_test.exe chttp2_fake_security_request_with_large_metadata_test.exe chttp2_fake_security_request_with_payload_test.exe chttp2_fake_security_simple_delayed_request_test.exe chttp2_fake_security_simple_request_test.exe chttp2_fake_security_simple_request_with_high_initial_sequence_number_test.exe chttp2_fullstack_bad_hostname_test.exe chttp2_fullstack_cancel_after_accept_test.exe chttp2_fullstack_cancel_after_accept_and_writes_closed_test.exe chttp2_fullstack_cancel_after_invoke_test.exe chttp2_fullstack_cancel_before_invoke_test.exe chttp2_fullstack_cancel_in_a_vacuum_test.exe chttp2_fullstack_census_simple_request_test.exe chttp2_fullstack_disappearing_server_test.exe chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_test.exe chttp2_fullstack_early_server_shutdown_finishes_tags_test.exe chttp2_fullstack_empty_batch_test.exe chttp2_fullstack_graceful_server_shutdown_test.exe chttp2_fullstack_invoke_large_request_test.exe chttp2_fullstack_max_concurrent_streams_test.exe chttp2_fullstack_max_message_length_test.exe chttp2_fullstack_no_op_test.exe chttp2_fullstack_ping_pong_streaming_test.exe chttp2_fullstack_registered_call_test.exe chttp2_fullstack_request_response_with_binary_metadata_and_payload_test.exe chttp2_fullstack_request_response_with_metadata_and_payload_test.exe chttp2_fullstack_request_response_with_payload_test.exe chttp2_fullstack_request_response_with_payload_and_call_creds_test.exe chttp2_fullstack_request_response_with_trailing_metadata_and_payload_test.exe chttp2_fullstack_request_with_large_metadata_test.exe chttp2_fullstack_request_with_payload_test.exe chttp2_fullstack_simple_delayed_request_test.exe chttp2_fullstack_simple_request_test.exe chttp2_fullstack_simple_request_with_high_initial_sequence_number_test.exe chttp2_simple_ssl_fullstack_bad_hostname_test.exe chttp2_simple_ssl_fullstack_cancel_after_accept_test.exe chttp2_simple_ssl_fullstack_cancel_after_accept_and_writes_closed_test.exe chttp2_simple_ssl_fullstack_cancel_after_invoke_test.exe chttp2_simple_ssl_fullstack_cancel_before_invoke_test.exe chttp2_simple_ssl_fullstack_cancel_in_a_vacuum_test.exe chttp2_simple_ssl_fullstack_census_simple_request_test.exe chttp2_simple_ssl_fullstack_disappearing_server_test.exe chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_inflight_calls_test.exe chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_tags_test.exe chttp2_simple_ssl_fullstack_empty_batch_test.exe chttp2_simple_ssl_fullstack_graceful_server_shutdown_test.exe chttp2_simple_ssl_fullstack_invoke_large_request_test.exe chttp2_simple_ssl_fullstack_max_concurrent_streams_test.exe chttp2_simple_ssl_fullstack_max_message_length_test.exe chttp2_simple_ssl_fullstack_no_op_test.exe chttp2_simple_ssl_fullstack_ping_pong_streaming_test.exe chttp2_simple_ssl_fullstack_registered_call_test.exe chttp2_simple_ssl_fullstack_request_response_with_binary_metadata_and_payload_test.exe chttp2_simple_ssl_fullstack_request_response_with_metadata_and_payload_test.exe chttp2_simple_ssl_fullstack_request_response_with_payload_test.exe chttp2_simple_ssl_fullstack_request_response_with_payload_and_call_creds_test.exe chttp2_simple_ssl_fullstack_request_response_with_trailing_metadata_and_payload_test.exe chttp2_simple_ssl_fullstack_request_with_large_metadata_test.exe chttp2_simple_ssl_fullstack_request_with_payload_test.exe chttp2_simple_ssl_fullstack_simple_delayed_request_test.exe chttp2_simple_ssl_fullstack_simple_request_test.exe chttp2_simple_ssl_fullstack_simple_request_with_high_initial_sequence_number_test.exe chttp2_simple_ssl_with_oauth2_fullstack_bad_hostname_test.exe chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_test.exe chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_and_writes_closed_test.exe chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_invoke_test.exe chttp2_simple_ssl_with_oauth2_fullstack_cancel_before_invoke_test.exe chttp2_simple_ssl_with_oauth2_fullstack_cancel_in_a_vacuum_test.exe chttp2_simple_ssl_with_oauth2_fullstack_census_simple_request_test.exe chttp2_simple_ssl_with_oauth2_fullstack_disappearing_server_test.exe chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_inflight_calls_test.exe chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_tags_test.exe chttp2_simple_ssl_with_oauth2_fullstack_empty_batch_test.exe chttp2_simple_ssl_with_oauth2_fullstack_graceful_server_shutdown_test.exe chttp2_simple_ssl_with_oauth2_fullstack_invoke_large_request_test.exe chttp2_simple_ssl_with_oauth2_fullstack_max_concurrent_streams_test.exe chttp2_simple_ssl_with_oauth2_fullstack_max_message_length_test.exe chttp2_simple_ssl_with_oauth2_fullstack_no_op_test.exe chttp2_simple_ssl_with_oauth2_fullstack_ping_pong_streaming_test.exe chttp2_simple_ssl_with_oauth2_fullstack_registered_call_test.exe chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_binary_metadata_and_payload_test.exe chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_metadata_and_payload_test.exe chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_test.exe chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_and_call_creds_test.exe chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_trailing_metadata_and_payload_test.exe chttp2_simple_ssl_with_oauth2_fullstack_request_with_large_metadata_test.exe chttp2_simple_ssl_with_oauth2_fullstack_request_with_payload_test.exe chttp2_simple_ssl_with_oauth2_fullstack_simple_delayed_request_test.exe chttp2_simple_ssl_with_oauth2_fullstack_simple_request_test.exe chttp2_simple_ssl_with_oauth2_fullstack_simple_request_with_high_initial_sequence_number_test.exe chttp2_socket_pair_bad_hostname_test.exe chttp2_socket_pair_cancel_after_accept_test.exe chttp2_socket_pair_cancel_after_accept_and_writes_closed_test.exe chttp2_socket_pair_cancel_after_invoke_test.exe chttp2_socket_pair_cancel_before_invoke_test.exe chttp2_socket_pair_cancel_in_a_vacuum_test.exe chttp2_socket_pair_census_simple_request_test.exe chttp2_socket_pair_disappearing_server_test.exe chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_test.exe chttp2_socket_pair_early_server_shutdown_finishes_tags_test.exe chttp2_socket_pair_empty_batch_test.exe chttp2_socket_pair_graceful_server_shutdown_test.exe chttp2_socket_pair_invoke_large_request_test.exe chttp2_socket_pair_max_concurrent_streams_test.exe chttp2_socket_pair_max_message_length_test.exe chttp2_socket_pair_no_op_test.exe chttp2_socket_pair_ping_pong_streaming_test.exe chttp2_socket_pair_registered_call_test.exe chttp2_socket_pair_request_response_with_binary_metadata_and_payload_test.exe chttp2_socket_pair_request_response_with_metadata_and_payload_test.exe chttp2_socket_pair_request_response_with_payload_test.exe chttp2_socket_pair_request_response_with_payload_and_call_creds_test.exe chttp2_socket_pair_request_response_with_trailing_metadata_and_payload_test.exe chttp2_socket_pair_request_with_large_metadata_test.exe chttp2_socket_pair_request_with_payload_test.exe chttp2_socket_pair_simple_delayed_request_test.exe chttp2_socket_pair_simple_request_test.exe chttp2_socket_pair_simple_request_with_high_initial_sequence_number_test.exe chttp2_socket_pair_one_byte_at_a_time_bad_hostname_test.exe chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_test.exe chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_test.exe chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_test.exe chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_test.exe chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_test.exe chttp2_socket_pair_one_byte_at_a_time_census_simple_request_test.exe chttp2_socket_pair_one_byte_at_a_time_disappearing_server_test.exe chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_test.exe chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_test.exe chttp2_socket_pair_one_byte_at_a_time_empty_batch_test.exe chttp2_socket_pair_one_byte_at_a_time_graceful_server_shutdown_test.exe chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_test.exe chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_test.exe chttp2_socket_pair_one_byte_at_a_time_max_message_length_test.exe chttp2_socket_pair_one_byte_at_a_time_no_op_test.exe chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_test.exe chttp2_socket_pair_one_byte_at_a_time_registered_call_test.exe chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_test.exe chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_test.exe chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_test.exe chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_and_call_creds_test.exe chttp2_socket_pair_one_byte_at_a_time_request_response_with_trailing_metadata_and_payload_test.exe chttp2_socket_pair_one_byte_at_a_time_request_with_large_metadata_test.exe chttp2_socket_pair_one_byte_at_a_time_request_with_payload_test.exe chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_test.exe chttp2_socket_pair_one_byte_at_a_time_simple_request_test.exe chttp2_socket_pair_one_byte_at_a_time_simple_request_with_high_initial_sequence_number_test.exe chttp2_fullstack_bad_hostname_unsecure_test.exe chttp2_fullstack_cancel_after_accept_unsecure_test.exe chttp2_fullstack_cancel_after_accept_and_writes_closed_unsecure_test.exe chttp2_fullstack_cancel_after_invoke_unsecure_test.exe chttp2_fullstack_cancel_before_invoke_unsecure_test.exe chttp2_fullstack_cancel_in_a_vacuum_unsecure_test.exe chttp2_fullstack_census_simple_request_unsecure_test.exe chttp2_fullstack_disappearing_server_unsecure_test.exe chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_unsecure_test.exe chttp2_fullstack_early_server_shutdown_finishes_tags_unsecure_test.exe chttp2_fullstack_empty_batch_unsecure_test.exe chttp2_fullstack_graceful_server_shutdown_unsecure_test.exe chttp2_fullstack_invoke_large_request_unsecure_test.exe chttp2_fullstack_max_concurrent_streams_unsecure_test.exe chttp2_fullstack_max_message_length_unsecure_test.exe chttp2_fullstack_no_op_unsecure_test.exe chttp2_fullstack_ping_pong_streaming_unsecure_test.exe chttp2_fullstack_registered_call_unsecure_test.exe chttp2_fullstack_request_response_with_binary_metadata_and_payload_unsecure_test.exe chttp2_fullstack_request_response_with_metadata_and_payload_unsecure_test.exe chttp2_fullstack_request_response_with_payload_unsecure_test.exe chttp2_fullstack_request_response_with_trailing_metadata_and_payload_unsecure_test.exe chttp2_fullstack_request_with_large_metadata_unsecure_test.exe chttp2_fullstack_request_with_payload_unsecure_test.exe chttp2_fullstack_simple_delayed_request_unsecure_test.exe chttp2_fullstack_simple_request_unsecure_test.exe chttp2_fullstack_simple_request_with_high_initial_sequence_number_unsecure_test.exe chttp2_socket_pair_bad_hostname_unsecure_test.exe chttp2_socket_pair_cancel_after_accept_unsecure_test.exe chttp2_socket_pair_cancel_after_accept_and_writes_closed_unsecure_test.exe chttp2_socket_pair_cancel_after_invoke_unsecure_test.exe chttp2_socket_pair_cancel_before_invoke_unsecure_test.exe chttp2_socket_pair_cancel_in_a_vacuum_unsecure_test.exe chttp2_socket_pair_census_simple_request_unsecure_test.exe chttp2_socket_pair_disappearing_server_unsecure_test.exe chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_unsecure_test.exe chttp2_socket_pair_early_server_shutdown_finishes_tags_unsecure_test.exe chttp2_socket_pair_empty_batch_unsecure_test.exe chttp2_socket_pair_graceful_server_shutdown_unsecure_test.exe chttp2_socket_pair_invoke_large_request_unsecure_test.exe chttp2_socket_pair_max_concurrent_streams_unsecure_test.exe chttp2_socket_pair_max_message_length_unsecure_test.exe chttp2_socket_pair_no_op_unsecure_test.exe chttp2_socket_pair_ping_pong_streaming_unsecure_test.exe chttp2_socket_pair_registered_call_unsecure_test.exe chttp2_socket_pair_request_response_with_binary_metadata_and_payload_unsecure_test.exe chttp2_socket_pair_request_response_with_metadata_and_payload_unsecure_test.exe chttp2_socket_pair_request_response_with_payload_unsecure_test.exe chttp2_socket_pair_request_response_with_trailing_metadata_and_payload_unsecure_test.exe chttp2_socket_pair_request_with_large_metadata_unsecure_test.exe chttp2_socket_pair_request_with_payload_unsecure_test.exe chttp2_socket_pair_simple_delayed_request_unsecure_test.exe chttp2_socket_pair_simple_request_unsecure_test.exe chttp2_socket_pair_simple_request_with_high_initial_sequence_number_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_bad_hostname_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_census_simple_request_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_disappearing_server_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_empty_batch_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_graceful_server_shutdown_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_max_message_length_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_no_op_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_registered_call_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_request_response_with_trailing_metadata_and_payload_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_request_with_large_metadata_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_request_with_payload_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_simple_request_unsecure_test.exe chttp2_socket_pair_one_byte_at_a_time_simple_request_with_high_initial_sequence_number_unsecure_test.exe 
	echo All tests built.

buildtests_cxx: interop_client.exe interop_server.exe 
	echo All tests built.

alarm_heap_test.exe: build_libs $(OUT_DIR)
	echo Building alarm_heap_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\iomgr\alarm_heap_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\alarm_heap_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\alarm_heap_test.obj 
alarm_heap_test: alarm_heap_test.exe
	echo Running alarm_heap_test
	$(OUT_DIR)\alarm_heap_test.exe
alarm_list_test.exe: build_libs $(OUT_DIR)
	echo Building alarm_list_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\iomgr\alarm_list_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\alarm_list_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\alarm_list_test.obj 
alarm_list_test: alarm_list_test.exe
	echo Running alarm_list_test
	$(OUT_DIR)\alarm_list_test.exe
alarm_test.exe: build_libs $(OUT_DIR)
	echo Building alarm_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\iomgr\alarm_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\alarm_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\alarm_test.obj 
alarm_test: alarm_test.exe
	echo Running alarm_test
	$(OUT_DIR)\alarm_test.exe
alpn_test.exe: build_libs $(OUT_DIR)
	echo Building alpn_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\transport\chttp2\alpn_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\alpn_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\alpn_test.obj 
alpn_test: alpn_test.exe
	echo Running alpn_test
	$(OUT_DIR)\alpn_test.exe
bin_encoder_test.exe: build_libs $(OUT_DIR)
	echo Building bin_encoder_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\transport\chttp2\bin_encoder_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\bin_encoder_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\bin_encoder_test.obj 
bin_encoder_test: bin_encoder_test.exe
	echo Running bin_encoder_test
	$(OUT_DIR)\bin_encoder_test.exe
census_hash_table_test.exe: build_libs $(OUT_DIR)
	echo Building census_hash_table_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\statistics\hash_table_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\census_hash_table_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\hash_table_test.obj 
census_hash_table_test: census_hash_table_test.exe
	echo Running census_hash_table_test
	$(OUT_DIR)\census_hash_table_test.exe
census_statistics_multiple_writers_circular_buffer_test.exe: build_libs $(OUT_DIR)
	echo Building census_statistics_multiple_writers_circular_buffer_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\statistics\multiple_writers_circular_buffer_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\census_statistics_multiple_writers_circular_buffer_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\multiple_writers_circular_buffer_test.obj 
census_statistics_multiple_writers_circular_buffer_test: census_statistics_multiple_writers_circular_buffer_test.exe
	echo Running census_statistics_multiple_writers_circular_buffer_test
	$(OUT_DIR)\census_statistics_multiple_writers_circular_buffer_test.exe
census_statistics_multiple_writers_test.exe: build_libs $(OUT_DIR)
	echo Building census_statistics_multiple_writers_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\statistics\multiple_writers_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\census_statistics_multiple_writers_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\multiple_writers_test.obj 
census_statistics_multiple_writers_test: census_statistics_multiple_writers_test.exe
	echo Running census_statistics_multiple_writers_test
	$(OUT_DIR)\census_statistics_multiple_writers_test.exe
census_statistics_performance_test.exe: build_libs $(OUT_DIR)
	echo Building census_statistics_performance_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\statistics\performance_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\census_statistics_performance_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\performance_test.obj 
census_statistics_performance_test: census_statistics_performance_test.exe
	echo Running census_statistics_performance_test
	$(OUT_DIR)\census_statistics_performance_test.exe
census_statistics_quick_test.exe: build_libs $(OUT_DIR)
	echo Building census_statistics_quick_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\statistics\quick_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\census_statistics_quick_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\quick_test.obj 
census_statistics_quick_test: census_statistics_quick_test.exe
	echo Running census_statistics_quick_test
	$(OUT_DIR)\census_statistics_quick_test.exe
census_statistics_small_log_test.exe: build_libs $(OUT_DIR)
	echo Building census_statistics_small_log_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\statistics\small_log_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\census_statistics_small_log_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\small_log_test.obj 
census_statistics_small_log_test: census_statistics_small_log_test.exe
	echo Running census_statistics_small_log_test
	$(OUT_DIR)\census_statistics_small_log_test.exe
census_stub_test.exe: build_libs $(OUT_DIR)
	echo Building census_stub_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\statistics\census_stub_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\census_stub_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\census_stub_test.obj 
census_stub_test: census_stub_test.exe
	echo Running census_stub_test
	$(OUT_DIR)\census_stub_test.exe
census_window_stats_test.exe: build_libs $(OUT_DIR)
	echo Building census_window_stats_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\statistics\window_stats_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\census_window_stats_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\window_stats_test.obj 
census_window_stats_test: census_window_stats_test.exe
	echo Running census_window_stats_test
	$(OUT_DIR)\census_window_stats_test.exe
chttp2_status_conversion_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_status_conversion_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\transport\chttp2\status_conversion_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_status_conversion_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\status_conversion_test.obj 
chttp2_status_conversion_test: chttp2_status_conversion_test.exe
	echo Running chttp2_status_conversion_test
	$(OUT_DIR)\chttp2_status_conversion_test.exe
chttp2_stream_encoder_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_stream_encoder_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\transport\chttp2\stream_encoder_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_stream_encoder_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\stream_encoder_test.obj 
chttp2_stream_encoder_test: chttp2_stream_encoder_test.exe
	echo Running chttp2_stream_encoder_test
	$(OUT_DIR)\chttp2_stream_encoder_test.exe
chttp2_stream_map_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_stream_map_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\transport\chttp2\stream_map_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_stream_map_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\stream_map_test.obj 
chttp2_stream_map_test: chttp2_stream_map_test.exe
	echo Running chttp2_stream_map_test
	$(OUT_DIR)\chttp2_stream_map_test.exe
fling_client.exe: build_libs $(OUT_DIR)
	echo Building fling_client
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\fling\client.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\fling_client.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\client.obj 
fling_client: fling_client.exe
	echo Running fling_client
	$(OUT_DIR)\fling_client.exe
fling_server.exe: build_libs $(OUT_DIR)
	echo Building fling_server
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\fling\server.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\fling_server.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\server.obj 
fling_server: fling_server.exe
	echo Running fling_server
	$(OUT_DIR)\fling_server.exe
gen_hpack_tables.exe: build_libs $(OUT_DIR)
	echo Building gen_hpack_tables
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\src\core\transport\chttp2\gen_hpack_tables.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gen_hpack_tables.exe" Debug\grpc_test_util.lib Debug\gpr.lib Debug\grpc.lib $(LIBS) $(OUT_DIR)\gen_hpack_tables.obj 
gen_hpack_tables: gen_hpack_tables.exe
	echo Running gen_hpack_tables
	$(OUT_DIR)\gen_hpack_tables.exe
gpr_cancellable_test.exe: build_libs $(OUT_DIR)
	echo Building gpr_cancellable_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\support\cancellable_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_cancellable_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\cancellable_test.obj 
gpr_cancellable_test: gpr_cancellable_test.exe
	echo Running gpr_cancellable_test
	$(OUT_DIR)\gpr_cancellable_test.exe
gpr_cmdline_test.exe: build_libs $(OUT_DIR)
	echo Building gpr_cmdline_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\support\cmdline_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_cmdline_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\cmdline_test.obj 
gpr_cmdline_test: gpr_cmdline_test.exe
	echo Running gpr_cmdline_test
	$(OUT_DIR)\gpr_cmdline_test.exe
gpr_env_test.exe: build_libs $(OUT_DIR)
	echo Building gpr_env_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\support\env_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_env_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\env_test.obj 
gpr_env_test: gpr_env_test.exe
	echo Running gpr_env_test
	$(OUT_DIR)\gpr_env_test.exe
gpr_file_test.exe: build_libs $(OUT_DIR)
	echo Building gpr_file_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\support\file_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_file_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\file_test.obj 
gpr_file_test: gpr_file_test.exe
	echo Running gpr_file_test
	$(OUT_DIR)\gpr_file_test.exe
gpr_histogram_test.exe: build_libs $(OUT_DIR)
	echo Building gpr_histogram_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\support\histogram_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_histogram_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\histogram_test.obj 
gpr_histogram_test: gpr_histogram_test.exe
	echo Running gpr_histogram_test
	$(OUT_DIR)\gpr_histogram_test.exe
gpr_host_port_test.exe: build_libs $(OUT_DIR)
	echo Building gpr_host_port_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\support\host_port_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_host_port_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\host_port_test.obj 
gpr_host_port_test: gpr_host_port_test.exe
	echo Running gpr_host_port_test
	$(OUT_DIR)\gpr_host_port_test.exe
gpr_log_test.exe: build_libs $(OUT_DIR)
	echo Building gpr_log_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\support\log_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_log_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\log_test.obj 
gpr_log_test: gpr_log_test.exe
	echo Running gpr_log_test
	$(OUT_DIR)\gpr_log_test.exe
gpr_slice_buffer_test.exe: build_libs $(OUT_DIR)
	echo Building gpr_slice_buffer_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\support\slice_buffer_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_slice_buffer_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\slice_buffer_test.obj 
gpr_slice_buffer_test: gpr_slice_buffer_test.exe
	echo Running gpr_slice_buffer_test
	$(OUT_DIR)\gpr_slice_buffer_test.exe
gpr_slice_test.exe: build_libs $(OUT_DIR)
	echo Building gpr_slice_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\support\slice_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_slice_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\slice_test.obj 
gpr_slice_test: gpr_slice_test.exe
	echo Running gpr_slice_test
	$(OUT_DIR)\gpr_slice_test.exe
gpr_string_test.exe: build_libs $(OUT_DIR)
	echo Building gpr_string_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\support\string_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_string_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\string_test.obj 
gpr_string_test: gpr_string_test.exe
	echo Running gpr_string_test
	$(OUT_DIR)\gpr_string_test.exe
gpr_sync_test.exe: build_libs $(OUT_DIR)
	echo Building gpr_sync_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\support\sync_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_sync_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\sync_test.obj 
gpr_sync_test: gpr_sync_test.exe
	echo Running gpr_sync_test
	$(OUT_DIR)\gpr_sync_test.exe
gpr_thd_test.exe: build_libs $(OUT_DIR)
	echo Building gpr_thd_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\support\thd_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_thd_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\thd_test.obj 
gpr_thd_test: gpr_thd_test.exe
	echo Running gpr_thd_test
	$(OUT_DIR)\gpr_thd_test.exe
gpr_time_test.exe: build_libs $(OUT_DIR)
	echo Building gpr_time_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\support\time_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_time_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\time_test.obj 
gpr_time_test: gpr_time_test.exe
	echo Running gpr_time_test
	$(OUT_DIR)\gpr_time_test.exe
gpr_tls_test.exe: build_libs $(OUT_DIR)
	echo Building gpr_tls_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\support\tls_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_tls_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\tls_test.obj 
gpr_tls_test: gpr_tls_test.exe
	echo Running gpr_tls_test
	$(OUT_DIR)\gpr_tls_test.exe
gpr_useful_test.exe: build_libs $(OUT_DIR)
	echo Building gpr_useful_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\support\useful_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_useful_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\useful_test.obj 
gpr_useful_test: gpr_useful_test.exe
	echo Running gpr_useful_test
	$(OUT_DIR)\gpr_useful_test.exe
grpc_base64_test.exe: build_libs $(OUT_DIR)
	echo Building grpc_base64_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\security\base64_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\grpc_base64_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\base64_test.obj 
grpc_base64_test: grpc_base64_test.exe
	echo Running grpc_base64_test
	$(OUT_DIR)\grpc_base64_test.exe
grpc_byte_buffer_reader_test.exe: build_libs $(OUT_DIR)
	echo Building grpc_byte_buffer_reader_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\surface\byte_buffer_reader_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\grpc_byte_buffer_reader_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\byte_buffer_reader_test.obj 
grpc_byte_buffer_reader_test: grpc_byte_buffer_reader_test.exe
	echo Running grpc_byte_buffer_reader_test
	$(OUT_DIR)\grpc_byte_buffer_reader_test.exe
grpc_channel_stack_test.exe: build_libs $(OUT_DIR)
	echo Building grpc_channel_stack_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\channel\channel_stack_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\grpc_channel_stack_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\channel_stack_test.obj 
grpc_channel_stack_test: grpc_channel_stack_test.exe
	echo Running grpc_channel_stack_test
	$(OUT_DIR)\grpc_channel_stack_test.exe
grpc_completion_queue_test.exe: build_libs $(OUT_DIR)
	echo Building grpc_completion_queue_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\surface\completion_queue_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\grpc_completion_queue_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\completion_queue_test.obj 
grpc_completion_queue_test: grpc_completion_queue_test.exe
	echo Running grpc_completion_queue_test
	$(OUT_DIR)\grpc_completion_queue_test.exe
grpc_create_jwt.exe: build_libs $(OUT_DIR)
	echo Building grpc_create_jwt
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\security\create_jwt.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\grpc_create_jwt.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\create_jwt.obj 
grpc_create_jwt: grpc_create_jwt.exe
	echo Running grpc_create_jwt
	$(OUT_DIR)\grpc_create_jwt.exe
grpc_credentials_test.exe: build_libs $(OUT_DIR)
	echo Building grpc_credentials_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\security\credentials_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\grpc_credentials_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\credentials_test.obj 
grpc_credentials_test: grpc_credentials_test.exe
	echo Running grpc_credentials_test
	$(OUT_DIR)\grpc_credentials_test.exe
grpc_fetch_oauth2.exe: build_libs $(OUT_DIR)
	echo Building grpc_fetch_oauth2
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\security\fetch_oauth2.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\grpc_fetch_oauth2.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\fetch_oauth2.obj 
grpc_fetch_oauth2: grpc_fetch_oauth2.exe
	echo Running grpc_fetch_oauth2
	$(OUT_DIR)\grpc_fetch_oauth2.exe
grpc_json_token_test.exe: build_libs $(OUT_DIR)
	echo Building grpc_json_token_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\security\json_token_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\grpc_json_token_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\json_token_test.obj 
grpc_json_token_test: grpc_json_token_test.exe
	echo Running grpc_json_token_test
	$(OUT_DIR)\grpc_json_token_test.exe
grpc_print_google_default_creds_token.exe: build_libs $(OUT_DIR)
	echo Building grpc_print_google_default_creds_token
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\security\print_google_default_creds_token.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\grpc_print_google_default_creds_token.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\print_google_default_creds_token.obj 
grpc_print_google_default_creds_token: grpc_print_google_default_creds_token.exe
	echo Running grpc_print_google_default_creds_token
	$(OUT_DIR)\grpc_print_google_default_creds_token.exe
grpc_stream_op_test.exe: build_libs $(OUT_DIR)
	echo Building grpc_stream_op_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\transport\stream_op_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\grpc_stream_op_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\stream_op_test.obj 
grpc_stream_op_test: grpc_stream_op_test.exe
	echo Running grpc_stream_op_test
	$(OUT_DIR)\grpc_stream_op_test.exe
hpack_parser_test.exe: build_libs $(OUT_DIR)
	echo Building hpack_parser_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\transport\chttp2\hpack_parser_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\hpack_parser_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\hpack_parser_test.obj 
hpack_parser_test: hpack_parser_test.exe
	echo Running hpack_parser_test
	$(OUT_DIR)\hpack_parser_test.exe
hpack_table_test.exe: build_libs $(OUT_DIR)
	echo Building hpack_table_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\transport\chttp2\hpack_table_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\hpack_table_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\hpack_table_test.obj 
hpack_table_test: hpack_table_test.exe
	echo Running hpack_table_test
	$(OUT_DIR)\hpack_table_test.exe
httpcli_format_request_test.exe: build_libs $(OUT_DIR)
	echo Building httpcli_format_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\httpcli\format_request_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\httpcli_format_request_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\format_request_test.obj 
httpcli_format_request_test: httpcli_format_request_test.exe
	echo Running httpcli_format_request_test
	$(OUT_DIR)\httpcli_format_request_test.exe
httpcli_parser_test.exe: build_libs $(OUT_DIR)
	echo Building httpcli_parser_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\httpcli\parser_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\httpcli_parser_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\parser_test.obj 
httpcli_parser_test: httpcli_parser_test.exe
	echo Running httpcli_parser_test
	$(OUT_DIR)\httpcli_parser_test.exe
httpcli_test.exe: build_libs $(OUT_DIR)
	echo Building httpcli_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\httpcli\httpcli_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\httpcli_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\httpcli_test.obj 
httpcli_test: httpcli_test.exe
	echo Running httpcli_test
	$(OUT_DIR)\httpcli_test.exe
json_rewrite.exe: build_libs $(OUT_DIR)
	echo Building json_rewrite
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\json\json_rewrite.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\json_rewrite.exe" Debug\grpc.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\json_rewrite.obj 
json_rewrite: json_rewrite.exe
	echo Running json_rewrite
	$(OUT_DIR)\json_rewrite.exe
json_rewrite_test.exe: build_libs $(OUT_DIR)
	echo Building json_rewrite_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\json\json_rewrite_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\json_rewrite_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\json_rewrite_test.obj 
json_rewrite_test: json_rewrite_test.exe
	echo Running json_rewrite_test
	$(OUT_DIR)\json_rewrite_test.exe
json_test.exe: build_libs $(OUT_DIR)
	echo Building json_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\json\json_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\json_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\json_test.obj 
json_test: json_test.exe
	echo Running json_test
	$(OUT_DIR)\json_test.exe
lame_client_test.exe: build_libs $(OUT_DIR)
	echo Building lame_client_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\surface\lame_client_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\lame_client_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\lame_client_test.obj 
lame_client_test: lame_client_test.exe
	echo Running lame_client_test
	$(OUT_DIR)\lame_client_test.exe
low_level_ping_pong_benchmark.exe: build_libs $(OUT_DIR)
	echo Building low_level_ping_pong_benchmark
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\network_benchmarks\low_level_ping_pong.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\low_level_ping_pong_benchmark.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\low_level_ping_pong.obj 
low_level_ping_pong_benchmark: low_level_ping_pong_benchmark.exe
	echo Running low_level_ping_pong_benchmark
	$(OUT_DIR)\low_level_ping_pong_benchmark.exe
message_compress_test.exe: build_libs $(OUT_DIR)
	echo Building message_compress_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\compression\message_compress_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\message_compress_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\message_compress_test.obj 
message_compress_test: message_compress_test.exe
	echo Running message_compress_test
	$(OUT_DIR)\message_compress_test.exe
multi_init_test.exe: build_libs $(OUT_DIR)
	echo Building multi_init_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\surface\multi_init_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\multi_init_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\multi_init_test.obj 
multi_init_test: multi_init_test.exe
	echo Running multi_init_test
	$(OUT_DIR)\multi_init_test.exe
murmur_hash_test.exe: build_libs $(OUT_DIR)
	echo Building murmur_hash_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\support\murmur_hash_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\murmur_hash_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\murmur_hash_test.obj 
murmur_hash_test: murmur_hash_test.exe
	echo Running murmur_hash_test
	$(OUT_DIR)\murmur_hash_test.exe
no_server_test.exe: build_libs $(OUT_DIR)
	echo Building no_server_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\no_server_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\no_server_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\no_server_test.obj 
no_server_test: no_server_test.exe
	echo Running no_server_test
	$(OUT_DIR)\no_server_test.exe
resolve_address_test.exe: build_libs $(OUT_DIR)
	echo Building resolve_address_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\iomgr\resolve_address_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\resolve_address_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\resolve_address_test.obj 
resolve_address_test: resolve_address_test.exe
	echo Running resolve_address_test
	$(OUT_DIR)\resolve_address_test.exe
secure_endpoint_test.exe: build_libs $(OUT_DIR)
	echo Building secure_endpoint_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\security\secure_endpoint_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\secure_endpoint_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\secure_endpoint_test.obj 
secure_endpoint_test: secure_endpoint_test.exe
	echo Running secure_endpoint_test
	$(OUT_DIR)\secure_endpoint_test.exe
sockaddr_utils_test.exe: build_libs $(OUT_DIR)
	echo Building sockaddr_utils_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\iomgr\sockaddr_utils_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\sockaddr_utils_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\sockaddr_utils_test.obj 
sockaddr_utils_test: sockaddr_utils_test.exe
	echo Running sockaddr_utils_test
	$(OUT_DIR)\sockaddr_utils_test.exe
time_averaged_stats_test.exe: build_libs $(OUT_DIR)
	echo Building time_averaged_stats_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\iomgr\time_averaged_stats_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\time_averaged_stats_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\time_averaged_stats_test.obj 
time_averaged_stats_test: time_averaged_stats_test.exe
	echo Running time_averaged_stats_test
	$(OUT_DIR)\time_averaged_stats_test.exe
time_test.exe: build_libs $(OUT_DIR)
	echo Building time_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\support\time_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\time_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\time_test.obj 
time_test: time_test.exe
	echo Running time_test
	$(OUT_DIR)\time_test.exe
timeout_encoding_test.exe: build_libs $(OUT_DIR)
	echo Building timeout_encoding_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\transport\chttp2\timeout_encoding_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\timeout_encoding_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\timeout_encoding_test.obj 
timeout_encoding_test: timeout_encoding_test.exe
	echo Running timeout_encoding_test
	$(OUT_DIR)\timeout_encoding_test.exe
timers_test.exe: build_libs $(OUT_DIR)
	echo Building timers_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\profiling\timers_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\timers_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\timers_test.obj 
timers_test: timers_test.exe
	echo Running timers_test
	$(OUT_DIR)\timers_test.exe
transport_metadata_test.exe: build_libs $(OUT_DIR)
	echo Building transport_metadata_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\transport\metadata_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\transport_metadata_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\metadata_test.obj 
transport_metadata_test: transport_metadata_test.exe
	echo Running transport_metadata_test
	$(OUT_DIR)\transport_metadata_test.exe
transport_security_test.exe: build_libs $(OUT_DIR)
	echo Building transport_security_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\tsi\transport_security_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\transport_security_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\transport_security_test.obj 
transport_security_test: transport_security_test.exe
	echo Running transport_security_test
	$(OUT_DIR)\transport_security_test.exe
interop_client.exe: build_libs $(OUT_DIR)
	echo Building interop_client
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\interop_client.exe" Debug\interop_client_main.lib Debug\interop_client_helper.lib Debug\grpc++_test_util.lib Debug\grpc_test_util.lib Debug\grpc++.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib Debug\grpc++_test_config.lib $(LIBS) $(OUT_DIR)\dummy.obj 
interop_client: interop_client.exe
	echo Running interop_client
	$(OUT_DIR)\interop_client.exe
interop_server.exe: build_libs $(OUT_DIR)
	echo Building interop_server
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\interop_server.exe" Debug\interop_server_main.lib Debug\interop_server_helper.lib Debug\grpc++_test_util.lib Debug\grpc_test_util.lib Debug\grpc++.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib Debug\grpc++_test_config.lib $(LIBS) $(OUT_DIR)\dummy.obj 
interop_server: interop_server.exe
	echo Running interop_server
	$(OUT_DIR)\interop_server.exe
chttp2_fake_security_bad_hostname_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_bad_hostname_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_bad_hostname_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_bad_hostname.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_bad_hostname_test: chttp2_fake_security_bad_hostname_test.exe
	echo Running chttp2_fake_security_bad_hostname_test
	$(OUT_DIR)\chttp2_fake_security_bad_hostname_test.exe
chttp2_fake_security_cancel_after_accept_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_cancel_after_accept_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_cancel_after_accept_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_cancel_after_accept.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_cancel_after_accept_test: chttp2_fake_security_cancel_after_accept_test.exe
	echo Running chttp2_fake_security_cancel_after_accept_test
	$(OUT_DIR)\chttp2_fake_security_cancel_after_accept_test.exe
chttp2_fake_security_cancel_after_accept_and_writes_closed_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_cancel_after_accept_and_writes_closed_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_cancel_after_accept_and_writes_closed_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_cancel_after_accept_and_writes_closed.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_cancel_after_accept_and_writes_closed_test: chttp2_fake_security_cancel_after_accept_and_writes_closed_test.exe
	echo Running chttp2_fake_security_cancel_after_accept_and_writes_closed_test
	$(OUT_DIR)\chttp2_fake_security_cancel_after_accept_and_writes_closed_test.exe
chttp2_fake_security_cancel_after_invoke_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_cancel_after_invoke_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_cancel_after_invoke_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_cancel_after_invoke.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_cancel_after_invoke_test: chttp2_fake_security_cancel_after_invoke_test.exe
	echo Running chttp2_fake_security_cancel_after_invoke_test
	$(OUT_DIR)\chttp2_fake_security_cancel_after_invoke_test.exe
chttp2_fake_security_cancel_before_invoke_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_cancel_before_invoke_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_cancel_before_invoke_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_cancel_before_invoke.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_cancel_before_invoke_test: chttp2_fake_security_cancel_before_invoke_test.exe
	echo Running chttp2_fake_security_cancel_before_invoke_test
	$(OUT_DIR)\chttp2_fake_security_cancel_before_invoke_test.exe
chttp2_fake_security_cancel_in_a_vacuum_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_cancel_in_a_vacuum_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_cancel_in_a_vacuum_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_cancel_in_a_vacuum.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_cancel_in_a_vacuum_test: chttp2_fake_security_cancel_in_a_vacuum_test.exe
	echo Running chttp2_fake_security_cancel_in_a_vacuum_test
	$(OUT_DIR)\chttp2_fake_security_cancel_in_a_vacuum_test.exe
chttp2_fake_security_census_simple_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_census_simple_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_census_simple_request_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_census_simple_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_census_simple_request_test: chttp2_fake_security_census_simple_request_test.exe
	echo Running chttp2_fake_security_census_simple_request_test
	$(OUT_DIR)\chttp2_fake_security_census_simple_request_test.exe
chttp2_fake_security_disappearing_server_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_disappearing_server_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_disappearing_server_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_disappearing_server.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_disappearing_server_test: chttp2_fake_security_disappearing_server_test.exe
	echo Running chttp2_fake_security_disappearing_server_test
	$(OUT_DIR)\chttp2_fake_security_disappearing_server_test.exe
chttp2_fake_security_early_server_shutdown_finishes_inflight_calls_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_early_server_shutdown_finishes_inflight_calls_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_early_server_shutdown_finishes_inflight_calls_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_early_server_shutdown_finishes_inflight_calls.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_early_server_shutdown_finishes_inflight_calls_test: chttp2_fake_security_early_server_shutdown_finishes_inflight_calls_test.exe
	echo Running chttp2_fake_security_early_server_shutdown_finishes_inflight_calls_test
	$(OUT_DIR)\chttp2_fake_security_early_server_shutdown_finishes_inflight_calls_test.exe
chttp2_fake_security_early_server_shutdown_finishes_tags_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_early_server_shutdown_finishes_tags_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_early_server_shutdown_finishes_tags_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_early_server_shutdown_finishes_tags.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_early_server_shutdown_finishes_tags_test: chttp2_fake_security_early_server_shutdown_finishes_tags_test.exe
	echo Running chttp2_fake_security_early_server_shutdown_finishes_tags_test
	$(OUT_DIR)\chttp2_fake_security_early_server_shutdown_finishes_tags_test.exe
chttp2_fake_security_empty_batch_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_empty_batch_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_empty_batch_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_empty_batch.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_empty_batch_test: chttp2_fake_security_empty_batch_test.exe
	echo Running chttp2_fake_security_empty_batch_test
	$(OUT_DIR)\chttp2_fake_security_empty_batch_test.exe
chttp2_fake_security_graceful_server_shutdown_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_graceful_server_shutdown_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_graceful_server_shutdown_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_graceful_server_shutdown.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_graceful_server_shutdown_test: chttp2_fake_security_graceful_server_shutdown_test.exe
	echo Running chttp2_fake_security_graceful_server_shutdown_test
	$(OUT_DIR)\chttp2_fake_security_graceful_server_shutdown_test.exe
chttp2_fake_security_invoke_large_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_invoke_large_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_invoke_large_request_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_invoke_large_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_invoke_large_request_test: chttp2_fake_security_invoke_large_request_test.exe
	echo Running chttp2_fake_security_invoke_large_request_test
	$(OUT_DIR)\chttp2_fake_security_invoke_large_request_test.exe
chttp2_fake_security_max_concurrent_streams_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_max_concurrent_streams_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_max_concurrent_streams_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_max_concurrent_streams.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_max_concurrent_streams_test: chttp2_fake_security_max_concurrent_streams_test.exe
	echo Running chttp2_fake_security_max_concurrent_streams_test
	$(OUT_DIR)\chttp2_fake_security_max_concurrent_streams_test.exe
chttp2_fake_security_max_message_length_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_max_message_length_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_max_message_length_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_max_message_length.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_max_message_length_test: chttp2_fake_security_max_message_length_test.exe
	echo Running chttp2_fake_security_max_message_length_test
	$(OUT_DIR)\chttp2_fake_security_max_message_length_test.exe
chttp2_fake_security_no_op_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_no_op_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_no_op_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_no_op.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_no_op_test: chttp2_fake_security_no_op_test.exe
	echo Running chttp2_fake_security_no_op_test
	$(OUT_DIR)\chttp2_fake_security_no_op_test.exe
chttp2_fake_security_ping_pong_streaming_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_ping_pong_streaming_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_ping_pong_streaming_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_ping_pong_streaming.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_ping_pong_streaming_test: chttp2_fake_security_ping_pong_streaming_test.exe
	echo Running chttp2_fake_security_ping_pong_streaming_test
	$(OUT_DIR)\chttp2_fake_security_ping_pong_streaming_test.exe
chttp2_fake_security_registered_call_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_registered_call_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_registered_call_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_registered_call.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_registered_call_test: chttp2_fake_security_registered_call_test.exe
	echo Running chttp2_fake_security_registered_call_test
	$(OUT_DIR)\chttp2_fake_security_registered_call_test.exe
chttp2_fake_security_request_response_with_binary_metadata_and_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_request_response_with_binary_metadata_and_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_request_response_with_binary_metadata_and_payload_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_request_response_with_binary_metadata_and_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_request_response_with_binary_metadata_and_payload_test: chttp2_fake_security_request_response_with_binary_metadata_and_payload_test.exe
	echo Running chttp2_fake_security_request_response_with_binary_metadata_and_payload_test
	$(OUT_DIR)\chttp2_fake_security_request_response_with_binary_metadata_and_payload_test.exe
chttp2_fake_security_request_response_with_metadata_and_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_request_response_with_metadata_and_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_request_response_with_metadata_and_payload_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_request_response_with_metadata_and_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_request_response_with_metadata_and_payload_test: chttp2_fake_security_request_response_with_metadata_and_payload_test.exe
	echo Running chttp2_fake_security_request_response_with_metadata_and_payload_test
	$(OUT_DIR)\chttp2_fake_security_request_response_with_metadata_and_payload_test.exe
chttp2_fake_security_request_response_with_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_request_response_with_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_request_response_with_payload_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_request_response_with_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_request_response_with_payload_test: chttp2_fake_security_request_response_with_payload_test.exe
	echo Running chttp2_fake_security_request_response_with_payload_test
	$(OUT_DIR)\chttp2_fake_security_request_response_with_payload_test.exe
chttp2_fake_security_request_response_with_payload_and_call_creds_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_request_response_with_payload_and_call_creds_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_request_response_with_payload_and_call_creds_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_request_response_with_payload_and_call_creds.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_request_response_with_payload_and_call_creds_test: chttp2_fake_security_request_response_with_payload_and_call_creds_test.exe
	echo Running chttp2_fake_security_request_response_with_payload_and_call_creds_test
	$(OUT_DIR)\chttp2_fake_security_request_response_with_payload_and_call_creds_test.exe
chttp2_fake_security_request_response_with_trailing_metadata_and_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_request_response_with_trailing_metadata_and_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_request_response_with_trailing_metadata_and_payload_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_request_response_with_trailing_metadata_and_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_request_response_with_trailing_metadata_and_payload_test: chttp2_fake_security_request_response_with_trailing_metadata_and_payload_test.exe
	echo Running chttp2_fake_security_request_response_with_trailing_metadata_and_payload_test
	$(OUT_DIR)\chttp2_fake_security_request_response_with_trailing_metadata_and_payload_test.exe
chttp2_fake_security_request_with_large_metadata_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_request_with_large_metadata_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_request_with_large_metadata_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_request_with_large_metadata.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_request_with_large_metadata_test: chttp2_fake_security_request_with_large_metadata_test.exe
	echo Running chttp2_fake_security_request_with_large_metadata_test
	$(OUT_DIR)\chttp2_fake_security_request_with_large_metadata_test.exe
chttp2_fake_security_request_with_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_request_with_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_request_with_payload_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_request_with_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_request_with_payload_test: chttp2_fake_security_request_with_payload_test.exe
	echo Running chttp2_fake_security_request_with_payload_test
	$(OUT_DIR)\chttp2_fake_security_request_with_payload_test.exe
chttp2_fake_security_simple_delayed_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_simple_delayed_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_simple_delayed_request_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_simple_delayed_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_simple_delayed_request_test: chttp2_fake_security_simple_delayed_request_test.exe
	echo Running chttp2_fake_security_simple_delayed_request_test
	$(OUT_DIR)\chttp2_fake_security_simple_delayed_request_test.exe
chttp2_fake_security_simple_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_simple_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_simple_request_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_simple_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_simple_request_test: chttp2_fake_security_simple_request_test.exe
	echo Running chttp2_fake_security_simple_request_test
	$(OUT_DIR)\chttp2_fake_security_simple_request_test.exe
chttp2_fake_security_simple_request_with_high_initial_sequence_number_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fake_security_simple_request_with_high_initial_sequence_number_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fake_security_simple_request_with_high_initial_sequence_number_test.exe" Debug\end2end_fixture_chttp2_fake_security.lib Debug\end2end_test_simple_request_with_high_initial_sequence_number.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fake_security_simple_request_with_high_initial_sequence_number_test: chttp2_fake_security_simple_request_with_high_initial_sequence_number_test.exe
	echo Running chttp2_fake_security_simple_request_with_high_initial_sequence_number_test
	$(OUT_DIR)\chttp2_fake_security_simple_request_with_high_initial_sequence_number_test.exe
chttp2_fullstack_bad_hostname_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_bad_hostname_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_bad_hostname_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_bad_hostname.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_bad_hostname_test: chttp2_fullstack_bad_hostname_test.exe
	echo Running chttp2_fullstack_bad_hostname_test
	$(OUT_DIR)\chttp2_fullstack_bad_hostname_test.exe
chttp2_fullstack_cancel_after_accept_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_cancel_after_accept_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_cancel_after_accept_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_cancel_after_accept.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_cancel_after_accept_test: chttp2_fullstack_cancel_after_accept_test.exe
	echo Running chttp2_fullstack_cancel_after_accept_test
	$(OUT_DIR)\chttp2_fullstack_cancel_after_accept_test.exe
chttp2_fullstack_cancel_after_accept_and_writes_closed_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_cancel_after_accept_and_writes_closed_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_cancel_after_accept_and_writes_closed_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_cancel_after_accept_and_writes_closed.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_cancel_after_accept_and_writes_closed_test: chttp2_fullstack_cancel_after_accept_and_writes_closed_test.exe
	echo Running chttp2_fullstack_cancel_after_accept_and_writes_closed_test
	$(OUT_DIR)\chttp2_fullstack_cancel_after_accept_and_writes_closed_test.exe
chttp2_fullstack_cancel_after_invoke_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_cancel_after_invoke_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_cancel_after_invoke_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_cancel_after_invoke.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_cancel_after_invoke_test: chttp2_fullstack_cancel_after_invoke_test.exe
	echo Running chttp2_fullstack_cancel_after_invoke_test
	$(OUT_DIR)\chttp2_fullstack_cancel_after_invoke_test.exe
chttp2_fullstack_cancel_before_invoke_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_cancel_before_invoke_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_cancel_before_invoke_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_cancel_before_invoke.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_cancel_before_invoke_test: chttp2_fullstack_cancel_before_invoke_test.exe
	echo Running chttp2_fullstack_cancel_before_invoke_test
	$(OUT_DIR)\chttp2_fullstack_cancel_before_invoke_test.exe
chttp2_fullstack_cancel_in_a_vacuum_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_cancel_in_a_vacuum_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_cancel_in_a_vacuum_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_cancel_in_a_vacuum.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_cancel_in_a_vacuum_test: chttp2_fullstack_cancel_in_a_vacuum_test.exe
	echo Running chttp2_fullstack_cancel_in_a_vacuum_test
	$(OUT_DIR)\chttp2_fullstack_cancel_in_a_vacuum_test.exe
chttp2_fullstack_census_simple_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_census_simple_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_census_simple_request_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_census_simple_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_census_simple_request_test: chttp2_fullstack_census_simple_request_test.exe
	echo Running chttp2_fullstack_census_simple_request_test
	$(OUT_DIR)\chttp2_fullstack_census_simple_request_test.exe
chttp2_fullstack_disappearing_server_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_disappearing_server_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_disappearing_server_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_disappearing_server.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_disappearing_server_test: chttp2_fullstack_disappearing_server_test.exe
	echo Running chttp2_fullstack_disappearing_server_test
	$(OUT_DIR)\chttp2_fullstack_disappearing_server_test.exe
chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_early_server_shutdown_finishes_inflight_calls.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_test: chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_test.exe
	echo Running chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_test
	$(OUT_DIR)\chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_test.exe
chttp2_fullstack_early_server_shutdown_finishes_tags_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_early_server_shutdown_finishes_tags_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_early_server_shutdown_finishes_tags_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_early_server_shutdown_finishes_tags.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_early_server_shutdown_finishes_tags_test: chttp2_fullstack_early_server_shutdown_finishes_tags_test.exe
	echo Running chttp2_fullstack_early_server_shutdown_finishes_tags_test
	$(OUT_DIR)\chttp2_fullstack_early_server_shutdown_finishes_tags_test.exe
chttp2_fullstack_empty_batch_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_empty_batch_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_empty_batch_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_empty_batch.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_empty_batch_test: chttp2_fullstack_empty_batch_test.exe
	echo Running chttp2_fullstack_empty_batch_test
	$(OUT_DIR)\chttp2_fullstack_empty_batch_test.exe
chttp2_fullstack_graceful_server_shutdown_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_graceful_server_shutdown_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_graceful_server_shutdown_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_graceful_server_shutdown.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_graceful_server_shutdown_test: chttp2_fullstack_graceful_server_shutdown_test.exe
	echo Running chttp2_fullstack_graceful_server_shutdown_test
	$(OUT_DIR)\chttp2_fullstack_graceful_server_shutdown_test.exe
chttp2_fullstack_invoke_large_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_invoke_large_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_invoke_large_request_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_invoke_large_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_invoke_large_request_test: chttp2_fullstack_invoke_large_request_test.exe
	echo Running chttp2_fullstack_invoke_large_request_test
	$(OUT_DIR)\chttp2_fullstack_invoke_large_request_test.exe
chttp2_fullstack_max_concurrent_streams_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_max_concurrent_streams_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_max_concurrent_streams_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_max_concurrent_streams.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_max_concurrent_streams_test: chttp2_fullstack_max_concurrent_streams_test.exe
	echo Running chttp2_fullstack_max_concurrent_streams_test
	$(OUT_DIR)\chttp2_fullstack_max_concurrent_streams_test.exe
chttp2_fullstack_max_message_length_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_max_message_length_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_max_message_length_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_max_message_length.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_max_message_length_test: chttp2_fullstack_max_message_length_test.exe
	echo Running chttp2_fullstack_max_message_length_test
	$(OUT_DIR)\chttp2_fullstack_max_message_length_test.exe
chttp2_fullstack_no_op_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_no_op_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_no_op_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_no_op.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_no_op_test: chttp2_fullstack_no_op_test.exe
	echo Running chttp2_fullstack_no_op_test
	$(OUT_DIR)\chttp2_fullstack_no_op_test.exe
chttp2_fullstack_ping_pong_streaming_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_ping_pong_streaming_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_ping_pong_streaming_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_ping_pong_streaming.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_ping_pong_streaming_test: chttp2_fullstack_ping_pong_streaming_test.exe
	echo Running chttp2_fullstack_ping_pong_streaming_test
	$(OUT_DIR)\chttp2_fullstack_ping_pong_streaming_test.exe
chttp2_fullstack_registered_call_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_registered_call_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_registered_call_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_registered_call.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_registered_call_test: chttp2_fullstack_registered_call_test.exe
	echo Running chttp2_fullstack_registered_call_test
	$(OUT_DIR)\chttp2_fullstack_registered_call_test.exe
chttp2_fullstack_request_response_with_binary_metadata_and_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_request_response_with_binary_metadata_and_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_request_response_with_binary_metadata_and_payload_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_request_response_with_binary_metadata_and_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_request_response_with_binary_metadata_and_payload_test: chttp2_fullstack_request_response_with_binary_metadata_and_payload_test.exe
	echo Running chttp2_fullstack_request_response_with_binary_metadata_and_payload_test
	$(OUT_DIR)\chttp2_fullstack_request_response_with_binary_metadata_and_payload_test.exe
chttp2_fullstack_request_response_with_metadata_and_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_request_response_with_metadata_and_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_request_response_with_metadata_and_payload_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_request_response_with_metadata_and_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_request_response_with_metadata_and_payload_test: chttp2_fullstack_request_response_with_metadata_and_payload_test.exe
	echo Running chttp2_fullstack_request_response_with_metadata_and_payload_test
	$(OUT_DIR)\chttp2_fullstack_request_response_with_metadata_and_payload_test.exe
chttp2_fullstack_request_response_with_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_request_response_with_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_request_response_with_payload_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_request_response_with_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_request_response_with_payload_test: chttp2_fullstack_request_response_with_payload_test.exe
	echo Running chttp2_fullstack_request_response_with_payload_test
	$(OUT_DIR)\chttp2_fullstack_request_response_with_payload_test.exe
chttp2_fullstack_request_response_with_payload_and_call_creds_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_request_response_with_payload_and_call_creds_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_request_response_with_payload_and_call_creds_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_request_response_with_payload_and_call_creds.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_request_response_with_payload_and_call_creds_test: chttp2_fullstack_request_response_with_payload_and_call_creds_test.exe
	echo Running chttp2_fullstack_request_response_with_payload_and_call_creds_test
	$(OUT_DIR)\chttp2_fullstack_request_response_with_payload_and_call_creds_test.exe
chttp2_fullstack_request_response_with_trailing_metadata_and_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_request_response_with_trailing_metadata_and_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_request_response_with_trailing_metadata_and_payload_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_request_response_with_trailing_metadata_and_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_request_response_with_trailing_metadata_and_payload_test: chttp2_fullstack_request_response_with_trailing_metadata_and_payload_test.exe
	echo Running chttp2_fullstack_request_response_with_trailing_metadata_and_payload_test
	$(OUT_DIR)\chttp2_fullstack_request_response_with_trailing_metadata_and_payload_test.exe
chttp2_fullstack_request_with_large_metadata_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_request_with_large_metadata_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_request_with_large_metadata_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_request_with_large_metadata.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_request_with_large_metadata_test: chttp2_fullstack_request_with_large_metadata_test.exe
	echo Running chttp2_fullstack_request_with_large_metadata_test
	$(OUT_DIR)\chttp2_fullstack_request_with_large_metadata_test.exe
chttp2_fullstack_request_with_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_request_with_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_request_with_payload_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_request_with_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_request_with_payload_test: chttp2_fullstack_request_with_payload_test.exe
	echo Running chttp2_fullstack_request_with_payload_test
	$(OUT_DIR)\chttp2_fullstack_request_with_payload_test.exe
chttp2_fullstack_simple_delayed_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_simple_delayed_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_simple_delayed_request_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_simple_delayed_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_simple_delayed_request_test: chttp2_fullstack_simple_delayed_request_test.exe
	echo Running chttp2_fullstack_simple_delayed_request_test
	$(OUT_DIR)\chttp2_fullstack_simple_delayed_request_test.exe
chttp2_fullstack_simple_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_simple_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_simple_request_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_simple_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_simple_request_test: chttp2_fullstack_simple_request_test.exe
	echo Running chttp2_fullstack_simple_request_test
	$(OUT_DIR)\chttp2_fullstack_simple_request_test.exe
chttp2_fullstack_simple_request_with_high_initial_sequence_number_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_simple_request_with_high_initial_sequence_number_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_simple_request_with_high_initial_sequence_number_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_simple_request_with_high_initial_sequence_number.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_simple_request_with_high_initial_sequence_number_test: chttp2_fullstack_simple_request_with_high_initial_sequence_number_test.exe
	echo Running chttp2_fullstack_simple_request_with_high_initial_sequence_number_test
	$(OUT_DIR)\chttp2_fullstack_simple_request_with_high_initial_sequence_number_test.exe
chttp2_simple_ssl_fullstack_bad_hostname_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_bad_hostname_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_bad_hostname_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_bad_hostname.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_bad_hostname_test: chttp2_simple_ssl_fullstack_bad_hostname_test.exe
	echo Running chttp2_simple_ssl_fullstack_bad_hostname_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_bad_hostname_test.exe
chttp2_simple_ssl_fullstack_cancel_after_accept_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_cancel_after_accept_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_cancel_after_accept_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_cancel_after_accept.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_cancel_after_accept_test: chttp2_simple_ssl_fullstack_cancel_after_accept_test.exe
	echo Running chttp2_simple_ssl_fullstack_cancel_after_accept_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_cancel_after_accept_test.exe
chttp2_simple_ssl_fullstack_cancel_after_accept_and_writes_closed_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_cancel_after_accept_and_writes_closed_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_cancel_after_accept_and_writes_closed_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_cancel_after_accept_and_writes_closed.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_cancel_after_accept_and_writes_closed_test: chttp2_simple_ssl_fullstack_cancel_after_accept_and_writes_closed_test.exe
	echo Running chttp2_simple_ssl_fullstack_cancel_after_accept_and_writes_closed_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_cancel_after_accept_and_writes_closed_test.exe
chttp2_simple_ssl_fullstack_cancel_after_invoke_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_cancel_after_invoke_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_cancel_after_invoke_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_cancel_after_invoke.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_cancel_after_invoke_test: chttp2_simple_ssl_fullstack_cancel_after_invoke_test.exe
	echo Running chttp2_simple_ssl_fullstack_cancel_after_invoke_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_cancel_after_invoke_test.exe
chttp2_simple_ssl_fullstack_cancel_before_invoke_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_cancel_before_invoke_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_cancel_before_invoke_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_cancel_before_invoke.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_cancel_before_invoke_test: chttp2_simple_ssl_fullstack_cancel_before_invoke_test.exe
	echo Running chttp2_simple_ssl_fullstack_cancel_before_invoke_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_cancel_before_invoke_test.exe
chttp2_simple_ssl_fullstack_cancel_in_a_vacuum_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_cancel_in_a_vacuum_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_cancel_in_a_vacuum_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_cancel_in_a_vacuum.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_cancel_in_a_vacuum_test: chttp2_simple_ssl_fullstack_cancel_in_a_vacuum_test.exe
	echo Running chttp2_simple_ssl_fullstack_cancel_in_a_vacuum_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_cancel_in_a_vacuum_test.exe
chttp2_simple_ssl_fullstack_census_simple_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_census_simple_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_census_simple_request_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_census_simple_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_census_simple_request_test: chttp2_simple_ssl_fullstack_census_simple_request_test.exe
	echo Running chttp2_simple_ssl_fullstack_census_simple_request_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_census_simple_request_test.exe
chttp2_simple_ssl_fullstack_disappearing_server_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_disappearing_server_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_disappearing_server_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_disappearing_server.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_disappearing_server_test: chttp2_simple_ssl_fullstack_disappearing_server_test.exe
	echo Running chttp2_simple_ssl_fullstack_disappearing_server_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_disappearing_server_test.exe
chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_inflight_calls_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_inflight_calls_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_inflight_calls_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_early_server_shutdown_finishes_inflight_calls.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_inflight_calls_test: chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_inflight_calls_test.exe
	echo Running chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_inflight_calls_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_inflight_calls_test.exe
chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_tags_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_tags_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_tags_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_early_server_shutdown_finishes_tags.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_tags_test: chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_tags_test.exe
	echo Running chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_tags_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_tags_test.exe
chttp2_simple_ssl_fullstack_empty_batch_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_empty_batch_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_empty_batch_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_empty_batch.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_empty_batch_test: chttp2_simple_ssl_fullstack_empty_batch_test.exe
	echo Running chttp2_simple_ssl_fullstack_empty_batch_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_empty_batch_test.exe
chttp2_simple_ssl_fullstack_graceful_server_shutdown_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_graceful_server_shutdown_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_graceful_server_shutdown_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_graceful_server_shutdown.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_graceful_server_shutdown_test: chttp2_simple_ssl_fullstack_graceful_server_shutdown_test.exe
	echo Running chttp2_simple_ssl_fullstack_graceful_server_shutdown_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_graceful_server_shutdown_test.exe
chttp2_simple_ssl_fullstack_invoke_large_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_invoke_large_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_invoke_large_request_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_invoke_large_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_invoke_large_request_test: chttp2_simple_ssl_fullstack_invoke_large_request_test.exe
	echo Running chttp2_simple_ssl_fullstack_invoke_large_request_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_invoke_large_request_test.exe
chttp2_simple_ssl_fullstack_max_concurrent_streams_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_max_concurrent_streams_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_max_concurrent_streams_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_max_concurrent_streams.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_max_concurrent_streams_test: chttp2_simple_ssl_fullstack_max_concurrent_streams_test.exe
	echo Running chttp2_simple_ssl_fullstack_max_concurrent_streams_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_max_concurrent_streams_test.exe
chttp2_simple_ssl_fullstack_max_message_length_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_max_message_length_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_max_message_length_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_max_message_length.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_max_message_length_test: chttp2_simple_ssl_fullstack_max_message_length_test.exe
	echo Running chttp2_simple_ssl_fullstack_max_message_length_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_max_message_length_test.exe
chttp2_simple_ssl_fullstack_no_op_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_no_op_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_no_op_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_no_op.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_no_op_test: chttp2_simple_ssl_fullstack_no_op_test.exe
	echo Running chttp2_simple_ssl_fullstack_no_op_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_no_op_test.exe
chttp2_simple_ssl_fullstack_ping_pong_streaming_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_ping_pong_streaming_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_ping_pong_streaming_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_ping_pong_streaming.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_ping_pong_streaming_test: chttp2_simple_ssl_fullstack_ping_pong_streaming_test.exe
	echo Running chttp2_simple_ssl_fullstack_ping_pong_streaming_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_ping_pong_streaming_test.exe
chttp2_simple_ssl_fullstack_registered_call_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_registered_call_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_registered_call_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_registered_call.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_registered_call_test: chttp2_simple_ssl_fullstack_registered_call_test.exe
	echo Running chttp2_simple_ssl_fullstack_registered_call_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_registered_call_test.exe
chttp2_simple_ssl_fullstack_request_response_with_binary_metadata_and_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_request_response_with_binary_metadata_and_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_request_response_with_binary_metadata_and_payload_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_request_response_with_binary_metadata_and_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_request_response_with_binary_metadata_and_payload_test: chttp2_simple_ssl_fullstack_request_response_with_binary_metadata_and_payload_test.exe
	echo Running chttp2_simple_ssl_fullstack_request_response_with_binary_metadata_and_payload_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_request_response_with_binary_metadata_and_payload_test.exe
chttp2_simple_ssl_fullstack_request_response_with_metadata_and_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_request_response_with_metadata_and_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_request_response_with_metadata_and_payload_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_request_response_with_metadata_and_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_request_response_with_metadata_and_payload_test: chttp2_simple_ssl_fullstack_request_response_with_metadata_and_payload_test.exe
	echo Running chttp2_simple_ssl_fullstack_request_response_with_metadata_and_payload_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_request_response_with_metadata_and_payload_test.exe
chttp2_simple_ssl_fullstack_request_response_with_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_request_response_with_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_request_response_with_payload_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_request_response_with_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_request_response_with_payload_test: chttp2_simple_ssl_fullstack_request_response_with_payload_test.exe
	echo Running chttp2_simple_ssl_fullstack_request_response_with_payload_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_request_response_with_payload_test.exe
chttp2_simple_ssl_fullstack_request_response_with_payload_and_call_creds_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_request_response_with_payload_and_call_creds_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_request_response_with_payload_and_call_creds_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_request_response_with_payload_and_call_creds.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_request_response_with_payload_and_call_creds_test: chttp2_simple_ssl_fullstack_request_response_with_payload_and_call_creds_test.exe
	echo Running chttp2_simple_ssl_fullstack_request_response_with_payload_and_call_creds_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_request_response_with_payload_and_call_creds_test.exe
chttp2_simple_ssl_fullstack_request_response_with_trailing_metadata_and_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_request_response_with_trailing_metadata_and_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_request_response_with_trailing_metadata_and_payload_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_request_response_with_trailing_metadata_and_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_request_response_with_trailing_metadata_and_payload_test: chttp2_simple_ssl_fullstack_request_response_with_trailing_metadata_and_payload_test.exe
	echo Running chttp2_simple_ssl_fullstack_request_response_with_trailing_metadata_and_payload_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_request_response_with_trailing_metadata_and_payload_test.exe
chttp2_simple_ssl_fullstack_request_with_large_metadata_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_request_with_large_metadata_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_request_with_large_metadata_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_request_with_large_metadata.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_request_with_large_metadata_test: chttp2_simple_ssl_fullstack_request_with_large_metadata_test.exe
	echo Running chttp2_simple_ssl_fullstack_request_with_large_metadata_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_request_with_large_metadata_test.exe
chttp2_simple_ssl_fullstack_request_with_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_request_with_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_request_with_payload_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_request_with_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_request_with_payload_test: chttp2_simple_ssl_fullstack_request_with_payload_test.exe
	echo Running chttp2_simple_ssl_fullstack_request_with_payload_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_request_with_payload_test.exe
chttp2_simple_ssl_fullstack_simple_delayed_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_simple_delayed_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_simple_delayed_request_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_simple_delayed_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_simple_delayed_request_test: chttp2_simple_ssl_fullstack_simple_delayed_request_test.exe
	echo Running chttp2_simple_ssl_fullstack_simple_delayed_request_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_simple_delayed_request_test.exe
chttp2_simple_ssl_fullstack_simple_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_simple_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_simple_request_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_simple_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_simple_request_test: chttp2_simple_ssl_fullstack_simple_request_test.exe
	echo Running chttp2_simple_ssl_fullstack_simple_request_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_simple_request_test.exe
chttp2_simple_ssl_fullstack_simple_request_with_high_initial_sequence_number_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_fullstack_simple_request_with_high_initial_sequence_number_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_fullstack_simple_request_with_high_initial_sequence_number_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib Debug\end2end_test_simple_request_with_high_initial_sequence_number.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_fullstack_simple_request_with_high_initial_sequence_number_test: chttp2_simple_ssl_fullstack_simple_request_with_high_initial_sequence_number_test.exe
	echo Running chttp2_simple_ssl_fullstack_simple_request_with_high_initial_sequence_number_test
	$(OUT_DIR)\chttp2_simple_ssl_fullstack_simple_request_with_high_initial_sequence_number_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_bad_hostname_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_bad_hostname_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_bad_hostname_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_bad_hostname.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_bad_hostname_test: chttp2_simple_ssl_with_oauth2_fullstack_bad_hostname_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_bad_hostname_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_bad_hostname_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_cancel_after_accept.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_test: chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_and_writes_closed_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_and_writes_closed_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_and_writes_closed_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_cancel_after_accept_and_writes_closed.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_and_writes_closed_test: chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_and_writes_closed_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_and_writes_closed_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_and_writes_closed_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_invoke_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_invoke_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_invoke_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_cancel_after_invoke.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_invoke_test: chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_invoke_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_invoke_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_invoke_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_cancel_before_invoke_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_cancel_before_invoke_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_cancel_before_invoke_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_cancel_before_invoke.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_cancel_before_invoke_test: chttp2_simple_ssl_with_oauth2_fullstack_cancel_before_invoke_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_cancel_before_invoke_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_cancel_before_invoke_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_cancel_in_a_vacuum_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_cancel_in_a_vacuum_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_cancel_in_a_vacuum_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_cancel_in_a_vacuum.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_cancel_in_a_vacuum_test: chttp2_simple_ssl_with_oauth2_fullstack_cancel_in_a_vacuum_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_cancel_in_a_vacuum_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_cancel_in_a_vacuum_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_census_simple_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_census_simple_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_census_simple_request_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_census_simple_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_census_simple_request_test: chttp2_simple_ssl_with_oauth2_fullstack_census_simple_request_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_census_simple_request_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_census_simple_request_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_disappearing_server_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_disappearing_server_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_disappearing_server_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_disappearing_server.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_disappearing_server_test: chttp2_simple_ssl_with_oauth2_fullstack_disappearing_server_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_disappearing_server_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_disappearing_server_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_inflight_calls_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_inflight_calls_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_inflight_calls_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_early_server_shutdown_finishes_inflight_calls.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_inflight_calls_test: chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_inflight_calls_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_inflight_calls_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_inflight_calls_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_tags_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_tags_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_tags_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_early_server_shutdown_finishes_tags.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_tags_test: chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_tags_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_tags_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_tags_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_empty_batch_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_empty_batch_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_empty_batch_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_empty_batch.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_empty_batch_test: chttp2_simple_ssl_with_oauth2_fullstack_empty_batch_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_empty_batch_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_empty_batch_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_graceful_server_shutdown_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_graceful_server_shutdown_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_graceful_server_shutdown_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_graceful_server_shutdown.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_graceful_server_shutdown_test: chttp2_simple_ssl_with_oauth2_fullstack_graceful_server_shutdown_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_graceful_server_shutdown_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_graceful_server_shutdown_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_invoke_large_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_invoke_large_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_invoke_large_request_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_invoke_large_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_invoke_large_request_test: chttp2_simple_ssl_with_oauth2_fullstack_invoke_large_request_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_invoke_large_request_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_invoke_large_request_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_max_concurrent_streams_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_max_concurrent_streams_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_max_concurrent_streams_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_max_concurrent_streams.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_max_concurrent_streams_test: chttp2_simple_ssl_with_oauth2_fullstack_max_concurrent_streams_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_max_concurrent_streams_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_max_concurrent_streams_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_max_message_length_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_max_message_length_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_max_message_length_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_max_message_length.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_max_message_length_test: chttp2_simple_ssl_with_oauth2_fullstack_max_message_length_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_max_message_length_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_max_message_length_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_no_op_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_no_op_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_no_op_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_no_op.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_no_op_test: chttp2_simple_ssl_with_oauth2_fullstack_no_op_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_no_op_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_no_op_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_ping_pong_streaming_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_ping_pong_streaming_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_ping_pong_streaming_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_ping_pong_streaming.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_ping_pong_streaming_test: chttp2_simple_ssl_with_oauth2_fullstack_ping_pong_streaming_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_ping_pong_streaming_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_ping_pong_streaming_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_registered_call_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_registered_call_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_registered_call_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_registered_call.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_registered_call_test: chttp2_simple_ssl_with_oauth2_fullstack_registered_call_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_registered_call_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_registered_call_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_binary_metadata_and_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_binary_metadata_and_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_binary_metadata_and_payload_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_request_response_with_binary_metadata_and_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_binary_metadata_and_payload_test: chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_binary_metadata_and_payload_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_binary_metadata_and_payload_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_binary_metadata_and_payload_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_metadata_and_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_metadata_and_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_metadata_and_payload_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_request_response_with_metadata_and_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_metadata_and_payload_test: chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_metadata_and_payload_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_metadata_and_payload_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_metadata_and_payload_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_request_response_with_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_test: chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_and_call_creds_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_and_call_creds_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_and_call_creds_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_request_response_with_payload_and_call_creds.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_and_call_creds_test: chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_and_call_creds_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_and_call_creds_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_and_call_creds_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_trailing_metadata_and_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_trailing_metadata_and_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_trailing_metadata_and_payload_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_request_response_with_trailing_metadata_and_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_trailing_metadata_and_payload_test: chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_trailing_metadata_and_payload_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_trailing_metadata_and_payload_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_trailing_metadata_and_payload_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_request_with_large_metadata_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_request_with_large_metadata_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_request_with_large_metadata_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_request_with_large_metadata.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_request_with_large_metadata_test: chttp2_simple_ssl_with_oauth2_fullstack_request_with_large_metadata_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_request_with_large_metadata_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_request_with_large_metadata_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_request_with_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_request_with_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_request_with_payload_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_request_with_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_request_with_payload_test: chttp2_simple_ssl_with_oauth2_fullstack_request_with_payload_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_request_with_payload_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_request_with_payload_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_simple_delayed_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_simple_delayed_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_simple_delayed_request_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_simple_delayed_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_simple_delayed_request_test: chttp2_simple_ssl_with_oauth2_fullstack_simple_delayed_request_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_simple_delayed_request_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_simple_delayed_request_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_simple_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_simple_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_simple_request_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_simple_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_simple_request_test: chttp2_simple_ssl_with_oauth2_fullstack_simple_request_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_simple_request_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_simple_request_test.exe
chttp2_simple_ssl_with_oauth2_fullstack_simple_request_with_high_initial_sequence_number_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_simple_ssl_with_oauth2_fullstack_simple_request_with_high_initial_sequence_number_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_simple_request_with_high_initial_sequence_number_test.exe" Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib Debug\end2end_test_simple_request_with_high_initial_sequence_number.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_simple_ssl_with_oauth2_fullstack_simple_request_with_high_initial_sequence_number_test: chttp2_simple_ssl_with_oauth2_fullstack_simple_request_with_high_initial_sequence_number_test.exe
	echo Running chttp2_simple_ssl_with_oauth2_fullstack_simple_request_with_high_initial_sequence_number_test
	$(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack_simple_request_with_high_initial_sequence_number_test.exe
chttp2_socket_pair_bad_hostname_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_bad_hostname_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_bad_hostname_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_bad_hostname.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_bad_hostname_test: chttp2_socket_pair_bad_hostname_test.exe
	echo Running chttp2_socket_pair_bad_hostname_test
	$(OUT_DIR)\chttp2_socket_pair_bad_hostname_test.exe
chttp2_socket_pair_cancel_after_accept_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_cancel_after_accept_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_cancel_after_accept_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_cancel_after_accept.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_cancel_after_accept_test: chttp2_socket_pair_cancel_after_accept_test.exe
	echo Running chttp2_socket_pair_cancel_after_accept_test
	$(OUT_DIR)\chttp2_socket_pair_cancel_after_accept_test.exe
chttp2_socket_pair_cancel_after_accept_and_writes_closed_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_cancel_after_accept_and_writes_closed_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_cancel_after_accept_and_writes_closed_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_cancel_after_accept_and_writes_closed.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_cancel_after_accept_and_writes_closed_test: chttp2_socket_pair_cancel_after_accept_and_writes_closed_test.exe
	echo Running chttp2_socket_pair_cancel_after_accept_and_writes_closed_test
	$(OUT_DIR)\chttp2_socket_pair_cancel_after_accept_and_writes_closed_test.exe
chttp2_socket_pair_cancel_after_invoke_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_cancel_after_invoke_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_cancel_after_invoke_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_cancel_after_invoke.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_cancel_after_invoke_test: chttp2_socket_pair_cancel_after_invoke_test.exe
	echo Running chttp2_socket_pair_cancel_after_invoke_test
	$(OUT_DIR)\chttp2_socket_pair_cancel_after_invoke_test.exe
chttp2_socket_pair_cancel_before_invoke_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_cancel_before_invoke_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_cancel_before_invoke_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_cancel_before_invoke.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_cancel_before_invoke_test: chttp2_socket_pair_cancel_before_invoke_test.exe
	echo Running chttp2_socket_pair_cancel_before_invoke_test
	$(OUT_DIR)\chttp2_socket_pair_cancel_before_invoke_test.exe
chttp2_socket_pair_cancel_in_a_vacuum_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_cancel_in_a_vacuum_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_cancel_in_a_vacuum_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_cancel_in_a_vacuum.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_cancel_in_a_vacuum_test: chttp2_socket_pair_cancel_in_a_vacuum_test.exe
	echo Running chttp2_socket_pair_cancel_in_a_vacuum_test
	$(OUT_DIR)\chttp2_socket_pair_cancel_in_a_vacuum_test.exe
chttp2_socket_pair_census_simple_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_census_simple_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_census_simple_request_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_census_simple_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_census_simple_request_test: chttp2_socket_pair_census_simple_request_test.exe
	echo Running chttp2_socket_pair_census_simple_request_test
	$(OUT_DIR)\chttp2_socket_pair_census_simple_request_test.exe
chttp2_socket_pair_disappearing_server_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_disappearing_server_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_disappearing_server_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_disappearing_server.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_disappearing_server_test: chttp2_socket_pair_disappearing_server_test.exe
	echo Running chttp2_socket_pair_disappearing_server_test
	$(OUT_DIR)\chttp2_socket_pair_disappearing_server_test.exe
chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_early_server_shutdown_finishes_inflight_calls.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_test: chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_test.exe
	echo Running chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_test
	$(OUT_DIR)\chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_test.exe
chttp2_socket_pair_early_server_shutdown_finishes_tags_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_early_server_shutdown_finishes_tags_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_early_server_shutdown_finishes_tags_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_early_server_shutdown_finishes_tags.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_early_server_shutdown_finishes_tags_test: chttp2_socket_pair_early_server_shutdown_finishes_tags_test.exe
	echo Running chttp2_socket_pair_early_server_shutdown_finishes_tags_test
	$(OUT_DIR)\chttp2_socket_pair_early_server_shutdown_finishes_tags_test.exe
chttp2_socket_pair_empty_batch_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_empty_batch_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_empty_batch_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_empty_batch.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_empty_batch_test: chttp2_socket_pair_empty_batch_test.exe
	echo Running chttp2_socket_pair_empty_batch_test
	$(OUT_DIR)\chttp2_socket_pair_empty_batch_test.exe
chttp2_socket_pair_graceful_server_shutdown_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_graceful_server_shutdown_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_graceful_server_shutdown_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_graceful_server_shutdown.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_graceful_server_shutdown_test: chttp2_socket_pair_graceful_server_shutdown_test.exe
	echo Running chttp2_socket_pair_graceful_server_shutdown_test
	$(OUT_DIR)\chttp2_socket_pair_graceful_server_shutdown_test.exe
chttp2_socket_pair_invoke_large_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_invoke_large_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_invoke_large_request_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_invoke_large_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_invoke_large_request_test: chttp2_socket_pair_invoke_large_request_test.exe
	echo Running chttp2_socket_pair_invoke_large_request_test
	$(OUT_DIR)\chttp2_socket_pair_invoke_large_request_test.exe
chttp2_socket_pair_max_concurrent_streams_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_max_concurrent_streams_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_max_concurrent_streams_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_max_concurrent_streams.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_max_concurrent_streams_test: chttp2_socket_pair_max_concurrent_streams_test.exe
	echo Running chttp2_socket_pair_max_concurrent_streams_test
	$(OUT_DIR)\chttp2_socket_pair_max_concurrent_streams_test.exe
chttp2_socket_pair_max_message_length_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_max_message_length_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_max_message_length_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_max_message_length.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_max_message_length_test: chttp2_socket_pair_max_message_length_test.exe
	echo Running chttp2_socket_pair_max_message_length_test
	$(OUT_DIR)\chttp2_socket_pair_max_message_length_test.exe
chttp2_socket_pair_no_op_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_no_op_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_no_op_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_no_op.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_no_op_test: chttp2_socket_pair_no_op_test.exe
	echo Running chttp2_socket_pair_no_op_test
	$(OUT_DIR)\chttp2_socket_pair_no_op_test.exe
chttp2_socket_pair_ping_pong_streaming_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_ping_pong_streaming_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_ping_pong_streaming_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_ping_pong_streaming.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_ping_pong_streaming_test: chttp2_socket_pair_ping_pong_streaming_test.exe
	echo Running chttp2_socket_pair_ping_pong_streaming_test
	$(OUT_DIR)\chttp2_socket_pair_ping_pong_streaming_test.exe
chttp2_socket_pair_registered_call_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_registered_call_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_registered_call_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_registered_call.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_registered_call_test: chttp2_socket_pair_registered_call_test.exe
	echo Running chttp2_socket_pair_registered_call_test
	$(OUT_DIR)\chttp2_socket_pair_registered_call_test.exe
chttp2_socket_pair_request_response_with_binary_metadata_and_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_request_response_with_binary_metadata_and_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_request_response_with_binary_metadata_and_payload_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_request_response_with_binary_metadata_and_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_request_response_with_binary_metadata_and_payload_test: chttp2_socket_pair_request_response_with_binary_metadata_and_payload_test.exe
	echo Running chttp2_socket_pair_request_response_with_binary_metadata_and_payload_test
	$(OUT_DIR)\chttp2_socket_pair_request_response_with_binary_metadata_and_payload_test.exe
chttp2_socket_pair_request_response_with_metadata_and_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_request_response_with_metadata_and_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_request_response_with_metadata_and_payload_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_request_response_with_metadata_and_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_request_response_with_metadata_and_payload_test: chttp2_socket_pair_request_response_with_metadata_and_payload_test.exe
	echo Running chttp2_socket_pair_request_response_with_metadata_and_payload_test
	$(OUT_DIR)\chttp2_socket_pair_request_response_with_metadata_and_payload_test.exe
chttp2_socket_pair_request_response_with_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_request_response_with_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_request_response_with_payload_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_request_response_with_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_request_response_with_payload_test: chttp2_socket_pair_request_response_with_payload_test.exe
	echo Running chttp2_socket_pair_request_response_with_payload_test
	$(OUT_DIR)\chttp2_socket_pair_request_response_with_payload_test.exe
chttp2_socket_pair_request_response_with_payload_and_call_creds_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_request_response_with_payload_and_call_creds_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_request_response_with_payload_and_call_creds_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_request_response_with_payload_and_call_creds.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_request_response_with_payload_and_call_creds_test: chttp2_socket_pair_request_response_with_payload_and_call_creds_test.exe
	echo Running chttp2_socket_pair_request_response_with_payload_and_call_creds_test
	$(OUT_DIR)\chttp2_socket_pair_request_response_with_payload_and_call_creds_test.exe
chttp2_socket_pair_request_response_with_trailing_metadata_and_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_request_response_with_trailing_metadata_and_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_request_response_with_trailing_metadata_and_payload_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_request_response_with_trailing_metadata_and_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_request_response_with_trailing_metadata_and_payload_test: chttp2_socket_pair_request_response_with_trailing_metadata_and_payload_test.exe
	echo Running chttp2_socket_pair_request_response_with_trailing_metadata_and_payload_test
	$(OUT_DIR)\chttp2_socket_pair_request_response_with_trailing_metadata_and_payload_test.exe
chttp2_socket_pair_request_with_large_metadata_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_request_with_large_metadata_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_request_with_large_metadata_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_request_with_large_metadata.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_request_with_large_metadata_test: chttp2_socket_pair_request_with_large_metadata_test.exe
	echo Running chttp2_socket_pair_request_with_large_metadata_test
	$(OUT_DIR)\chttp2_socket_pair_request_with_large_metadata_test.exe
chttp2_socket_pair_request_with_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_request_with_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_request_with_payload_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_request_with_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_request_with_payload_test: chttp2_socket_pair_request_with_payload_test.exe
	echo Running chttp2_socket_pair_request_with_payload_test
	$(OUT_DIR)\chttp2_socket_pair_request_with_payload_test.exe
chttp2_socket_pair_simple_delayed_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_simple_delayed_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_simple_delayed_request_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_simple_delayed_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_simple_delayed_request_test: chttp2_socket_pair_simple_delayed_request_test.exe
	echo Running chttp2_socket_pair_simple_delayed_request_test
	$(OUT_DIR)\chttp2_socket_pair_simple_delayed_request_test.exe
chttp2_socket_pair_simple_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_simple_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_simple_request_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_simple_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_simple_request_test: chttp2_socket_pair_simple_request_test.exe
	echo Running chttp2_socket_pair_simple_request_test
	$(OUT_DIR)\chttp2_socket_pair_simple_request_test.exe
chttp2_socket_pair_simple_request_with_high_initial_sequence_number_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_simple_request_with_high_initial_sequence_number_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_simple_request_with_high_initial_sequence_number_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_simple_request_with_high_initial_sequence_number.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_simple_request_with_high_initial_sequence_number_test: chttp2_socket_pair_simple_request_with_high_initial_sequence_number_test.exe
	echo Running chttp2_socket_pair_simple_request_with_high_initial_sequence_number_test
	$(OUT_DIR)\chttp2_socket_pair_simple_request_with_high_initial_sequence_number_test.exe
chttp2_socket_pair_one_byte_at_a_time_bad_hostname_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_bad_hostname_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_bad_hostname_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_bad_hostname.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_bad_hostname_test: chttp2_socket_pair_one_byte_at_a_time_bad_hostname_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_bad_hostname_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_bad_hostname_test.exe
chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_cancel_after_accept.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_test: chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_test.exe
chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_cancel_after_accept_and_writes_closed.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_test: chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_test.exe
chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_cancel_after_invoke.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_test: chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_test.exe
chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_cancel_before_invoke.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_test: chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_test.exe
chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_cancel_in_a_vacuum.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_test: chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_test.exe
chttp2_socket_pair_one_byte_at_a_time_census_simple_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_census_simple_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_census_simple_request_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_census_simple_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_census_simple_request_test: chttp2_socket_pair_one_byte_at_a_time_census_simple_request_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_census_simple_request_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_census_simple_request_test.exe
chttp2_socket_pair_one_byte_at_a_time_disappearing_server_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_disappearing_server_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_disappearing_server_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_disappearing_server.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_disappearing_server_test: chttp2_socket_pair_one_byte_at_a_time_disappearing_server_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_disappearing_server_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_disappearing_server_test.exe
chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_early_server_shutdown_finishes_inflight_calls.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_test: chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_test.exe
chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_early_server_shutdown_finishes_tags.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_test: chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_test.exe
chttp2_socket_pair_one_byte_at_a_time_empty_batch_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_empty_batch_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_empty_batch_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_empty_batch.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_empty_batch_test: chttp2_socket_pair_one_byte_at_a_time_empty_batch_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_empty_batch_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_empty_batch_test.exe
chttp2_socket_pair_one_byte_at_a_time_graceful_server_shutdown_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_graceful_server_shutdown_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_graceful_server_shutdown_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_graceful_server_shutdown.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_graceful_server_shutdown_test: chttp2_socket_pair_one_byte_at_a_time_graceful_server_shutdown_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_graceful_server_shutdown_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_graceful_server_shutdown_test.exe
chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_invoke_large_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_test: chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_test.exe
chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_max_concurrent_streams.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_test: chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_test.exe
chttp2_socket_pair_one_byte_at_a_time_max_message_length_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_max_message_length_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_max_message_length_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_max_message_length.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_max_message_length_test: chttp2_socket_pair_one_byte_at_a_time_max_message_length_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_max_message_length_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_max_message_length_test.exe
chttp2_socket_pair_one_byte_at_a_time_no_op_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_no_op_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_no_op_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_no_op.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_no_op_test: chttp2_socket_pair_one_byte_at_a_time_no_op_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_no_op_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_no_op_test.exe
chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_ping_pong_streaming.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_test: chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_test.exe
chttp2_socket_pair_one_byte_at_a_time_registered_call_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_registered_call_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_registered_call_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_registered_call.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_registered_call_test: chttp2_socket_pair_one_byte_at_a_time_registered_call_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_registered_call_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_registered_call_test.exe
chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_request_response_with_binary_metadata_and_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_test: chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_test.exe
chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_request_response_with_metadata_and_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_test: chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_test.exe
chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_request_response_with_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_test: chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_test.exe
chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_and_call_creds_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_and_call_creds_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_and_call_creds_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_request_response_with_payload_and_call_creds.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_and_call_creds_test: chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_and_call_creds_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_and_call_creds_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_and_call_creds_test.exe
chttp2_socket_pair_one_byte_at_a_time_request_response_with_trailing_metadata_and_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_request_response_with_trailing_metadata_and_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_response_with_trailing_metadata_and_payload_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_request_response_with_trailing_metadata_and_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_request_response_with_trailing_metadata_and_payload_test: chttp2_socket_pair_one_byte_at_a_time_request_response_with_trailing_metadata_and_payload_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_request_response_with_trailing_metadata_and_payload_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_response_with_trailing_metadata_and_payload_test.exe
chttp2_socket_pair_one_byte_at_a_time_request_with_large_metadata_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_request_with_large_metadata_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_with_large_metadata_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_request_with_large_metadata.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_request_with_large_metadata_test: chttp2_socket_pair_one_byte_at_a_time_request_with_large_metadata_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_request_with_large_metadata_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_with_large_metadata_test.exe
chttp2_socket_pair_one_byte_at_a_time_request_with_payload_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_request_with_payload_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_with_payload_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_request_with_payload.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_request_with_payload_test: chttp2_socket_pair_one_byte_at_a_time_request_with_payload_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_request_with_payload_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_with_payload_test.exe
chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_simple_delayed_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_test: chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_test.exe
chttp2_socket_pair_one_byte_at_a_time_simple_request_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_simple_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_simple_request_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_simple_request.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_simple_request_test: chttp2_socket_pair_one_byte_at_a_time_simple_request_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_simple_request_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_simple_request_test.exe
chttp2_socket_pair_one_byte_at_a_time_simple_request_with_high_initial_sequence_number_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_simple_request_with_high_initial_sequence_number_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_simple_request_with_high_initial_sequence_number_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_simple_request_with_high_initial_sequence_number.lib Debug\end2end_certs.lib Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_simple_request_with_high_initial_sequence_number_test: chttp2_socket_pair_one_byte_at_a_time_simple_request_with_high_initial_sequence_number_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_simple_request_with_high_initial_sequence_number_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_simple_request_with_high_initial_sequence_number_test.exe
chttp2_fullstack_bad_hostname_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_bad_hostname_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_bad_hostname_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_bad_hostname.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_bad_hostname_unsecure_test: chttp2_fullstack_bad_hostname_unsecure_test.exe
	echo Running chttp2_fullstack_bad_hostname_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_bad_hostname_unsecure_test.exe
chttp2_fullstack_cancel_after_accept_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_cancel_after_accept_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_cancel_after_accept_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_cancel_after_accept.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_cancel_after_accept_unsecure_test: chttp2_fullstack_cancel_after_accept_unsecure_test.exe
	echo Running chttp2_fullstack_cancel_after_accept_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_cancel_after_accept_unsecure_test.exe
chttp2_fullstack_cancel_after_accept_and_writes_closed_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_cancel_after_accept_and_writes_closed_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_cancel_after_accept_and_writes_closed_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_cancel_after_accept_and_writes_closed.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_cancel_after_accept_and_writes_closed_unsecure_test: chttp2_fullstack_cancel_after_accept_and_writes_closed_unsecure_test.exe
	echo Running chttp2_fullstack_cancel_after_accept_and_writes_closed_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_cancel_after_accept_and_writes_closed_unsecure_test.exe
chttp2_fullstack_cancel_after_invoke_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_cancel_after_invoke_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_cancel_after_invoke_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_cancel_after_invoke.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_cancel_after_invoke_unsecure_test: chttp2_fullstack_cancel_after_invoke_unsecure_test.exe
	echo Running chttp2_fullstack_cancel_after_invoke_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_cancel_after_invoke_unsecure_test.exe
chttp2_fullstack_cancel_before_invoke_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_cancel_before_invoke_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_cancel_before_invoke_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_cancel_before_invoke.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_cancel_before_invoke_unsecure_test: chttp2_fullstack_cancel_before_invoke_unsecure_test.exe
	echo Running chttp2_fullstack_cancel_before_invoke_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_cancel_before_invoke_unsecure_test.exe
chttp2_fullstack_cancel_in_a_vacuum_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_cancel_in_a_vacuum_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_cancel_in_a_vacuum_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_cancel_in_a_vacuum.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_cancel_in_a_vacuum_unsecure_test: chttp2_fullstack_cancel_in_a_vacuum_unsecure_test.exe
	echo Running chttp2_fullstack_cancel_in_a_vacuum_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_cancel_in_a_vacuum_unsecure_test.exe
chttp2_fullstack_census_simple_request_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_census_simple_request_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_census_simple_request_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_census_simple_request.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_census_simple_request_unsecure_test: chttp2_fullstack_census_simple_request_unsecure_test.exe
	echo Running chttp2_fullstack_census_simple_request_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_census_simple_request_unsecure_test.exe
chttp2_fullstack_disappearing_server_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_disappearing_server_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_disappearing_server_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_disappearing_server.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_disappearing_server_unsecure_test: chttp2_fullstack_disappearing_server_unsecure_test.exe
	echo Running chttp2_fullstack_disappearing_server_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_disappearing_server_unsecure_test.exe
chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_early_server_shutdown_finishes_inflight_calls.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_unsecure_test: chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_unsecure_test.exe
	echo Running chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_unsecure_test.exe
chttp2_fullstack_early_server_shutdown_finishes_tags_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_early_server_shutdown_finishes_tags_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_early_server_shutdown_finishes_tags_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_early_server_shutdown_finishes_tags.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_early_server_shutdown_finishes_tags_unsecure_test: chttp2_fullstack_early_server_shutdown_finishes_tags_unsecure_test.exe
	echo Running chttp2_fullstack_early_server_shutdown_finishes_tags_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_early_server_shutdown_finishes_tags_unsecure_test.exe
chttp2_fullstack_empty_batch_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_empty_batch_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_empty_batch_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_empty_batch.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_empty_batch_unsecure_test: chttp2_fullstack_empty_batch_unsecure_test.exe
	echo Running chttp2_fullstack_empty_batch_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_empty_batch_unsecure_test.exe
chttp2_fullstack_graceful_server_shutdown_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_graceful_server_shutdown_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_graceful_server_shutdown_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_graceful_server_shutdown.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_graceful_server_shutdown_unsecure_test: chttp2_fullstack_graceful_server_shutdown_unsecure_test.exe
	echo Running chttp2_fullstack_graceful_server_shutdown_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_graceful_server_shutdown_unsecure_test.exe
chttp2_fullstack_invoke_large_request_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_invoke_large_request_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_invoke_large_request_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_invoke_large_request.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_invoke_large_request_unsecure_test: chttp2_fullstack_invoke_large_request_unsecure_test.exe
	echo Running chttp2_fullstack_invoke_large_request_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_invoke_large_request_unsecure_test.exe
chttp2_fullstack_max_concurrent_streams_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_max_concurrent_streams_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_max_concurrent_streams_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_max_concurrent_streams.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_max_concurrent_streams_unsecure_test: chttp2_fullstack_max_concurrent_streams_unsecure_test.exe
	echo Running chttp2_fullstack_max_concurrent_streams_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_max_concurrent_streams_unsecure_test.exe
chttp2_fullstack_max_message_length_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_max_message_length_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_max_message_length_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_max_message_length.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_max_message_length_unsecure_test: chttp2_fullstack_max_message_length_unsecure_test.exe
	echo Running chttp2_fullstack_max_message_length_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_max_message_length_unsecure_test.exe
chttp2_fullstack_no_op_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_no_op_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_no_op_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_no_op.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_no_op_unsecure_test: chttp2_fullstack_no_op_unsecure_test.exe
	echo Running chttp2_fullstack_no_op_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_no_op_unsecure_test.exe
chttp2_fullstack_ping_pong_streaming_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_ping_pong_streaming_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_ping_pong_streaming_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_ping_pong_streaming.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_ping_pong_streaming_unsecure_test: chttp2_fullstack_ping_pong_streaming_unsecure_test.exe
	echo Running chttp2_fullstack_ping_pong_streaming_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_ping_pong_streaming_unsecure_test.exe
chttp2_fullstack_registered_call_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_registered_call_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_registered_call_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_registered_call.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_registered_call_unsecure_test: chttp2_fullstack_registered_call_unsecure_test.exe
	echo Running chttp2_fullstack_registered_call_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_registered_call_unsecure_test.exe
chttp2_fullstack_request_response_with_binary_metadata_and_payload_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_request_response_with_binary_metadata_and_payload_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_request_response_with_binary_metadata_and_payload_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_request_response_with_binary_metadata_and_payload.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_request_response_with_binary_metadata_and_payload_unsecure_test: chttp2_fullstack_request_response_with_binary_metadata_and_payload_unsecure_test.exe
	echo Running chttp2_fullstack_request_response_with_binary_metadata_and_payload_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_request_response_with_binary_metadata_and_payload_unsecure_test.exe
chttp2_fullstack_request_response_with_metadata_and_payload_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_request_response_with_metadata_and_payload_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_request_response_with_metadata_and_payload_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_request_response_with_metadata_and_payload.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_request_response_with_metadata_and_payload_unsecure_test: chttp2_fullstack_request_response_with_metadata_and_payload_unsecure_test.exe
	echo Running chttp2_fullstack_request_response_with_metadata_and_payload_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_request_response_with_metadata_and_payload_unsecure_test.exe
chttp2_fullstack_request_response_with_payload_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_request_response_with_payload_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_request_response_with_payload_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_request_response_with_payload.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_request_response_with_payload_unsecure_test: chttp2_fullstack_request_response_with_payload_unsecure_test.exe
	echo Running chttp2_fullstack_request_response_with_payload_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_request_response_with_payload_unsecure_test.exe
chttp2_fullstack_request_response_with_trailing_metadata_and_payload_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_request_response_with_trailing_metadata_and_payload_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_request_response_with_trailing_metadata_and_payload_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_request_response_with_trailing_metadata_and_payload.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_request_response_with_trailing_metadata_and_payload_unsecure_test: chttp2_fullstack_request_response_with_trailing_metadata_and_payload_unsecure_test.exe
	echo Running chttp2_fullstack_request_response_with_trailing_metadata_and_payload_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_request_response_with_trailing_metadata_and_payload_unsecure_test.exe
chttp2_fullstack_request_with_large_metadata_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_request_with_large_metadata_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_request_with_large_metadata_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_request_with_large_metadata.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_request_with_large_metadata_unsecure_test: chttp2_fullstack_request_with_large_metadata_unsecure_test.exe
	echo Running chttp2_fullstack_request_with_large_metadata_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_request_with_large_metadata_unsecure_test.exe
chttp2_fullstack_request_with_payload_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_request_with_payload_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_request_with_payload_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_request_with_payload.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_request_with_payload_unsecure_test: chttp2_fullstack_request_with_payload_unsecure_test.exe
	echo Running chttp2_fullstack_request_with_payload_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_request_with_payload_unsecure_test.exe
chttp2_fullstack_simple_delayed_request_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_simple_delayed_request_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_simple_delayed_request_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_simple_delayed_request.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_simple_delayed_request_unsecure_test: chttp2_fullstack_simple_delayed_request_unsecure_test.exe
	echo Running chttp2_fullstack_simple_delayed_request_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_simple_delayed_request_unsecure_test.exe
chttp2_fullstack_simple_request_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_simple_request_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_simple_request_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_simple_request.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_simple_request_unsecure_test: chttp2_fullstack_simple_request_unsecure_test.exe
	echo Running chttp2_fullstack_simple_request_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_simple_request_unsecure_test.exe
chttp2_fullstack_simple_request_with_high_initial_sequence_number_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_fullstack_simple_request_with_high_initial_sequence_number_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_fullstack_simple_request_with_high_initial_sequence_number_unsecure_test.exe" Debug\end2end_fixture_chttp2_fullstack.lib Debug\end2end_test_simple_request_with_high_initial_sequence_number.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_fullstack_simple_request_with_high_initial_sequence_number_unsecure_test: chttp2_fullstack_simple_request_with_high_initial_sequence_number_unsecure_test.exe
	echo Running chttp2_fullstack_simple_request_with_high_initial_sequence_number_unsecure_test
	$(OUT_DIR)\chttp2_fullstack_simple_request_with_high_initial_sequence_number_unsecure_test.exe
chttp2_socket_pair_bad_hostname_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_bad_hostname_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_bad_hostname_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_bad_hostname.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_bad_hostname_unsecure_test: chttp2_socket_pair_bad_hostname_unsecure_test.exe
	echo Running chttp2_socket_pair_bad_hostname_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_bad_hostname_unsecure_test.exe
chttp2_socket_pair_cancel_after_accept_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_cancel_after_accept_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_cancel_after_accept_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_cancel_after_accept.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_cancel_after_accept_unsecure_test: chttp2_socket_pair_cancel_after_accept_unsecure_test.exe
	echo Running chttp2_socket_pair_cancel_after_accept_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_cancel_after_accept_unsecure_test.exe
chttp2_socket_pair_cancel_after_accept_and_writes_closed_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_cancel_after_accept_and_writes_closed_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_cancel_after_accept_and_writes_closed_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_cancel_after_accept_and_writes_closed.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_cancel_after_accept_and_writes_closed_unsecure_test: chttp2_socket_pair_cancel_after_accept_and_writes_closed_unsecure_test.exe
	echo Running chttp2_socket_pair_cancel_after_accept_and_writes_closed_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_cancel_after_accept_and_writes_closed_unsecure_test.exe
chttp2_socket_pair_cancel_after_invoke_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_cancel_after_invoke_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_cancel_after_invoke_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_cancel_after_invoke.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_cancel_after_invoke_unsecure_test: chttp2_socket_pair_cancel_after_invoke_unsecure_test.exe
	echo Running chttp2_socket_pair_cancel_after_invoke_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_cancel_after_invoke_unsecure_test.exe
chttp2_socket_pair_cancel_before_invoke_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_cancel_before_invoke_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_cancel_before_invoke_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_cancel_before_invoke.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_cancel_before_invoke_unsecure_test: chttp2_socket_pair_cancel_before_invoke_unsecure_test.exe
	echo Running chttp2_socket_pair_cancel_before_invoke_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_cancel_before_invoke_unsecure_test.exe
chttp2_socket_pair_cancel_in_a_vacuum_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_cancel_in_a_vacuum_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_cancel_in_a_vacuum_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_cancel_in_a_vacuum.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_cancel_in_a_vacuum_unsecure_test: chttp2_socket_pair_cancel_in_a_vacuum_unsecure_test.exe
	echo Running chttp2_socket_pair_cancel_in_a_vacuum_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_cancel_in_a_vacuum_unsecure_test.exe
chttp2_socket_pair_census_simple_request_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_census_simple_request_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_census_simple_request_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_census_simple_request.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_census_simple_request_unsecure_test: chttp2_socket_pair_census_simple_request_unsecure_test.exe
	echo Running chttp2_socket_pair_census_simple_request_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_census_simple_request_unsecure_test.exe
chttp2_socket_pair_disappearing_server_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_disappearing_server_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_disappearing_server_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_disappearing_server.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_disappearing_server_unsecure_test: chttp2_socket_pair_disappearing_server_unsecure_test.exe
	echo Running chttp2_socket_pair_disappearing_server_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_disappearing_server_unsecure_test.exe
chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_early_server_shutdown_finishes_inflight_calls.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_unsecure_test: chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_unsecure_test.exe
	echo Running chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_unsecure_test.exe
chttp2_socket_pair_early_server_shutdown_finishes_tags_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_early_server_shutdown_finishes_tags_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_early_server_shutdown_finishes_tags_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_early_server_shutdown_finishes_tags.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_early_server_shutdown_finishes_tags_unsecure_test: chttp2_socket_pair_early_server_shutdown_finishes_tags_unsecure_test.exe
	echo Running chttp2_socket_pair_early_server_shutdown_finishes_tags_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_early_server_shutdown_finishes_tags_unsecure_test.exe
chttp2_socket_pair_empty_batch_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_empty_batch_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_empty_batch_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_empty_batch.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_empty_batch_unsecure_test: chttp2_socket_pair_empty_batch_unsecure_test.exe
	echo Running chttp2_socket_pair_empty_batch_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_empty_batch_unsecure_test.exe
chttp2_socket_pair_graceful_server_shutdown_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_graceful_server_shutdown_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_graceful_server_shutdown_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_graceful_server_shutdown.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_graceful_server_shutdown_unsecure_test: chttp2_socket_pair_graceful_server_shutdown_unsecure_test.exe
	echo Running chttp2_socket_pair_graceful_server_shutdown_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_graceful_server_shutdown_unsecure_test.exe
chttp2_socket_pair_invoke_large_request_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_invoke_large_request_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_invoke_large_request_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_invoke_large_request.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_invoke_large_request_unsecure_test: chttp2_socket_pair_invoke_large_request_unsecure_test.exe
	echo Running chttp2_socket_pair_invoke_large_request_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_invoke_large_request_unsecure_test.exe
chttp2_socket_pair_max_concurrent_streams_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_max_concurrent_streams_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_max_concurrent_streams_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_max_concurrent_streams.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_max_concurrent_streams_unsecure_test: chttp2_socket_pair_max_concurrent_streams_unsecure_test.exe
	echo Running chttp2_socket_pair_max_concurrent_streams_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_max_concurrent_streams_unsecure_test.exe
chttp2_socket_pair_max_message_length_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_max_message_length_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_max_message_length_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_max_message_length.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_max_message_length_unsecure_test: chttp2_socket_pair_max_message_length_unsecure_test.exe
	echo Running chttp2_socket_pair_max_message_length_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_max_message_length_unsecure_test.exe
chttp2_socket_pair_no_op_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_no_op_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_no_op_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_no_op.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_no_op_unsecure_test: chttp2_socket_pair_no_op_unsecure_test.exe
	echo Running chttp2_socket_pair_no_op_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_no_op_unsecure_test.exe
chttp2_socket_pair_ping_pong_streaming_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_ping_pong_streaming_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_ping_pong_streaming_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_ping_pong_streaming.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_ping_pong_streaming_unsecure_test: chttp2_socket_pair_ping_pong_streaming_unsecure_test.exe
	echo Running chttp2_socket_pair_ping_pong_streaming_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_ping_pong_streaming_unsecure_test.exe
chttp2_socket_pair_registered_call_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_registered_call_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_registered_call_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_registered_call.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_registered_call_unsecure_test: chttp2_socket_pair_registered_call_unsecure_test.exe
	echo Running chttp2_socket_pair_registered_call_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_registered_call_unsecure_test.exe
chttp2_socket_pair_request_response_with_binary_metadata_and_payload_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_request_response_with_binary_metadata_and_payload_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_request_response_with_binary_metadata_and_payload_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_request_response_with_binary_metadata_and_payload.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_request_response_with_binary_metadata_and_payload_unsecure_test: chttp2_socket_pair_request_response_with_binary_metadata_and_payload_unsecure_test.exe
	echo Running chttp2_socket_pair_request_response_with_binary_metadata_and_payload_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_request_response_with_binary_metadata_and_payload_unsecure_test.exe
chttp2_socket_pair_request_response_with_metadata_and_payload_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_request_response_with_metadata_and_payload_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_request_response_with_metadata_and_payload_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_request_response_with_metadata_and_payload.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_request_response_with_metadata_and_payload_unsecure_test: chttp2_socket_pair_request_response_with_metadata_and_payload_unsecure_test.exe
	echo Running chttp2_socket_pair_request_response_with_metadata_and_payload_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_request_response_with_metadata_and_payload_unsecure_test.exe
chttp2_socket_pair_request_response_with_payload_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_request_response_with_payload_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_request_response_with_payload_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_request_response_with_payload.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_request_response_with_payload_unsecure_test: chttp2_socket_pair_request_response_with_payload_unsecure_test.exe
	echo Running chttp2_socket_pair_request_response_with_payload_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_request_response_with_payload_unsecure_test.exe
chttp2_socket_pair_request_response_with_trailing_metadata_and_payload_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_request_response_with_trailing_metadata_and_payload_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_request_response_with_trailing_metadata_and_payload_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_request_response_with_trailing_metadata_and_payload.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_request_response_with_trailing_metadata_and_payload_unsecure_test: chttp2_socket_pair_request_response_with_trailing_metadata_and_payload_unsecure_test.exe
	echo Running chttp2_socket_pair_request_response_with_trailing_metadata_and_payload_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_request_response_with_trailing_metadata_and_payload_unsecure_test.exe
chttp2_socket_pair_request_with_large_metadata_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_request_with_large_metadata_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_request_with_large_metadata_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_request_with_large_metadata.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_request_with_large_metadata_unsecure_test: chttp2_socket_pair_request_with_large_metadata_unsecure_test.exe
	echo Running chttp2_socket_pair_request_with_large_metadata_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_request_with_large_metadata_unsecure_test.exe
chttp2_socket_pair_request_with_payload_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_request_with_payload_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_request_with_payload_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_request_with_payload.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_request_with_payload_unsecure_test: chttp2_socket_pair_request_with_payload_unsecure_test.exe
	echo Running chttp2_socket_pair_request_with_payload_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_request_with_payload_unsecure_test.exe
chttp2_socket_pair_simple_delayed_request_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_simple_delayed_request_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_simple_delayed_request_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_simple_delayed_request.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_simple_delayed_request_unsecure_test: chttp2_socket_pair_simple_delayed_request_unsecure_test.exe
	echo Running chttp2_socket_pair_simple_delayed_request_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_simple_delayed_request_unsecure_test.exe
chttp2_socket_pair_simple_request_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_simple_request_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_simple_request_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_simple_request.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_simple_request_unsecure_test: chttp2_socket_pair_simple_request_unsecure_test.exe
	echo Running chttp2_socket_pair_simple_request_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_simple_request_unsecure_test.exe
chttp2_socket_pair_simple_request_with_high_initial_sequence_number_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_simple_request_with_high_initial_sequence_number_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_simple_request_with_high_initial_sequence_number_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair.lib Debug\end2end_test_simple_request_with_high_initial_sequence_number.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_simple_request_with_high_initial_sequence_number_unsecure_test: chttp2_socket_pair_simple_request_with_high_initial_sequence_number_unsecure_test.exe
	echo Running chttp2_socket_pair_simple_request_with_high_initial_sequence_number_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_simple_request_with_high_initial_sequence_number_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_bad_hostname_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_bad_hostname_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_bad_hostname_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_bad_hostname.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_bad_hostname_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_bad_hostname_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_bad_hostname_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_bad_hostname_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_cancel_after_accept.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_cancel_after_accept_and_writes_closed.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_cancel_after_invoke.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_cancel_before_invoke.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_cancel_in_a_vacuum.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_census_simple_request_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_census_simple_request_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_census_simple_request_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_census_simple_request.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_census_simple_request_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_census_simple_request_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_census_simple_request_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_census_simple_request_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_disappearing_server_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_disappearing_server_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_disappearing_server_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_disappearing_server.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_disappearing_server_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_disappearing_server_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_disappearing_server_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_disappearing_server_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_early_server_shutdown_finishes_inflight_calls.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_early_server_shutdown_finishes_tags.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_empty_batch_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_empty_batch_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_empty_batch_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_empty_batch.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_empty_batch_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_empty_batch_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_empty_batch_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_empty_batch_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_graceful_server_shutdown_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_graceful_server_shutdown_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_graceful_server_shutdown_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_graceful_server_shutdown.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_graceful_server_shutdown_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_graceful_server_shutdown_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_graceful_server_shutdown_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_graceful_server_shutdown_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_invoke_large_request.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_max_concurrent_streams.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_max_message_length_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_max_message_length_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_max_message_length_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_max_message_length.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_max_message_length_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_max_message_length_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_max_message_length_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_max_message_length_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_no_op_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_no_op_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_no_op_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_no_op.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_no_op_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_no_op_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_no_op_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_no_op_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_ping_pong_streaming.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_registered_call_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_registered_call_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_registered_call_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_registered_call.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_registered_call_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_registered_call_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_registered_call_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_registered_call_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_request_response_with_binary_metadata_and_payload.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_request_response_with_metadata_and_payload.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_request_response_with_payload.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_request_response_with_trailing_metadata_and_payload_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_request_response_with_trailing_metadata_and_payload_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_response_with_trailing_metadata_and_payload_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_request_response_with_trailing_metadata_and_payload.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_request_response_with_trailing_metadata_and_payload_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_request_response_with_trailing_metadata_and_payload_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_request_response_with_trailing_metadata_and_payload_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_response_with_trailing_metadata_and_payload_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_request_with_large_metadata_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_request_with_large_metadata_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_with_large_metadata_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_request_with_large_metadata.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_request_with_large_metadata_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_request_with_large_metadata_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_request_with_large_metadata_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_with_large_metadata_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_request_with_payload_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_request_with_payload_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_with_payload_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_request_with_payload.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_request_with_payload_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_request_with_payload_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_request_with_payload_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_request_with_payload_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_simple_delayed_request.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_simple_request_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_simple_request_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_simple_request_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_simple_request.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_simple_request_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_simple_request_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_simple_request_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_simple_request_unsecure_test.exe
chttp2_socket_pair_one_byte_at_a_time_simple_request_with_high_initial_sequence_number_unsecure_test.exe: build_libs $(OUT_DIR)
	echo Building chttp2_socket_pair_one_byte_at_a_time_simple_request_with_high_initial_sequence_number_unsecure_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\vsprojects\dummy.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_simple_request_with_high_initial_sequence_number_unsecure_test.exe" Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib Debug\end2end_test_simple_request_with_high_initial_sequence_number.lib Debug\grpc_test_util_unsecure.lib Debug\grpc_unsecure.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dummy.obj 
chttp2_socket_pair_one_byte_at_a_time_simple_request_with_high_initial_sequence_number_unsecure_test: chttp2_socket_pair_one_byte_at_a_time_simple_request_with_high_initial_sequence_number_unsecure_test.exe
	echo Running chttp2_socket_pair_one_byte_at_a_time_simple_request_with_high_initial_sequence_number_unsecure_test
	$(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time_simple_request_with_high_initial_sequence_number_unsecure_test.exe
build_gpr:
	msbuild grpc.sln /t:gpr /p:Configuration=Debug /p:Linkage-grpc_dependencies_zlib=static
build_gpr_test_util:
	msbuild grpc.sln /t:gpr_test_util /p:Configuration=Debug /p:Linkage-grpc_dependencies_zlib=static
build_grpc:
	msbuild grpc.sln /t:grpc /p:Configuration=Debug /p:Linkage-grpc_dependencies_zlib=static
build_grpc_test_util:
	msbuild grpc.sln /t:grpc_test_util /p:Configuration=Debug /p:Linkage-grpc_dependencies_zlib=static
build_grpc_test_util_unsecure:
	msbuild grpc.sln /t:grpc_test_util_unsecure /p:Configuration=Debug /p:Linkage-grpc_dependencies_zlib=static
build_grpc_unsecure:
	msbuild grpc.sln /t:grpc_unsecure /p:Configuration=Debug /p:Linkage-grpc_dependencies_zlib=static
Debug\end2end_fixture_chttp2_fake_security.lib: $(OUT_DIR)
	echo Building end2end_fixture_chttp2_fake_security
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\fixtures\chttp2_fake_security.c 
	$(LIBTOOL) /OUT:"Debug\end2end_fixture_chttp2_fake_security.lib" $(OUT_DIR)\chttp2_fake_security.obj 
Debug\end2end_fixture_chttp2_fullstack.lib: $(OUT_DIR)
	echo Building end2end_fixture_chttp2_fullstack
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\fixtures\chttp2_fullstack.c 
	$(LIBTOOL) /OUT:"Debug\end2end_fixture_chttp2_fullstack.lib" $(OUT_DIR)\chttp2_fullstack.obj 
Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib: $(OUT_DIR)
	echo Building end2end_fixture_chttp2_simple_ssl_fullstack
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\fixtures\chttp2_simple_ssl_fullstack.c 
	$(LIBTOOL) /OUT:"Debug\end2end_fixture_chttp2_simple_ssl_fullstack.lib" $(OUT_DIR)\chttp2_simple_ssl_fullstack.obj 
Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib: $(OUT_DIR)
	echo Building end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\fixtures\chttp2_simple_ssl_with_oauth2_fullstack.c 
	$(LIBTOOL) /OUT:"Debug\end2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.lib" $(OUT_DIR)\chttp2_simple_ssl_with_oauth2_fullstack.obj 
Debug\end2end_fixture_chttp2_socket_pair.lib: $(OUT_DIR)
	echo Building end2end_fixture_chttp2_socket_pair
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\fixtures\chttp2_socket_pair.c 
	$(LIBTOOL) /OUT:"Debug\end2end_fixture_chttp2_socket_pair.lib" $(OUT_DIR)\chttp2_socket_pair.obj 
Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib: $(OUT_DIR)
	echo Building end2end_fixture_chttp2_socket_pair_one_byte_at_a_time
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\fixtures\chttp2_socket_pair_one_byte_at_a_time.c 
	$(LIBTOOL) /OUT:"Debug\end2end_fixture_chttp2_socket_pair_one_byte_at_a_time.lib" $(OUT_DIR)\chttp2_socket_pair_one_byte_at_a_time.obj 
Debug\end2end_test_bad_hostname.lib: $(OUT_DIR)
	echo Building end2end_test_bad_hostname
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\bad_hostname.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_bad_hostname.lib" $(OUT_DIR)\bad_hostname.obj 
Debug\end2end_test_cancel_after_accept.lib: $(OUT_DIR)
	echo Building end2end_test_cancel_after_accept
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\cancel_after_accept.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_cancel_after_accept.lib" $(OUT_DIR)\cancel_after_accept.obj 
Debug\end2end_test_cancel_after_accept_and_writes_closed.lib: $(OUT_DIR)
	echo Building end2end_test_cancel_after_accept_and_writes_closed
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\cancel_after_accept_and_writes_closed.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_cancel_after_accept_and_writes_closed.lib" $(OUT_DIR)\cancel_after_accept_and_writes_closed.obj 
Debug\end2end_test_cancel_after_invoke.lib: $(OUT_DIR)
	echo Building end2end_test_cancel_after_invoke
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\cancel_after_invoke.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_cancel_after_invoke.lib" $(OUT_DIR)\cancel_after_invoke.obj 
Debug\end2end_test_cancel_before_invoke.lib: $(OUT_DIR)
	echo Building end2end_test_cancel_before_invoke
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\cancel_before_invoke.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_cancel_before_invoke.lib" $(OUT_DIR)\cancel_before_invoke.obj 
Debug\end2end_test_cancel_in_a_vacuum.lib: $(OUT_DIR)
	echo Building end2end_test_cancel_in_a_vacuum
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\cancel_in_a_vacuum.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_cancel_in_a_vacuum.lib" $(OUT_DIR)\cancel_in_a_vacuum.obj 
Debug\end2end_test_census_simple_request.lib: $(OUT_DIR)
	echo Building end2end_test_census_simple_request
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\census_simple_request.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_census_simple_request.lib" $(OUT_DIR)\census_simple_request.obj 
Debug\end2end_test_disappearing_server.lib: $(OUT_DIR)
	echo Building end2end_test_disappearing_server
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\disappearing_server.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_disappearing_server.lib" $(OUT_DIR)\disappearing_server.obj 
Debug\end2end_test_early_server_shutdown_finishes_inflight_calls.lib: $(OUT_DIR)
	echo Building end2end_test_early_server_shutdown_finishes_inflight_calls
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\early_server_shutdown_finishes_inflight_calls.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_early_server_shutdown_finishes_inflight_calls.lib" $(OUT_DIR)\early_server_shutdown_finishes_inflight_calls.obj 
Debug\end2end_test_early_server_shutdown_finishes_tags.lib: $(OUT_DIR)
	echo Building end2end_test_early_server_shutdown_finishes_tags
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\early_server_shutdown_finishes_tags.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_early_server_shutdown_finishes_tags.lib" $(OUT_DIR)\early_server_shutdown_finishes_tags.obj 
Debug\end2end_test_empty_batch.lib: $(OUT_DIR)
	echo Building end2end_test_empty_batch
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\empty_batch.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_empty_batch.lib" $(OUT_DIR)\empty_batch.obj 
Debug\end2end_test_graceful_server_shutdown.lib: $(OUT_DIR)
	echo Building end2end_test_graceful_server_shutdown
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\graceful_server_shutdown.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_graceful_server_shutdown.lib" $(OUT_DIR)\graceful_server_shutdown.obj 
Debug\end2end_test_invoke_large_request.lib: $(OUT_DIR)
	echo Building end2end_test_invoke_large_request
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\invoke_large_request.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_invoke_large_request.lib" $(OUT_DIR)\invoke_large_request.obj 
Debug\end2end_test_max_concurrent_streams.lib: $(OUT_DIR)
	echo Building end2end_test_max_concurrent_streams
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\max_concurrent_streams.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_max_concurrent_streams.lib" $(OUT_DIR)\max_concurrent_streams.obj 
Debug\end2end_test_max_message_length.lib: $(OUT_DIR)
	echo Building end2end_test_max_message_length
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\max_message_length.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_max_message_length.lib" $(OUT_DIR)\max_message_length.obj 
Debug\end2end_test_no_op.lib: $(OUT_DIR)
	echo Building end2end_test_no_op
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\no_op.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_no_op.lib" $(OUT_DIR)\no_op.obj 
Debug\end2end_test_ping_pong_streaming.lib: $(OUT_DIR)
	echo Building end2end_test_ping_pong_streaming
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\ping_pong_streaming.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_ping_pong_streaming.lib" $(OUT_DIR)\ping_pong_streaming.obj 
Debug\end2end_test_registered_call.lib: $(OUT_DIR)
	echo Building end2end_test_registered_call
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\registered_call.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_registered_call.lib" $(OUT_DIR)\registered_call.obj 
Debug\end2end_test_request_response_with_binary_metadata_and_payload.lib: $(OUT_DIR)
	echo Building end2end_test_request_response_with_binary_metadata_and_payload
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\request_response_with_binary_metadata_and_payload.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_request_response_with_binary_metadata_and_payload.lib" $(OUT_DIR)\request_response_with_binary_metadata_and_payload.obj 
Debug\end2end_test_request_response_with_metadata_and_payload.lib: $(OUT_DIR)
	echo Building end2end_test_request_response_with_metadata_and_payload
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\request_response_with_metadata_and_payload.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_request_response_with_metadata_and_payload.lib" $(OUT_DIR)\request_response_with_metadata_and_payload.obj 
Debug\end2end_test_request_response_with_payload.lib: $(OUT_DIR)
	echo Building end2end_test_request_response_with_payload
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\request_response_with_payload.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_request_response_with_payload.lib" $(OUT_DIR)\request_response_with_payload.obj 
Debug\end2end_test_request_response_with_payload_and_call_creds.lib: $(OUT_DIR)
	echo Building end2end_test_request_response_with_payload_and_call_creds
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\request_response_with_payload_and_call_creds.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_request_response_with_payload_and_call_creds.lib" $(OUT_DIR)\request_response_with_payload_and_call_creds.obj 
Debug\end2end_test_request_response_with_trailing_metadata_and_payload.lib: $(OUT_DIR)
	echo Building end2end_test_request_response_with_trailing_metadata_and_payload
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\request_response_with_trailing_metadata_and_payload.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_request_response_with_trailing_metadata_and_payload.lib" $(OUT_DIR)\request_response_with_trailing_metadata_and_payload.obj 
Debug\end2end_test_request_with_large_metadata.lib: $(OUT_DIR)
	echo Building end2end_test_request_with_large_metadata
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\request_with_large_metadata.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_request_with_large_metadata.lib" $(OUT_DIR)\request_with_large_metadata.obj 
Debug\end2end_test_request_with_payload.lib: $(OUT_DIR)
	echo Building end2end_test_request_with_payload
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\request_with_payload.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_request_with_payload.lib" $(OUT_DIR)\request_with_payload.obj 
Debug\end2end_test_simple_delayed_request.lib: $(OUT_DIR)
	echo Building end2end_test_simple_delayed_request
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\simple_delayed_request.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_simple_delayed_request.lib" $(OUT_DIR)\simple_delayed_request.obj 
Debug\end2end_test_simple_request.lib: $(OUT_DIR)
	echo Building end2end_test_simple_request
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\simple_request.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_simple_request.lib" $(OUT_DIR)\simple_request.obj 
Debug\end2end_test_simple_request_with_high_initial_sequence_number.lib: $(OUT_DIR)
	echo Building end2end_test_simple_request_with_high_initial_sequence_number
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\tests\simple_request_with_high_initial_sequence_number.c 
	$(LIBTOOL) /OUT:"Debug\end2end_test_simple_request_with_high_initial_sequence_number.lib" $(OUT_DIR)\simple_request_with_high_initial_sequence_number.obj 
Debug\end2end_certs.lib: $(OUT_DIR)
	echo Building end2end_certs
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ $(REPO_ROOT)\test\core\end2end\data\test_root_cert.c $(REPO_ROOT)\test\core\end2end\data\server1_cert.c $(REPO_ROOT)\test\core\end2end\data\server1_key.c 
	$(LIBTOOL) /OUT:"Debug\end2end_certs.lib" $(OUT_DIR)\test_root_cert.obj $(OUT_DIR)\server1_cert.obj $(OUT_DIR)\server1_key.obj 
