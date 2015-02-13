# NMake file to build secondary gRPC targets on Windows.
# Use grpc.sln to solution to build the gRPC libraries.

OUT_DIR=test_bin

gpr_test_util:
	MSBuild.exe gpr_test_util.vcxproj /p:Configuration=Debug

grpc_test_util:
	MSBuild.exe grpc_test_util.vcxproj /p:Configuration=Debug

$(OUT_DIR):
	mkdir $(OUT_DIR)

buildtests: alarm_heap_test.exe alarm_list_test.exe alarm_test.exe alpn_test.exe bin_encoder_test.exe census_hash_table_test.exe census_statistics_multiple_writers_circular_buffer_test.exe census_statistics_multiple_writers_test.exe census_statistics_performance_test.exe census_statistics_quick_test.exe census_statistics_small_log_test.exe census_stats_store_test.exe census_stub_test.exe census_trace_store_test.exe census_window_stats_test.exe chttp2_status_conversion_test.exe chttp2_stream_encoder_test.exe chttp2_stream_map_test.exe chttp2_transport_end2end_test.exe dualstack_socket_test.exe echo_test.exe fd_posix_test.exe fling_stream_test.exe fling_test.exe gpr_cancellable_test.exe gpr_cmdline_test.exe gpr_env_test.exe gpr_file_test.exe gpr_histogram_test.exe gpr_host_port_test.exe gpr_log_test.exe gpr_slice_buffer_test.exe gpr_slice_test.exe gpr_string_test.exe gpr_sync_test.exe gpr_thd_test.exe gpr_time_test.exe gpr_useful_test.exe grpc_base64_test.exe grpc_byte_buffer_reader_test.exe grpc_channel_stack_test.exe grpc_completion_queue_test.exe grpc_credentials_test.exe grpc_json_token_test.exe grpc_stream_op_test.exe hpack_parser_test.exe hpack_table_test.exe httpcli_format_request_test.exe httpcli_parser_test.exe httpcli_test.exe json_rewrite_test.exe json_test.exe lame_client_test.exe message_compress_test.exe metadata_buffer_test.exe murmur_hash_test.exe no_server_test.exe poll_kick_posix_test.exe resolve_address_test.exe secure_endpoint_test.exe sockaddr_utils_test.exe tcp_client_posix_test.exe tcp_posix_test.exe tcp_server_posix_test.exe time_averaged_stats_test.exe time_test.exe timeout_encoding_test.exe transport_metadata_test.exe 
	echo All tests built.

test: alarm_heap_test alarm_list_test alarm_test alpn_test bin_encoder_test census_hash_table_test census_statistics_multiple_writers_circular_buffer_test census_statistics_multiple_writers_test census_statistics_performance_test census_statistics_quick_test census_statistics_small_log_test census_stats_store_test census_stub_test census_trace_store_test census_window_stats_test chttp2_status_conversion_test chttp2_stream_encoder_test chttp2_stream_map_test chttp2_transport_end2end_test dualstack_socket_test echo_test fd_posix_test fling_stream_test fling_test gpr_cancellable_test gpr_cmdline_test gpr_env_test gpr_file_test gpr_histogram_test gpr_host_port_test gpr_log_test gpr_slice_buffer_test gpr_slice_test gpr_string_test gpr_sync_test gpr_thd_test gpr_time_test gpr_useful_test grpc_base64_test grpc_byte_buffer_reader_test grpc_channel_stack_test grpc_completion_queue_test grpc_credentials_test grpc_json_token_test grpc_stream_op_test hpack_parser_test hpack_table_test httpcli_format_request_test httpcli_parser_test httpcli_test json_rewrite_test json_test lame_client_test message_compress_test metadata_buffer_test murmur_hash_test no_server_test poll_kick_posix_test resolve_address_test secure_endpoint_test sockaddr_utils_test tcp_client_posix_test tcp_posix_test tcp_server_posix_test time_averaged_stats_test time_test timeout_encoding_test transport_metadata_test 
	echo All tests ran.

test_gpr: gpr_cancellable_test gpr_cmdline_test gpr_env_test gpr_file_test gpr_histogram_test gpr_host_port_test gpr_log_test gpr_slice_buffer_test gpr_slice_test gpr_string_test gpr_sync_test gpr_thd_test gpr_time_test gpr_useful_test 
	echo All tests ran.

alarm_heap_test.exe: grpc_test_util
	echo Building alarm_heap_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\iomgr\alarm_heap_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\alarm_heap_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\alarm_heap_test.obj 

