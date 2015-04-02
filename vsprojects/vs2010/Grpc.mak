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

CC=cl.exe
LINK=link.exe

INCLUDES=/I..\.. /I..\..\include /I..\..\third_party\zlib /I..\third_party /I..\..\third_party\openssl\inc32
DEFINES=/D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /D _CRT_SECURE_NO_WARNINGS
CFLAGS=/c $(INCLUDES) /nologo /Z7 /W3 /WX- /sdl $(DEFINES) /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze-
LFLAGS=/DEBUG /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86

OPENSSL_LIBS=..\..\third_party\openssl\out32\ssleay32.lib ..\..\third_party\openssl\out32\libeay32.lib
WINSOCK_LIBS=ws2_32.lib
ZLIB_LIBS=Debug\zlibwapi.lib
LIBS=$(OPENSSL_LIBS) $(WINSOCK_LIBS) $(ZLIB_LIBS)

gpr_test_util:
	MSBuild.exe gpr_test_util.vcxproj /p:Configuration=Debug

grpc_test_util:
	MSBuild.exe grpc_test_util.vcxproj /p:Configuration=Debug

$(OUT_DIR):
	mkdir $(OUT_DIR)

buildtests: alarm_heap_test.exe alarm_list_test.exe alarm_test.exe alpn_test.exe bin_encoder_test.exe census_hash_table_test.exe census_statistics_multiple_writers_circular_buffer_test.exe census_statistics_multiple_writers_test.exe census_statistics_performance_test.exe census_statistics_quick_test.exe census_statistics_small_log_test.exe census_stats_store_test.exe census_stub_test.exe census_trace_store_test.exe census_window_stats_test.exe chttp2_status_conversion_test.exe chttp2_stream_encoder_test.exe chttp2_stream_map_test.exe chttp2_transport_end2end_test.exe dualstack_socket_test.exe echo_test.exe fd_posix_test.exe fling_stream_test.exe fling_test.exe gpr_cancellable_test.exe gpr_cmdline_test.exe gpr_env_test.exe gpr_file_test.exe gpr_histogram_test.exe gpr_host_port_test.exe gpr_log_test.exe gpr_slice_buffer_test.exe gpr_slice_test.exe gpr_string_test.exe gpr_sync_test.exe gpr_thd_test.exe gpr_time_test.exe gpr_useful_test.exe grpc_base64_test.exe grpc_byte_buffer_reader_test.exe grpc_channel_stack_test.exe grpc_completion_queue_test.exe grpc_credentials_test.exe grpc_json_token_test.exe grpc_stream_op_test.exe hpack_parser_test.exe hpack_table_test.exe httpcli_format_request_test.exe httpcli_parser_test.exe httpcli_test.exe json_rewrite_test.exe json_test.exe lame_client_test.exe message_compress_test.exe metadata_buffer_test.exe multi_init_test.exe murmur_hash_test.exe no_server_test.exe poll_kick_posix_test.exe resolve_address_test.exe secure_endpoint_test.exe sockaddr_utils_test.exe tcp_client_posix_test.exe tcp_posix_test.exe tcp_server_posix_test.exe time_averaged_stats_test.exe time_test.exe timeout_encoding_test.exe transport_metadata_test.exe transport_security_test.exe 
	echo All tests built.

test: alarm_heap_test alarm_list_test alarm_test alpn_test bin_encoder_test census_hash_table_test census_statistics_multiple_writers_circular_buffer_test census_statistics_multiple_writers_test census_statistics_performance_test census_statistics_quick_test census_statistics_small_log_test census_stats_store_test census_stub_test census_trace_store_test census_window_stats_test chttp2_status_conversion_test chttp2_stream_encoder_test chttp2_stream_map_test chttp2_transport_end2end_test dualstack_socket_test echo_test fd_posix_test fling_stream_test fling_test gpr_cancellable_test gpr_cmdline_test gpr_env_test gpr_file_test gpr_histogram_test gpr_host_port_test gpr_log_test gpr_slice_buffer_test gpr_slice_test gpr_string_test gpr_sync_test gpr_thd_test gpr_time_test gpr_useful_test grpc_base64_test grpc_byte_buffer_reader_test grpc_channel_stack_test grpc_completion_queue_test grpc_credentials_test grpc_json_token_test grpc_stream_op_test hpack_parser_test hpack_table_test httpcli_format_request_test httpcli_parser_test httpcli_test json_rewrite_test json_test lame_client_test message_compress_test metadata_buffer_test multi_init_test murmur_hash_test no_server_test poll_kick_posix_test resolve_address_test secure_endpoint_test sockaddr_utils_test tcp_client_posix_test tcp_posix_test tcp_server_posix_test time_averaged_stats_test time_test timeout_encoding_test transport_metadata_test transport_security_test 
	echo All tests ran.

test_gpr: gpr_cancellable_test gpr_cmdline_test gpr_env_test gpr_file_test gpr_histogram_test gpr_host_port_test gpr_log_test gpr_slice_buffer_test gpr_slice_test gpr_string_test gpr_sync_test gpr_thd_test gpr_time_test gpr_useful_test 
	echo All tests ran.

alarm_heap_test.exe: grpc_test_util
	echo Building alarm_heap_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\iomgr\alarm_heap_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\alarm_heap_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\alarm_heap_test.obj 
alarm_heap_test: alarm_heap_test.exe
	echo Running alarm_heap_test
	$(OUT_DIR)\alarm_heap_test.exe

alarm_list_test.exe: grpc_test_util
	echo Building alarm_list_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\iomgr\alarm_list_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\alarm_list_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\alarm_list_test.obj 
alarm_list_test: alarm_list_test.exe
	echo Running alarm_list_test
	$(OUT_DIR)\alarm_list_test.exe

alarm_test.exe: grpc_test_util
	echo Building alarm_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\iomgr\alarm_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\alarm_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\alarm_test.obj 
alarm_test: alarm_test.exe
	echo Running alarm_test
	$(OUT_DIR)\alarm_test.exe

alpn_test.exe: grpc_test_util
	echo Building alpn_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\transport\chttp2\alpn_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\alpn_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\alpn_test.obj 
alpn_test: alpn_test.exe
	echo Running alpn_test
	$(OUT_DIR)\alpn_test.exe

bin_encoder_test.exe: grpc_test_util
	echo Building bin_encoder_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\transport\chttp2\bin_encoder_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\bin_encoder_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\bin_encoder_test.obj 
bin_encoder_test: bin_encoder_test.exe
	echo Running bin_encoder_test
	$(OUT_DIR)\bin_encoder_test.exe

census_hash_table_test.exe: grpc_test_util
	echo Building census_hash_table_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\statistics\hash_table_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\census_hash_table_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\hash_table_test.obj 
census_hash_table_test: census_hash_table_test.exe
	echo Running census_hash_table_test
	$(OUT_DIR)\census_hash_table_test.exe

census_statistics_multiple_writers_circular_buffer_test.exe: grpc_test_util
	echo Building census_statistics_multiple_writers_circular_buffer_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\statistics\multiple_writers_circular_buffer_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\census_statistics_multiple_writers_circular_buffer_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\multiple_writers_circular_buffer_test.obj 
census_statistics_multiple_writers_circular_buffer_test: census_statistics_multiple_writers_circular_buffer_test.exe
	echo Running census_statistics_multiple_writers_circular_buffer_test
	$(OUT_DIR)\census_statistics_multiple_writers_circular_buffer_test.exe

census_statistics_multiple_writers_test.exe: grpc_test_util
	echo Building census_statistics_multiple_writers_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\statistics\multiple_writers_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\census_statistics_multiple_writers_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\multiple_writers_test.obj 
census_statistics_multiple_writers_test: census_statistics_multiple_writers_test.exe
	echo Running census_statistics_multiple_writers_test
	$(OUT_DIR)\census_statistics_multiple_writers_test.exe