alarm_heap_test: alarm_heap_test.exe
	echo Running alarm_heap_test
	$(OUT_DIR)\alarm_heap_test.exe

alarm_list_test.exe: grpc_test_util
	echo Building alarm_list_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\iomgr\alarm_list_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\alarm_list_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\alarm_list_test.obj 

alarm_list_test: alarm_list_test.exe
	echo Running alarm_list_test
	$(OUT_DIR)\alarm_list_test.exe

alarm_test.exe: grpc_test_util
	echo Building alarm_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\iomgr\alarm_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\alarm_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\alarm_test.obj 

alarm_test: alarm_test.exe
	echo Running alarm_test
	$(OUT_DIR)\alarm_test.exe

alpn_test.exe: grpc_test_util
	echo Building alpn_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\transport\chttp2\alpn_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\alpn_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\alpn_test.obj 

alpn_test: alpn_test.exe
	echo Running alpn_test
	$(OUT_DIR)\alpn_test.exe

bin_encoder_test.exe: grpc_test_util
	echo Building bin_encoder_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\transport\chttp2\bin_encoder_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\bin_encoder_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\bin_encoder_test.obj 

bin_encoder_test: bin_encoder_test.exe
	echo Running bin_encoder_test
	$(OUT_DIR)\bin_encoder_test.exe

census_hash_table_test.exe: grpc_test_util
	echo Building census_hash_table_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\statistics\hash_table_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\census_hash_table_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\hash_table_test.obj 

census_hash_table_test: census_hash_table_test.exe
	echo Running census_hash_table_test
	$(OUT_DIR)\census_hash_table_test.exe

census_statistics_multiple_writers_circular_buffer_test.exe: grpc_test_util
	echo Building census_statistics_multiple_writers_circular_buffer_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\statistics\multiple_writers_circular_buffer_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\census_statistics_multiple_writers_circular_buffer_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\multiple_writers_circular_buffer_test.obj 

census_statistics_multiple_writers_circular_buffer_test: census_statistics_multiple_writers_circular_buffer_test.exe
	echo Running census_statistics_multiple_writers_circular_buffer_test
	$(OUT_DIR)\census_statistics_multiple_writers_circular_buffer_test.exe

census_statistics_multiple_writers_test.exe: grpc_test_util
	echo Building census_statistics_multiple_writers_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\statistics\multiple_writers_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\census_statistics_multiple_writers_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\multiple_writers_test.obj 

census_statistics_multiple_writers_test: census_statistics_multiple_writers_test.exe
	echo Running census_statistics_multiple_writers_test
	$(OUT_DIR)\census_statistics_multiple_writers_test.exe

census_statistics_performance_test.exe: grpc_test_util
	echo Building census_statistics_performance_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\statistics\performance_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\census_statistics_performance_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\performance_test.obj 

census_statistics_performance_test: census_statistics_performance_test.exe
	echo Running census_statistics_performance_test
	$(OUT_DIR)\census_statistics_performance_test.exe

census_statistics_quick_test.exe: grpc_test_util
	echo Building census_statistics_quick_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\statistics\quick_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\census_statistics_quick_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\quick_test.obj 

census_statistics_quick_test: census_statistics_quick_test.exe
	echo Running census_statistics_quick_test
	$(OUT_DIR)\census_statistics_quick_test.exe

census_statistics_small_log_test.exe: grpc_test_util
	echo Building census_statistics_small_log_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\statistics\small_log_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\census_statistics_small_log_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\small_log_test.obj 

census_statistics_small_log_test: census_statistics_small_log_test.exe
	echo Running census_statistics_small_log_test
	$(OUT_DIR)\census_statistics_small_log_test.exe

census_stats_store_test.exe: grpc_test_util
	echo Building census_stats_store_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\statistics\rpc_stats_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\census_stats_store_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\rpc_stats_test.obj 

census_stats_store_test: census_stats_store_test.exe
	echo Running census_stats_store_test
	$(OUT_DIR)\census_stats_store_test.exe

census_stub_test.exe: grpc_test_util
	echo Building census_stub_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\statistics\census_stub_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\census_stub_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\census_stub_test.obj 

census_stub_test: census_stub_test.exe
	echo Running census_stub_test
	$(OUT_DIR)\census_stub_test.exe

census_trace_store_test.exe: grpc_test_util
	echo Building census_trace_store_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\statistics\trace_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\census_trace_store_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\trace_test.obj 