census_statistics_performance_test.exe: grpc_test_util
	echo Building census_statistics_performance_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\statistics\performance_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\census_statistics_performance_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\performance_test.obj 
census_statistics_performance_test: census_statistics_performance_test.exe
	echo Running census_statistics_performance_test
	$(OUT_DIR)\census_statistics_performance_test.exe

census_statistics_quick_test.exe: grpc_test_util
	echo Building census_statistics_quick_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\statistics\quick_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\census_statistics_quick_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\quick_test.obj 
census_statistics_quick_test: census_statistics_quick_test.exe
	echo Running census_statistics_quick_test
	$(OUT_DIR)\census_statistics_quick_test.exe

census_statistics_small_log_test.exe: grpc_test_util
	echo Building census_statistics_small_log_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\statistics\small_log_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\census_statistics_small_log_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\small_log_test.obj 
census_statistics_small_log_test: census_statistics_small_log_test.exe
	echo Running census_statistics_small_log_test
	$(OUT_DIR)\census_statistics_small_log_test.exe

census_stats_store_test.exe: grpc_test_util
	echo Building census_stats_store_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\statistics\rpc_stats_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\census_stats_store_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\rpc_stats_test.obj 
census_stats_store_test: census_stats_store_test.exe
	echo Running census_stats_store_test
	$(OUT_DIR)\census_stats_store_test.exe

census_stub_test.exe: grpc_test_util
	echo Building census_stub_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\statistics\census_stub_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\census_stub_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\census_stub_test.obj 
census_stub_test: census_stub_test.exe
	echo Running census_stub_test
	$(OUT_DIR)\census_stub_test.exe

census_trace_store_test.exe: grpc_test_util
	echo Building census_trace_store_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\statistics\trace_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\census_trace_store_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\trace_test.obj 
census_trace_store_test: census_trace_store_test.exe
	echo Running census_trace_store_test
	$(OUT_DIR)\census_trace_store_test.exe

census_window_stats_test.exe: grpc_test_util
	echo Building census_window_stats_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\statistics\window_stats_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\census_window_stats_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\window_stats_test.obj 
census_window_stats_test: census_window_stats_test.exe
	echo Running census_window_stats_test
	$(OUT_DIR)\census_window_stats_test.exe

chttp2_status_conversion_test.exe: grpc_test_util
	echo Building chttp2_status_conversion_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\transport\chttp2\status_conversion_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_status_conversion_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\status_conversion_test.obj 
chttp2_status_conversion_test: chttp2_status_conversion_test.exe
	echo Running chttp2_status_conversion_test
	$(OUT_DIR)\chttp2_status_conversion_test.exe

chttp2_stream_encoder_test.exe: grpc_test_util
	echo Building chttp2_stream_encoder_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\transport\chttp2\stream_encoder_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_stream_encoder_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\stream_encoder_test.obj 
chttp2_stream_encoder_test: chttp2_stream_encoder_test.exe
	echo Running chttp2_stream_encoder_test
	$(OUT_DIR)\chttp2_stream_encoder_test.exe

chttp2_stream_map_test.exe: grpc_test_util
	echo Building chttp2_stream_map_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\transport\chttp2\stream_map_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_stream_map_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\stream_map_test.obj 
chttp2_stream_map_test: chttp2_stream_map_test.exe
	echo Running chttp2_stream_map_test
	$(OUT_DIR)\chttp2_stream_map_test.exe

chttp2_transport_end2end_test.exe: grpc_test_util
	echo Building chttp2_transport_end2end_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\transport\chttp2_transport_end2end_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\chttp2_transport_end2end_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\chttp2_transport_end2end_test.obj 
chttp2_transport_end2end_test: chttp2_transport_end2end_test.exe
	echo Running chttp2_transport_end2end_test
	$(OUT_DIR)\chttp2_transport_end2end_test.exe

dualstack_socket_test.exe: grpc_test_util
	echo Building dualstack_socket_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\end2end\dualstack_socket_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\dualstack_socket_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\dualstack_socket_test.obj 