census_trace_store_test: census_trace_store_test.exe
	echo Running census_trace_store_test
	$(OUT_DIR)\census_trace_store_test.exe

census_window_stats_test.exe: grpc_test_util
	echo Building census_window_stats_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\statistics\window_stats_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\census_window_stats_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\window_stats_test.obj 

census_window_stats_test: census_window_stats_test.exe
	echo Running census_window_stats_test
	$(OUT_DIR)\census_window_stats_test.exe

chttp2_status_conversion_test.exe: grpc_test_util
	echo Building chttp2_status_conversion_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\transport\chttp2\status_conversion_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\chttp2_status_conversion_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\status_conversion_test.obj 

chttp2_status_conversion_test: chttp2_status_conversion_test.exe
	echo Running chttp2_status_conversion_test
	$(OUT_DIR)\chttp2_status_conversion_test.exe

chttp2_stream_encoder_test.exe: grpc_test_util
	echo Building chttp2_stream_encoder_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\transport\chttp2\stream_encoder_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\chttp2_stream_encoder_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\stream_encoder_test.obj 

chttp2_stream_encoder_test: chttp2_stream_encoder_test.exe
	echo Running chttp2_stream_encoder_test
	$(OUT_DIR)\chttp2_stream_encoder_test.exe

chttp2_stream_map_test.exe: grpc_test_util
	echo Building chttp2_stream_map_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\transport\chttp2\stream_map_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\chttp2_stream_map_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\stream_map_test.obj 

chttp2_stream_map_test: chttp2_stream_map_test.exe
	echo Running chttp2_stream_map_test
	$(OUT_DIR)\chttp2_stream_map_test.exe

chttp2_transport_end2end_test.exe: grpc_test_util
	echo Building chttp2_transport_end2end_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\transport\chttp2_transport_end2end_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\chttp2_transport_end2end_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\chttp2_transport_end2end_test.obj 

chttp2_transport_end2end_test: chttp2_transport_end2end_test.exe
	echo Running chttp2_transport_end2end_test
	$(OUT_DIR)\chttp2_transport_end2end_test.exe

dualstack_socket_test.exe: grpc_test_util
	echo Building dualstack_socket_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\end2end\dualstack_socket_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\dualstack_socket_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\dualstack_socket_test.obj 

dualstack_socket_test: dualstack_socket_test.exe
	echo Running dualstack_socket_test
	$(OUT_DIR)\dualstack_socket_test.exe

echo_test.exe: grpc_test_util
	echo Building echo_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\echo\echo_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\echo_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\echo_test.obj 

echo_test: echo_test.exe
	echo Running echo_test
	$(OUT_DIR)\echo_test.exe

fd_posix_test.exe: grpc_test_util
	echo Building fd_posix_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\iomgr\fd_posix_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\fd_posix_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\fd_posix_test.obj 

fd_posix_test: fd_posix_test.exe
	echo Running fd_posix_test
	$(OUT_DIR)\fd_posix_test.exe

fling_stream_test.exe: grpc_test_util
	echo Building fling_stream_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\fling\fling_stream_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\fling_stream_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\fling_stream_test.obj 

fling_stream_test: fling_stream_test.exe
	echo Running fling_stream_test
	$(OUT_DIR)\fling_stream_test.exe

fling_test.exe: grpc_test_util
	echo Building fling_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\fling\fling_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\fling_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\fling_test.obj 

fling_test: fling_test.exe
	echo Running fling_test
	$(OUT_DIR)\fling_test.exe

gpr_cancellable_test.exe: grpc_test_util
	echo Building gpr_cancellable_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\support\cancellable_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\gpr_cancellable_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\cancellable_test.obj 

gpr_cancellable_test: gpr_cancellable_test.exe
	echo Running gpr_cancellable_test
	$(OUT_DIR)\gpr_cancellable_test.exe

gpr_cmdline_test.exe: grpc_test_util
	echo Building gpr_cmdline_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\support\cmdline_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\gpr_cmdline_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\cmdline_test.obj 

gpr_cmdline_test: gpr_cmdline_test.exe
	echo Running gpr_cmdline_test
	$(OUT_DIR)\gpr_cmdline_test.exe

gpr_env_test.exe: grpc_test_util
	echo Building gpr_env_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\support\env_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\gpr_env_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\env_test.obj 

gpr_env_test: gpr_env_test.exe
	echo Running gpr_env_test
	$(OUT_DIR)\gpr_env_test.exe

gpr_file_test.exe: grpc_test_util
	echo Building gpr_file_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\support\file_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\gpr_file_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\file_test.obj 

gpr_file_test: gpr_file_test.exe
	echo Running gpr_file_test
	$(OUT_DIR)\gpr_file_test.exe

gpr_histogram_test.exe: grpc_test_util
	echo Building gpr_histogram_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\support\histogram_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\gpr_histogram_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\histogram_test.obj 

gpr_histogram_test: gpr_histogram_test.exe
	echo Running gpr_histogram_test
	$(OUT_DIR)\gpr_histogram_test.exe

gpr_host_port_test.exe: grpc_test_util
	echo Building gpr_host_port_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\support\host_port_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\gpr_host_port_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\host_port_test.obj 

gpr_host_port_test: gpr_host_port_test.exe
	echo Running gpr_host_port_test
	$(OUT_DIR)\gpr_host_port_test.exe

gpr_log_test.exe: grpc_test_util
	echo Building gpr_log_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\support\log_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\gpr_log_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\log_test.obj 

gpr_log_test: gpr_log_test.exe
	echo Running gpr_log_test
	$(OUT_DIR)\gpr_log_test.exe

gpr_slice_buffer_test.exe: grpc_test_util
	echo Building gpr_slice_buffer_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\support\slice_buffer_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\gpr_slice_buffer_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\slice_buffer_test.obj 

gpr_slice_buffer_test: gpr_slice_buffer_test.exe
	echo Running gpr_slice_buffer_test
	$(OUT_DIR)\gpr_slice_buffer_test.exe

gpr_slice_test.exe: grpc_test_util
	echo Building gpr_slice_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\support\slice_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\gpr_slice_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\slice_test.obj 

gpr_slice_test: gpr_slice_test.exe
	echo Running gpr_slice_test
	$(OUT_DIR)\gpr_slice_test.exe

gpr_string_test.exe: grpc_test_util
	echo Building gpr_string_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\support\string_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\gpr_string_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\string_test.obj 

gpr_string_test: gpr_string_test.exe
	echo Running gpr_string_test
	$(OUT_DIR)\gpr_string_test.exe

gpr_sync_test.exe: grpc_test_util
	echo Building gpr_sync_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\support\sync_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\gpr_sync_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\sync_test.obj 

gpr_sync_test: gpr_sync_test.exe
	echo Running gpr_sync_test
	$(OUT_DIR)\gpr_sync_test.exe

gpr_thd_test.exe: grpc_test_util
	echo Building gpr_thd_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\support\thd_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\gpr_thd_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\thd_test.obj 

gpr_thd_test: gpr_thd_test.exe
	echo Running gpr_thd_test
	$(OUT_DIR)\gpr_thd_test.exe

gpr_time_test.exe: grpc_test_util
	echo Building gpr_time_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\support\time_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\gpr_time_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\time_test.obj 

gpr_time_test: gpr_time_test.exe
	echo Running gpr_time_test
	$(OUT_DIR)\gpr_time_test.exe

gpr_useful_test.exe: grpc_test_util
	echo Building gpr_useful_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\support\useful_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\gpr_useful_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\useful_test.obj 

gpr_useful_test: gpr_useful_test.exe
	echo Running gpr_useful_test
	$(OUT_DIR)\gpr_useful_test.exe

grpc_base64_test.exe: grpc_test_util
	echo Building grpc_base64_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\security\base64_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\grpc_base64_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\base64_test.obj 

grpc_base64_test: grpc_base64_test.exe
	echo Running grpc_base64_test
	$(OUT_DIR)\grpc_base64_test.exe

grpc_byte_buffer_reader_test.exe: grpc_test_util
	echo Building grpc_byte_buffer_reader_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\surface\byte_buffer_reader_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\grpc_byte_buffer_reader_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\byte_buffer_reader_test.obj 

grpc_byte_buffer_reader_test: grpc_byte_buffer_reader_test.exe
	echo Running grpc_byte_buffer_reader_test
	$(OUT_DIR)\grpc_byte_buffer_reader_test.exe

grpc_channel_stack_test.exe: grpc_test_util
	echo Building grpc_channel_stack_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\channel\channel_stack_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\grpc_channel_stack_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\channel_stack_test.obj 

grpc_channel_stack_test: grpc_channel_stack_test.exe
	echo Running grpc_channel_stack_test
	$(OUT_DIR)\grpc_channel_stack_test.exe