dualstack_socket_test: dualstack_socket_test.exe
	echo Running dualstack_socket_test
	$(OUT_DIR)\dualstack_socket_test.exe

echo_client.exe: grpc_test_util
	echo Building echo_client
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\echo\client.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\echo_client.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\client.obj 
echo_client: echo_client.exe
	echo Running echo_client
	$(OUT_DIR)\echo_client.exe

echo_server.exe: grpc_test_util
	echo Building echo_server
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\echo\server.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\echo_server.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\server.obj 
echo_server: echo_server.exe
	echo Running echo_server
	$(OUT_DIR)\echo_server.exe

echo_test.exe: grpc_test_util
	echo Building echo_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\echo\echo_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\echo_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\echo_test.obj 
echo_test: echo_test.exe
	echo Running echo_test
	$(OUT_DIR)\echo_test.exe

fd_posix_test.exe: grpc_test_util
	echo Building fd_posix_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\iomgr\fd_posix_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\fd_posix_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\fd_posix_test.obj 
fd_posix_test: fd_posix_test.exe
	echo Running fd_posix_test
	$(OUT_DIR)\fd_posix_test.exe

fling_client.exe: grpc_test_util
	echo Building fling_client
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\fling\client.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\fling_client.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\client.obj 
fling_client: fling_client.exe
	echo Running fling_client
	$(OUT_DIR)\fling_client.exe

fling_server.exe: grpc_test_util
	echo Building fling_server
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\fling\server.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\fling_server.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\server.obj 
fling_server: fling_server.exe
	echo Running fling_server
	$(OUT_DIR)\fling_server.exe

fling_stream_test.exe: grpc_test_util
	echo Building fling_stream_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\fling\fling_stream_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\fling_stream_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\fling_stream_test.obj 
fling_stream_test: fling_stream_test.exe
	echo Running fling_stream_test
	$(OUT_DIR)\fling_stream_test.exe

fling_test.exe: grpc_test_util
	echo Building fling_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\fling\fling_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\fling_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\fling_test.obj 
fling_test: fling_test.exe
	echo Running fling_test
	$(OUT_DIR)\fling_test.exe

gen_hpack_tables.exe: grpc_test_util
	echo Building gen_hpack_tables
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\src\core\transport\chttp2\gen_hpack_tables.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gen_hpack_tables.exe" Debug\grpc_test_util.lib Debug\gpr.lib Debug\grpc.lib $(LIBS) $(OUT_DIR)\gen_hpack_tables.obj 
gen_hpack_tables: gen_hpack_tables.exe
	echo Running gen_hpack_tables
	$(OUT_DIR)\gen_hpack_tables.exe

gpr_cancellable_test.exe: grpc_test_util
	echo Building gpr_cancellable_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\support\cancellable_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_cancellable_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\cancellable_test.obj 
gpr_cancellable_test: gpr_cancellable_test.exe
	echo Running gpr_cancellable_test
	$(OUT_DIR)\gpr_cancellable_test.exe

gpr_cmdline_test.exe: grpc_test_util
	echo Building gpr_cmdline_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\support\cmdline_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_cmdline_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\cmdline_test.obj 
gpr_cmdline_test: gpr_cmdline_test.exe
	echo Running gpr_cmdline_test
	$(OUT_DIR)\gpr_cmdline_test.exe

gpr_env_test.exe: grpc_test_util
	echo Building gpr_env_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\support\env_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_env_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\env_test.obj 
gpr_env_test: gpr_env_test.exe
	echo Running gpr_env_test
	$(OUT_DIR)\gpr_env_test.exe

gpr_file_test.exe: grpc_test_util
	echo Building gpr_file_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\support\file_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_file_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\file_test.obj 
gpr_file_test: gpr_file_test.exe
	echo Running gpr_file_test
	$(OUT_DIR)\gpr_file_test.exe

gpr_histogram_test.exe: grpc_test_util
	echo Building gpr_histogram_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\support\histogram_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_histogram_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\histogram_test.obj 
gpr_histogram_test: gpr_histogram_test.exe
	echo Running gpr_histogram_test
	$(OUT_DIR)\gpr_histogram_test.exe

gpr_host_port_test.exe: grpc_test_util
	echo Building gpr_host_port_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\support\host_port_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_host_port_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\host_port_test.obj 
gpr_host_port_test: gpr_host_port_test.exe
	echo Running gpr_host_port_test
	$(OUT_DIR)\gpr_host_port_test.exe

gpr_log_test.exe: grpc_test_util
	echo Building gpr_log_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\support\log_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_log_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\log_test.obj 
gpr_log_test: gpr_log_test.exe
	echo Running gpr_log_test
	$(OUT_DIR)\gpr_log_test.exe

gpr_slice_buffer_test.exe: grpc_test_util
	echo Building gpr_slice_buffer_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\support\slice_buffer_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_slice_buffer_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\slice_buffer_test.obj 
gpr_slice_buffer_test: gpr_slice_buffer_test.exe
	echo Running gpr_slice_buffer_test
	$(OUT_DIR)\gpr_slice_buffer_test.exe

gpr_slice_test.exe: grpc_test_util
	echo Building gpr_slice_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\support\slice_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_slice_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\slice_test.obj 
gpr_slice_test: gpr_slice_test.exe
	echo Running gpr_slice_test
	$(OUT_DIR)\gpr_slice_test.exe

gpr_string_test.exe: grpc_test_util
	echo Building gpr_string_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\support\string_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_string_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\string_test.obj 
gpr_string_test: gpr_string_test.exe
	echo Running gpr_string_test
	$(OUT_DIR)\gpr_string_test.exe

gpr_sync_test.exe: grpc_test_util
	echo Building gpr_sync_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\support\sync_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_sync_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\sync_test.obj 
gpr_sync_test: gpr_sync_test.exe
	echo Running gpr_sync_test
	$(OUT_DIR)\gpr_sync_test.exe

gpr_thd_test.exe: grpc_test_util
	echo Building gpr_thd_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\support\thd_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_thd_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\thd_test.obj 
gpr_thd_test: gpr_thd_test.exe
	echo Running gpr_thd_test
	$(OUT_DIR)\gpr_thd_test.exe

gpr_time_test.exe: grpc_test_util
	echo Building gpr_time_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\support\time_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_time_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\time_test.obj 
gpr_time_test: gpr_time_test.exe
	echo Running gpr_time_test
	$(OUT_DIR)\gpr_time_test.exe

gpr_useful_test.exe: grpc_test_util
	echo Building gpr_useful_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\support\useful_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\gpr_useful_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\useful_test.obj 
gpr_useful_test: gpr_useful_test.exe
	echo Running gpr_useful_test
	$(OUT_DIR)\gpr_useful_test.exe

grpc_base64_test.exe: grpc_test_util
	echo Building grpc_base64_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\security\base64_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\grpc_base64_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\base64_test.obj 
grpc_base64_test: grpc_base64_test.exe
	echo Running grpc_base64_test
	$(OUT_DIR)\grpc_base64_test.exe

grpc_byte_buffer_reader_test.exe: grpc_test_util
	echo Building grpc_byte_buffer_reader_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\surface\byte_buffer_reader_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\grpc_byte_buffer_reader_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\byte_buffer_reader_test.obj 
grpc_byte_buffer_reader_test: grpc_byte_buffer_reader_test.exe
	echo Running grpc_byte_buffer_reader_test
	$(OUT_DIR)\grpc_byte_buffer_reader_test.exe

grpc_channel_stack_test.exe: grpc_test_util
	echo Building grpc_channel_stack_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\channel\channel_stack_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\grpc_channel_stack_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\channel_stack_test.obj 
grpc_channel_stack_test: grpc_channel_stack_test.exe
	echo Running grpc_channel_stack_test
	$(OUT_DIR)\grpc_channel_stack_test.exe