grpc_completion_queue_test.exe: grpc_test_util
	echo Building grpc_completion_queue_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\surface\completion_queue_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\grpc_completion_queue_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\completion_queue_test.obj 

grpc_completion_queue_test: grpc_completion_queue_test.exe
	echo Running grpc_completion_queue_test
	$(OUT_DIR)\grpc_completion_queue_test.exe

grpc_credentials_test.exe: grpc_test_util
	echo Building grpc_credentials_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\security\credentials_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\grpc_credentials_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\credentials_test.obj 

grpc_credentials_test: grpc_credentials_test.exe
	echo Running grpc_credentials_test
	$(OUT_DIR)\grpc_credentials_test.exe

grpc_json_token_test.exe: grpc_test_util
	echo Building grpc_json_token_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\security\json_token_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\grpc_json_token_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\json_token_test.obj 

grpc_json_token_test: grpc_json_token_test.exe
	echo Running grpc_json_token_test
	$(OUT_DIR)\grpc_json_token_test.exe

grpc_stream_op_test.exe: grpc_test_util
	echo Building grpc_stream_op_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\transport\stream_op_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\grpc_stream_op_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\stream_op_test.obj 

grpc_stream_op_test: grpc_stream_op_test.exe
	echo Running grpc_stream_op_test
	$(OUT_DIR)\grpc_stream_op_test.exe

hpack_parser_test.exe: grpc_test_util
	echo Building hpack_parser_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\transport\chttp2\hpack_parser_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\hpack_parser_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\hpack_parser_test.obj 

hpack_parser_test: hpack_parser_test.exe
	echo Running hpack_parser_test
	$(OUT_DIR)\hpack_parser_test.exe

hpack_table_test.exe: grpc_test_util
	echo Building hpack_table_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\transport\chttp2\hpack_table_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\hpack_table_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\hpack_table_test.obj 

hpack_table_test: hpack_table_test.exe
	echo Running hpack_table_test
	$(OUT_DIR)\hpack_table_test.exe

httpcli_format_request_test.exe: grpc_test_util
	echo Building httpcli_format_request_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\httpcli\format_request_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\httpcli_format_request_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\format_request_test.obj 

httpcli_format_request_test: httpcli_format_request_test.exe
	echo Running httpcli_format_request_test
	$(OUT_DIR)\httpcli_format_request_test.exe

httpcli_parser_test.exe: grpc_test_util
	echo Building httpcli_parser_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\httpcli\parser_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\httpcli_parser_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\parser_test.obj 

httpcli_parser_test: httpcli_parser_test.exe
	echo Running httpcli_parser_test
	$(OUT_DIR)\httpcli_parser_test.exe

httpcli_test.exe: grpc_test_util
	echo Building httpcli_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\httpcli\httpcli_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\httpcli_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\httpcli_test.obj 

httpcli_test: httpcli_test.exe
	echo Running httpcli_test
	$(OUT_DIR)\httpcli_test.exe

json_rewrite_test.exe: grpc_test_util
	echo Building json_rewrite_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\json\json_rewrite_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\json_rewrite_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\json_rewrite_test.obj 

json_rewrite_test: json_rewrite_test.exe
	echo Running json_rewrite_test
	$(OUT_DIR)\json_rewrite_test.exe

json_test.exe: grpc_test_util
	echo Building json_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\json\json_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\json_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\json_test.obj 

json_test: json_test.exe
	echo Running json_test
	$(OUT_DIR)\json_test.exe

lame_client_test.exe: grpc_test_util
	echo Building lame_client_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\surface\lame_client_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\lame_client_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\lame_client_test.obj 

lame_client_test: lame_client_test.exe
	echo Running lame_client_test
	$(OUT_DIR)\lame_client_test.exe

message_compress_test.exe: grpc_test_util
	echo Building message_compress_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\compression\message_compress_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\message_compress_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\message_compress_test.obj 

message_compress_test: message_compress_test.exe
	echo Running message_compress_test
	$(OUT_DIR)\message_compress_test.exe

metadata_buffer_test.exe: grpc_test_util
	echo Building metadata_buffer_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\channel\metadata_buffer_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\metadata_buffer_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\metadata_buffer_test.obj 

metadata_buffer_test: metadata_buffer_test.exe
	echo Running metadata_buffer_test
	$(OUT_DIR)\metadata_buffer_test.exe