grpc_completion_queue_benchmark.exe: grpc_test_util
	echo Building grpc_completion_queue_benchmark
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\surface\completion_queue_benchmark.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\grpc_completion_queue_benchmark.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\completion_queue_benchmark.obj 
grpc_completion_queue_benchmark: grpc_completion_queue_benchmark.exe
	echo Running grpc_completion_queue_benchmark
	$(OUT_DIR)\grpc_completion_queue_benchmark.exe

grpc_completion_queue_test.exe: grpc_test_util
	echo Building grpc_completion_queue_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\surface\completion_queue_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\grpc_completion_queue_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\completion_queue_test.obj 
grpc_completion_queue_test: grpc_completion_queue_test.exe
	echo Running grpc_completion_queue_test
	$(OUT_DIR)\grpc_completion_queue_test.exe

grpc_create_jwt.exe: grpc_test_util
	echo Building grpc_create_jwt
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\security\create_jwt.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\grpc_create_jwt.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\create_jwt.obj 
grpc_create_jwt: grpc_create_jwt.exe
	echo Running grpc_create_jwt
	$(OUT_DIR)\grpc_create_jwt.exe

grpc_credentials_test.exe: grpc_test_util
	echo Building grpc_credentials_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\security\credentials_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\grpc_credentials_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\credentials_test.obj 
grpc_credentials_test: grpc_credentials_test.exe
	echo Running grpc_credentials_test
	$(OUT_DIR)\grpc_credentials_test.exe

grpc_fetch_oauth2.exe: grpc_test_util
	echo Building grpc_fetch_oauth2
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\security\fetch_oauth2.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\grpc_fetch_oauth2.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\fetch_oauth2.obj 
grpc_fetch_oauth2: grpc_fetch_oauth2.exe
	echo Running grpc_fetch_oauth2
	$(OUT_DIR)\grpc_fetch_oauth2.exe

grpc_json_token_test.exe: grpc_test_util
	echo Building grpc_json_token_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\security\json_token_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\grpc_json_token_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\json_token_test.obj 
grpc_json_token_test: grpc_json_token_test.exe
	echo Running grpc_json_token_test
	$(OUT_DIR)\grpc_json_token_test.exe

grpc_print_google_default_creds_token.exe: grpc_test_util
	echo Building grpc_print_google_default_creds_token
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\security\print_google_default_creds_token.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\grpc_print_google_default_creds_token.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\print_google_default_creds_token.obj 
grpc_print_google_default_creds_token: grpc_print_google_default_creds_token.exe
	echo Running grpc_print_google_default_creds_token
	$(OUT_DIR)\grpc_print_google_default_creds_token.exe

grpc_stream_op_test.exe: grpc_test_util
	echo Building grpc_stream_op_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\transport\stream_op_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\grpc_stream_op_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\stream_op_test.obj 
grpc_stream_op_test: grpc_stream_op_test.exe
	echo Running grpc_stream_op_test
	$(OUT_DIR)\grpc_stream_op_test.exe

hpack_parser_test.exe: grpc_test_util
	echo Building hpack_parser_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\transport\chttp2\hpack_parser_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\hpack_parser_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\hpack_parser_test.obj 
hpack_parser_test: hpack_parser_test.exe
	echo Running hpack_parser_test
	$(OUT_DIR)\hpack_parser_test.exe

hpack_table_test.exe: grpc_test_util
	echo Building hpack_table_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\transport\chttp2\hpack_table_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\hpack_table_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\hpack_table_test.obj 
hpack_table_test: hpack_table_test.exe
	echo Running hpack_table_test
	$(OUT_DIR)\hpack_table_test.exe

httpcli_format_request_test.exe: grpc_test_util
	echo Building httpcli_format_request_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\httpcli\format_request_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\httpcli_format_request_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\format_request_test.obj 
httpcli_format_request_test: httpcli_format_request_test.exe
	echo Running httpcli_format_request_test
	$(OUT_DIR)\httpcli_format_request_test.exe

httpcli_parser_test.exe: grpc_test_util
	echo Building httpcli_parser_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\httpcli\parser_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\httpcli_parser_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\parser_test.obj 
httpcli_parser_test: httpcli_parser_test.exe
	echo Running httpcli_parser_test
	$(OUT_DIR)\httpcli_parser_test.exe

httpcli_test.exe: grpc_test_util
	echo Building httpcli_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\httpcli\httpcli_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\httpcli_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\httpcli_test.obj 
httpcli_test: httpcli_test.exe
	echo Running httpcli_test
	$(OUT_DIR)\httpcli_test.exe

json_rewrite.exe: grpc_test_util
	echo Building json_rewrite
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\json\json_rewrite.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\json_rewrite.exe" Debug\grpc.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\json_rewrite.obj 
json_rewrite: json_rewrite.exe
	echo Running json_rewrite
	$(OUT_DIR)\json_rewrite.exe

json_rewrite_test.exe: grpc_test_util
	echo Building json_rewrite_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\json\json_rewrite_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\json_rewrite_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\json_rewrite_test.obj 
json_rewrite_test: json_rewrite_test.exe
	echo Running json_rewrite_test
	$(OUT_DIR)\json_rewrite_test.exe

json_test.exe: grpc_test_util
	echo Building json_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\json\json_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\json_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\json_test.obj 
json_test: json_test.exe
	echo Running json_test
	$(OUT_DIR)\json_test.exe

lame_client_test.exe: grpc_test_util
	echo Building lame_client_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\surface\lame_client_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\lame_client_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\lame_client_test.obj 
lame_client_test: lame_client_test.exe
	echo Running lame_client_test
	$(OUT_DIR)\lame_client_test.exe

low_level_ping_pong_benchmark.exe: grpc_test_util
	echo Building low_level_ping_pong_benchmark
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\network_benchmarks\low_level_ping_pong.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\low_level_ping_pong_benchmark.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\low_level_ping_pong.obj 
low_level_ping_pong_benchmark: low_level_ping_pong_benchmark.exe
	echo Running low_level_ping_pong_benchmark
	$(OUT_DIR)\low_level_ping_pong_benchmark.exe

message_compress_test.exe: grpc_test_util
	echo Building message_compress_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\compression\message_compress_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\message_compress_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\message_compress_test.obj 
message_compress_test: message_compress_test.exe
	echo Running message_compress_test
	$(OUT_DIR)\message_compress_test.exe

metadata_buffer_test.exe: grpc_test_util
	echo Building metadata_buffer_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\channel\metadata_buffer_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\metadata_buffer_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\metadata_buffer_test.obj 
metadata_buffer_test: metadata_buffer_test.exe
	echo Running metadata_buffer_test
	$(OUT_DIR)\metadata_buffer_test.exe

multi_init_test.exe: grpc_test_util
	echo Building multi_init_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\surface\multi_init_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\multi_init_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\multi_init_test.obj 
multi_init_test: multi_init_test.exe
	echo Running multi_init_test
	$(OUT_DIR)\multi_init_test.exe

murmur_hash_test.exe: grpc_test_util
	echo Building murmur_hash_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\support\murmur_hash_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\murmur_hash_test.exe" Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\murmur_hash_test.obj 
murmur_hash_test: murmur_hash_test.exe
	echo Running murmur_hash_test
	$(OUT_DIR)\murmur_hash_test.exe

no_server_test.exe: grpc_test_util
	echo Building no_server_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\end2end\no_server_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\no_server_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\no_server_test.obj 
no_server_test: no_server_test.exe
	echo Running no_server_test
	$(OUT_DIR)\no_server_test.exe

poll_kick_posix_test.exe: grpc_test_util
	echo Building poll_kick_posix_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\iomgr\poll_kick_posix_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\poll_kick_posix_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\poll_kick_posix_test.obj 