murmur_hash_test.exe: grpc_test_util
	echo Building murmur_hash_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\support\murmur_hash_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\murmur_hash_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\murmur_hash_test.obj 

murmur_hash_test: murmur_hash_test.exe
	echo Running murmur_hash_test
	$(OUT_DIR)\murmur_hash_test.exe

no_server_test.exe: grpc_test_util
	echo Building no_server_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\end2end\no_server_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\no_server_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\no_server_test.obj 

no_server_test: no_server_test.exe
	echo Running no_server_test
	$(OUT_DIR)\no_server_test.exe

poll_kick_posix_test.exe: grpc_test_util
	echo Building poll_kick_posix_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\iomgr\poll_kick_posix_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\poll_kick_posix_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\poll_kick_posix_test.obj 

poll_kick_posix_test: poll_kick_posix_test.exe
	echo Running poll_kick_posix_test
	$(OUT_DIR)\poll_kick_posix_test.exe

resolve_address_test.exe: grpc_test_util
	echo Building resolve_address_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\iomgr\resolve_address_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\resolve_address_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\resolve_address_test.obj 

resolve_address_test: resolve_address_test.exe
	echo Running resolve_address_test
	$(OUT_DIR)\resolve_address_test.exe

secure_endpoint_test.exe: grpc_test_util
	echo Building secure_endpoint_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\security\secure_endpoint_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\secure_endpoint_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\secure_endpoint_test.obj 

secure_endpoint_test: secure_endpoint_test.exe
	echo Running secure_endpoint_test
	$(OUT_DIR)\secure_endpoint_test.exe

sockaddr_utils_test.exe: grpc_test_util
	echo Building sockaddr_utils_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\iomgr\sockaddr_utils_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\sockaddr_utils_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\sockaddr_utils_test.obj 

sockaddr_utils_test: sockaddr_utils_test.exe
	echo Running sockaddr_utils_test
	$(OUT_DIR)\sockaddr_utils_test.exe

tcp_client_posix_test.exe: grpc_test_util
	echo Building tcp_client_posix_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\iomgr\tcp_client_posix_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\tcp_client_posix_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\tcp_client_posix_test.obj 

tcp_client_posix_test: tcp_client_posix_test.exe
	echo Running tcp_client_posix_test
	$(OUT_DIR)\tcp_client_posix_test.exe

tcp_posix_test.exe: grpc_test_util
	echo Building tcp_posix_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\iomgr\tcp_posix_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\tcp_posix_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\tcp_posix_test.obj 

tcp_posix_test: tcp_posix_test.exe
	echo Running tcp_posix_test
	$(OUT_DIR)\tcp_posix_test.exe

tcp_server_posix_test.exe: grpc_test_util
	echo Building tcp_server_posix_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\iomgr\tcp_server_posix_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\tcp_server_posix_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\tcp_server_posix_test.obj 

tcp_server_posix_test: tcp_server_posix_test.exe
	echo Running tcp_server_posix_test
	$(OUT_DIR)\tcp_server_posix_test.exe

time_averaged_stats_test.exe: grpc_test_util
	echo Building time_averaged_stats_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\iomgr\time_averaged_stats_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\time_averaged_stats_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\time_averaged_stats_test.obj 

time_averaged_stats_test: time_averaged_stats_test.exe
	echo Running time_averaged_stats_test
	$(OUT_DIR)\time_averaged_stats_test.exe

time_test.exe: grpc_test_util
	echo Building time_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\support\time_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\time_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\time_test.obj 

time_test: time_test.exe
	echo Running time_test
	$(OUT_DIR)\time_test.exe

timeout_encoding_test.exe: grpc_test_util
	echo Building timeout_encoding_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\transport\chttp2\timeout_encoding_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\timeout_encoding_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\timeout_encoding_test.obj 

timeout_encoding_test: timeout_encoding_test.exe
	echo Running timeout_encoding_test
	$(OUT_DIR)\timeout_encoding_test.exe

transport_metadata_test.exe: grpc_test_util
	echo Building transport_metadata_test
	cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:$(OUT_DIR)\ ..\..\test\core\transport\metadata_test.c 
	link.exe /DEBUG /OUT:"$(OUT_DIR)\transport_metadata_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib $(OUT_DIR)\metadata_test.obj 

transport_metadata_test: transport_metadata_test.exe
	echo Running transport_metadata_test
	$(OUT_DIR)\transport_metadata_test.exe