poll_kick_posix_test: poll_kick_posix_test.exe
	echo Running poll_kick_posix_test
	$(OUT_DIR)\poll_kick_posix_test.exe

resolve_address_test.exe: grpc_test_util
	echo Building resolve_address_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\iomgr\resolve_address_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\resolve_address_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\resolve_address_test.obj 
resolve_address_test: resolve_address_test.exe
	echo Running resolve_address_test
	$(OUT_DIR)\resolve_address_test.exe

secure_endpoint_test.exe: grpc_test_util
	echo Building secure_endpoint_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\security\secure_endpoint_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\secure_endpoint_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\secure_endpoint_test.obj 
secure_endpoint_test: secure_endpoint_test.exe
	echo Running secure_endpoint_test
	$(OUT_DIR)\secure_endpoint_test.exe

sockaddr_utils_test.exe: grpc_test_util
	echo Building sockaddr_utils_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\iomgr\sockaddr_utils_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\sockaddr_utils_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\sockaddr_utils_test.obj 
sockaddr_utils_test: sockaddr_utils_test.exe
	echo Running sockaddr_utils_test
	$(OUT_DIR)\sockaddr_utils_test.exe

tcp_client_posix_test.exe: grpc_test_util
	echo Building tcp_client_posix_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\iomgr\tcp_client_posix_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\tcp_client_posix_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\tcp_client_posix_test.obj 
tcp_client_posix_test: tcp_client_posix_test.exe
	echo Running tcp_client_posix_test
	$(OUT_DIR)\tcp_client_posix_test.exe

tcp_posix_test.exe: grpc_test_util
	echo Building tcp_posix_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\iomgr\tcp_posix_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\tcp_posix_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\tcp_posix_test.obj 
tcp_posix_test: tcp_posix_test.exe
	echo Running tcp_posix_test
	$(OUT_DIR)\tcp_posix_test.exe

tcp_server_posix_test.exe: grpc_test_util
	echo Building tcp_server_posix_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\iomgr\tcp_server_posix_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\tcp_server_posix_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\tcp_server_posix_test.obj 
tcp_server_posix_test: tcp_server_posix_test.exe
	echo Running tcp_server_posix_test
	$(OUT_DIR)\tcp_server_posix_test.exe

time_averaged_stats_test.exe: grpc_test_util
	echo Building time_averaged_stats_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\iomgr\time_averaged_stats_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\time_averaged_stats_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\time_averaged_stats_test.obj 
time_averaged_stats_test: time_averaged_stats_test.exe
	echo Running time_averaged_stats_test
	$(OUT_DIR)\time_averaged_stats_test.exe

time_test.exe: grpc_test_util
	echo Building time_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\support\time_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\time_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\time_test.obj 
time_test: time_test.exe
	echo Running time_test
	$(OUT_DIR)\time_test.exe

timeout_encoding_test.exe: grpc_test_util
	echo Building timeout_encoding_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\transport\chttp2\timeout_encoding_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\timeout_encoding_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\timeout_encoding_test.obj 
timeout_encoding_test: timeout_encoding_test.exe
	echo Running timeout_encoding_test
	$(OUT_DIR)\timeout_encoding_test.exe

transport_metadata_test.exe: grpc_test_util
	echo Building transport_metadata_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\transport\metadata_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\transport_metadata_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\metadata_test.obj 
transport_metadata_test: transport_metadata_test.exe
	echo Running transport_metadata_test
	$(OUT_DIR)\transport_metadata_test.exe

transport_security_test.exe: grpc_test_util
	echo Building transport_security_test
	$(CC) $(CFLAGS) /Fo:$(OUT_DIR)\ ..\..\test\core\tsi\transport_security_test.c 
	$(LINK) $(LFLAGS) /OUT:"$(OUT_DIR)\transport_security_test.exe" Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(LIBS) $(OUT_DIR)\transport_security_test.obj 
transport_security_test: transport_security_test.exe
	echo Running transport_security_test
	$(OUT_DIR)\transport_security_test.exe

