@rem Build and runs unit all unit tests

@rem Set VS variables
@call "%VS120COMNTOOLS%\..\..\vc\vcvarsall.bat" x86

@rem Build the library dependencies first
MSBuild.exe gpr.vcxproj /p:Configuration=Debug
MSBuild.exe gpr_test_util.vcxproj /p:Configuration=Debug
MSBuild.exe grpc.vcxproj /p:Configuration=Debug
MSBuild.exe gprc_test_util.vcxproj /p:Configuration=Debug

mkdir test_bin

echo Building test alarm_heap_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\iomgr\alarm_heap_test.c 
link.exe /DEBUG /OUT:"test_bin\alarm_heap_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\alarm_heap_test.obj 
echo(
echo Running test alarm_heap_test
test_bin\alarm_heap_test.exe || echo TEST FAILED: alarm_heap_test && exit /b
echo(

echo Building test alarm_list_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\iomgr\alarm_list_test.c 
link.exe /DEBUG /OUT:"test_bin\alarm_list_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\alarm_list_test.obj 
echo(
echo Running test alarm_list_test
test_bin\alarm_list_test.exe || echo TEST FAILED: alarm_list_test && exit /b
echo(

echo Building test alarm_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\iomgr\alarm_test.c 
link.exe /DEBUG /OUT:"test_bin\alarm_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\alarm_test.obj 
echo(
echo Running test alarm_test
test_bin\alarm_test.exe || echo TEST FAILED: alarm_test && exit /b
echo(

echo Building test alpn_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\transport\chttp2\alpn_test.c 
link.exe /DEBUG /OUT:"test_bin\alpn_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\alpn_test.obj 
echo(
echo Running test alpn_test
test_bin\alpn_test.exe || echo TEST FAILED: alpn_test && exit /b
echo(

echo Building test bin_encoder_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\transport\chttp2\bin_encoder_test.c 
link.exe /DEBUG /OUT:"test_bin\bin_encoder_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\bin_encoder_test.obj 
echo(
echo Running test bin_encoder_test
test_bin\bin_encoder_test.exe || echo TEST FAILED: bin_encoder_test && exit /b
echo(

echo Building test census_hash_table_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\statistics\hash_table_test.c 
link.exe /DEBUG /OUT:"test_bin\census_hash_table_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\hash_table_test.obj 
echo(
echo Running test census_hash_table_test
test_bin\census_hash_table_test.exe || echo TEST FAILED: census_hash_table_test && exit /b
echo(

echo Building test census_statistics_multiple_writers_circular_buffer_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\statistics\multiple_writers_circular_buffer_test.c 
link.exe /DEBUG /OUT:"test_bin\census_statistics_multiple_writers_circular_buffer_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\multiple_writers_circular_buffer_test.obj 
echo(
echo Running test census_statistics_multiple_writers_circular_buffer_test
test_bin\census_statistics_multiple_writers_circular_buffer_test.exe || echo TEST FAILED: census_statistics_multiple_writers_circular_buffer_test && exit /b
echo(

echo Building test census_statistics_multiple_writers_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\statistics\multiple_writers_test.c 
link.exe /DEBUG /OUT:"test_bin\census_statistics_multiple_writers_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\multiple_writers_test.obj 
echo(
echo Running test census_statistics_multiple_writers_test
test_bin\census_statistics_multiple_writers_test.exe || echo TEST FAILED: census_statistics_multiple_writers_test && exit /b
echo(

echo Building test census_statistics_performance_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\statistics\performance_test.c 
link.exe /DEBUG /OUT:"test_bin\census_statistics_performance_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\performance_test.obj 
echo(
echo Running test census_statistics_performance_test
test_bin\census_statistics_performance_test.exe || echo TEST FAILED: census_statistics_performance_test && exit /b
echo(

echo Building test census_statistics_quick_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\statistics\quick_test.c 
link.exe /DEBUG /OUT:"test_bin\census_statistics_quick_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\quick_test.obj 
echo(
echo Running test census_statistics_quick_test
test_bin\census_statistics_quick_test.exe || echo TEST FAILED: census_statistics_quick_test && exit /b
echo(

echo Building test census_statistics_small_log_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\statistics\small_log_test.c 
link.exe /DEBUG /OUT:"test_bin\census_statistics_small_log_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\small_log_test.obj 
echo(
echo Running test census_statistics_small_log_test
test_bin\census_statistics_small_log_test.exe || echo TEST FAILED: census_statistics_small_log_test && exit /b
echo(

echo Building test census_stats_store_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\statistics\rpc_stats_test.c 
link.exe /DEBUG /OUT:"test_bin\census_stats_store_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\rpc_stats_test.obj 
echo(
echo Running test census_stats_store_test
test_bin\census_stats_store_test.exe || echo TEST FAILED: census_stats_store_test && exit /b
echo(

echo Building test census_stub_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\statistics\census_stub_test.c 
link.exe /DEBUG /OUT:"test_bin\census_stub_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\census_stub_test.obj 
echo(
echo Running test census_stub_test
test_bin\census_stub_test.exe || echo TEST FAILED: census_stub_test && exit /b
echo(

echo Building test census_trace_store_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\statistics\trace_test.c 
link.exe /DEBUG /OUT:"test_bin\census_trace_store_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\trace_test.obj 
echo(
echo Running test census_trace_store_test
test_bin\census_trace_store_test.exe || echo TEST FAILED: census_trace_store_test && exit /b
echo(

echo Building test census_window_stats_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\statistics\window_stats_test.c 
link.exe /DEBUG /OUT:"test_bin\census_window_stats_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\window_stats_test.obj 
echo(
echo Running test census_window_stats_test
test_bin\census_window_stats_test.exe || echo TEST FAILED: census_window_stats_test && exit /b
echo(

echo Building test chttp2_status_conversion_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\transport\chttp2\status_conversion_test.c 
link.exe /DEBUG /OUT:"test_bin\chttp2_status_conversion_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\status_conversion_test.obj 
echo(
echo Running test chttp2_status_conversion_test
test_bin\chttp2_status_conversion_test.exe || echo TEST FAILED: chttp2_status_conversion_test && exit /b
echo(

echo Building test chttp2_stream_encoder_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\transport\chttp2\stream_encoder_test.c 
link.exe /DEBUG /OUT:"test_bin\chttp2_stream_encoder_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\stream_encoder_test.obj 
echo(
echo Running test chttp2_stream_encoder_test
test_bin\chttp2_stream_encoder_test.exe || echo TEST FAILED: chttp2_stream_encoder_test && exit /b
echo(

echo Building test chttp2_stream_map_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\transport\chttp2\stream_map_test.c 
link.exe /DEBUG /OUT:"test_bin\chttp2_stream_map_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\stream_map_test.obj 
echo(
echo Running test chttp2_stream_map_test
test_bin\chttp2_stream_map_test.exe || echo TEST FAILED: chttp2_stream_map_test && exit /b
echo(

echo Building test chttp2_transport_end2end_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\transport\chttp2_transport_end2end_test.c 
link.exe /DEBUG /OUT:"test_bin\chttp2_transport_end2end_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\chttp2_transport_end2end_test.obj 
echo(
echo Running test chttp2_transport_end2end_test
test_bin\chttp2_transport_end2end_test.exe || echo TEST FAILED: chttp2_transport_end2end_test && exit /b
echo(

echo Building test dualstack_socket_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\end2end\dualstack_socket_test.c 
link.exe /DEBUG /OUT:"test_bin\dualstack_socket_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\dualstack_socket_test.obj 
echo(
echo Running test dualstack_socket_test
test_bin\dualstack_socket_test.exe || echo TEST FAILED: dualstack_socket_test && exit /b
echo(

echo Building test echo_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\echo\echo_test.c 
link.exe /DEBUG /OUT:"test_bin\echo_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\echo_test.obj 
echo(
echo Running test echo_test
test_bin\echo_test.exe || echo TEST FAILED: echo_test && exit /b
echo(

echo Building test fd_posix_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\iomgr\fd_posix_test.c 
link.exe /DEBUG /OUT:"test_bin\fd_posix_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\fd_posix_test.obj 
echo(
echo Running test fd_posix_test
test_bin\fd_posix_test.exe || echo TEST FAILED: fd_posix_test && exit /b
echo(

echo Building test fling_stream_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\fling\fling_stream_test.c 
link.exe /DEBUG /OUT:"test_bin\fling_stream_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\fling_stream_test.obj 
echo(
echo Running test fling_stream_test
test_bin\fling_stream_test.exe || echo TEST FAILED: fling_stream_test && exit /b
echo(

echo Building test fling_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\fling\fling_test.c 
link.exe /DEBUG /OUT:"test_bin\fling_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\fling_test.obj 
echo(
echo Running test fling_test
test_bin\fling_test.exe || echo TEST FAILED: fling_test && exit /b
echo(

echo Building test gpr_cancellable_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\support\cancellable_test.c 
link.exe /DEBUG /OUT:"test_bin\gpr_cancellable_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib test_bin\cancellable_test.obj 
echo(
echo Running test gpr_cancellable_test
test_bin\gpr_cancellable_test.exe || echo TEST FAILED: gpr_cancellable_test && exit /b
echo(

echo Building test gpr_cmdline_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\support\cmdline_test.c 
link.exe /DEBUG /OUT:"test_bin\gpr_cmdline_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib test_bin\cmdline_test.obj 
echo(
echo Running test gpr_cmdline_test
test_bin\gpr_cmdline_test.exe || echo TEST FAILED: gpr_cmdline_test && exit /b
echo(

echo Building test gpr_env_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\support\env_test.c 
link.exe /DEBUG /OUT:"test_bin\gpr_env_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib test_bin\env_test.obj 
echo(
echo Running test gpr_env_test
test_bin\gpr_env_test.exe || echo TEST FAILED: gpr_env_test && exit /b
echo(

echo Building test gpr_file_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\support\file_test.c 
link.exe /DEBUG /OUT:"test_bin\gpr_file_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib test_bin\file_test.obj 
echo(
echo Running test gpr_file_test
test_bin\gpr_file_test.exe || echo TEST FAILED: gpr_file_test && exit /b
echo(

echo Building test gpr_histogram_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\support\histogram_test.c 
link.exe /DEBUG /OUT:"test_bin\gpr_histogram_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib test_bin\histogram_test.obj 
echo(
echo Running test gpr_histogram_test
test_bin\gpr_histogram_test.exe || echo TEST FAILED: gpr_histogram_test && exit /b
echo(

echo Building test gpr_host_port_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\support\host_port_test.c 
link.exe /DEBUG /OUT:"test_bin\gpr_host_port_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib test_bin\host_port_test.obj 
echo(
echo Running test gpr_host_port_test
test_bin\gpr_host_port_test.exe || echo TEST FAILED: gpr_host_port_test && exit /b
echo(

echo Building test gpr_log_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\support\log_test.c 
link.exe /DEBUG /OUT:"test_bin\gpr_log_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib test_bin\log_test.obj 
echo(
echo Running test gpr_log_test
test_bin\gpr_log_test.exe || echo TEST FAILED: gpr_log_test && exit /b
echo(

echo Building test gpr_slice_buffer_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\support\slice_buffer_test.c 
link.exe /DEBUG /OUT:"test_bin\gpr_slice_buffer_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib test_bin\slice_buffer_test.obj 
echo(
echo Running test gpr_slice_buffer_test
test_bin\gpr_slice_buffer_test.exe || echo TEST FAILED: gpr_slice_buffer_test && exit /b
echo(

echo Building test gpr_slice_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\support\slice_test.c 
link.exe /DEBUG /OUT:"test_bin\gpr_slice_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib test_bin\slice_test.obj 
echo(
echo Running test gpr_slice_test
test_bin\gpr_slice_test.exe || echo TEST FAILED: gpr_slice_test && exit /b
echo(

echo Building test gpr_string_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\support\string_test.c 
link.exe /DEBUG /OUT:"test_bin\gpr_string_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib test_bin\string_test.obj 
echo(
echo Running test gpr_string_test
test_bin\gpr_string_test.exe || echo TEST FAILED: gpr_string_test && exit /b
echo(

echo Building test gpr_sync_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\support\sync_test.c 
link.exe /DEBUG /OUT:"test_bin\gpr_sync_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib test_bin\sync_test.obj 
echo(
echo Running test gpr_sync_test
test_bin\gpr_sync_test.exe || echo TEST FAILED: gpr_sync_test && exit /b
echo(

echo Building test gpr_thd_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\support\thd_test.c 
link.exe /DEBUG /OUT:"test_bin\gpr_thd_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib test_bin\thd_test.obj 
echo(
echo Running test gpr_thd_test
test_bin\gpr_thd_test.exe || echo TEST FAILED: gpr_thd_test && exit /b
echo(

echo Building test gpr_time_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\support\time_test.c 
link.exe /DEBUG /OUT:"test_bin\gpr_time_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib test_bin\time_test.obj 
echo(
echo Running test gpr_time_test
test_bin\gpr_time_test.exe || echo TEST FAILED: gpr_time_test && exit /b
echo(

echo Building test gpr_useful_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\support\useful_test.c 
link.exe /DEBUG /OUT:"test_bin\gpr_useful_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib test_bin\useful_test.obj 
echo(
echo Running test gpr_useful_test
test_bin\gpr_useful_test.exe || echo TEST FAILED: gpr_useful_test && exit /b
echo(

echo Building test grpc_base64_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\security\base64_test.c 
link.exe /DEBUG /OUT:"test_bin\grpc_base64_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\base64_test.obj 
echo(
echo Running test grpc_base64_test
test_bin\grpc_base64_test.exe || echo TEST FAILED: grpc_base64_test && exit /b
echo(

echo Building test grpc_byte_buffer_reader_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\surface\byte_buffer_reader_test.c 
link.exe /DEBUG /OUT:"test_bin\grpc_byte_buffer_reader_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\byte_buffer_reader_test.obj 
echo(
echo Running test grpc_byte_buffer_reader_test
test_bin\grpc_byte_buffer_reader_test.exe || echo TEST FAILED: grpc_byte_buffer_reader_test && exit /b
echo(

echo Building test grpc_channel_stack_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\channel\channel_stack_test.c 
link.exe /DEBUG /OUT:"test_bin\grpc_channel_stack_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\channel_stack_test.obj 
echo(
echo Running test grpc_channel_stack_test
test_bin\grpc_channel_stack_test.exe || echo TEST FAILED: grpc_channel_stack_test && exit /b
echo(

echo Building test grpc_completion_queue_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\surface\completion_queue_test.c 
link.exe /DEBUG /OUT:"test_bin\grpc_completion_queue_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\completion_queue_test.obj 
echo(
echo Running test grpc_completion_queue_test
test_bin\grpc_completion_queue_test.exe || echo TEST FAILED: grpc_completion_queue_test && exit /b
echo(

echo Building test grpc_credentials_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\security\credentials_test.c 
link.exe /DEBUG /OUT:"test_bin\grpc_credentials_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\credentials_test.obj 
echo(
echo Running test grpc_credentials_test
test_bin\grpc_credentials_test.exe || echo TEST FAILED: grpc_credentials_test && exit /b
echo(

echo Building test grpc_json_token_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\security\json_token_test.c 
link.exe /DEBUG /OUT:"test_bin\grpc_json_token_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\json_token_test.obj 
echo(
echo Running test grpc_json_token_test
test_bin\grpc_json_token_test.exe || echo TEST FAILED: grpc_json_token_test && exit /b
echo(

echo Building test grpc_stream_op_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\transport\stream_op_test.c 
link.exe /DEBUG /OUT:"test_bin\grpc_stream_op_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\stream_op_test.obj 
echo(
echo Running test grpc_stream_op_test
test_bin\grpc_stream_op_test.exe || echo TEST FAILED: grpc_stream_op_test && exit /b
echo(

echo Building test hpack_parser_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\transport\chttp2\hpack_parser_test.c 
link.exe /DEBUG /OUT:"test_bin\hpack_parser_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\hpack_parser_test.obj 
echo(
echo Running test hpack_parser_test
test_bin\hpack_parser_test.exe || echo TEST FAILED: hpack_parser_test && exit /b
echo(

echo Building test hpack_table_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\transport\chttp2\hpack_table_test.c 
link.exe /DEBUG /OUT:"test_bin\hpack_table_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\hpack_table_test.obj 
echo(
echo Running test hpack_table_test
test_bin\hpack_table_test.exe || echo TEST FAILED: hpack_table_test && exit /b
echo(

echo Building test httpcli_format_request_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\httpcli\format_request_test.c 
link.exe /DEBUG /OUT:"test_bin\httpcli_format_request_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\format_request_test.obj 
echo(
echo Running test httpcli_format_request_test
test_bin\httpcli_format_request_test.exe || echo TEST FAILED: httpcli_format_request_test && exit /b
echo(

echo Building test httpcli_parser_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\httpcli\parser_test.c 
link.exe /DEBUG /OUT:"test_bin\httpcli_parser_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\parser_test.obj 
echo(
echo Running test httpcli_parser_test
test_bin\httpcli_parser_test.exe || echo TEST FAILED: httpcli_parser_test && exit /b
echo(

echo Building test httpcli_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\httpcli\httpcli_test.c 
link.exe /DEBUG /OUT:"test_bin\httpcli_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\httpcli_test.obj 
echo(
echo Running test httpcli_test
test_bin\httpcli_test.exe || echo TEST FAILED: httpcli_test && exit /b
echo(

echo Building test json_rewrite_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\json\json_rewrite_test.c 
link.exe /DEBUG /OUT:"test_bin\json_rewrite_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\json_rewrite_test.obj 
echo(
echo Running test json_rewrite_test
test_bin\json_rewrite_test.exe || echo TEST FAILED: json_rewrite_test && exit /b
echo(

echo Building test json_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\json\json_test.c 
link.exe /DEBUG /OUT:"test_bin\json_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\json_test.obj 
echo(
echo Running test json_test
test_bin\json_test.exe || echo TEST FAILED: json_test && exit /b
echo(

echo Building test lame_client_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\surface\lame_client_test.c 
link.exe /DEBUG /OUT:"test_bin\lame_client_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\lame_client_test.obj 
echo(
echo Running test lame_client_test
test_bin\lame_client_test.exe || echo TEST FAILED: lame_client_test && exit /b
echo(

echo Building test message_compress_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\compression\message_compress_test.c 
link.exe /DEBUG /OUT:"test_bin\message_compress_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\message_compress_test.obj 
echo(
echo Running test message_compress_test
test_bin\message_compress_test.exe || echo TEST FAILED: message_compress_test && exit /b
echo(

echo Building test metadata_buffer_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\channel\metadata_buffer_test.c 
link.exe /DEBUG /OUT:"test_bin\metadata_buffer_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\metadata_buffer_test.obj 
echo(
echo Running test metadata_buffer_test
test_bin\metadata_buffer_test.exe || echo TEST FAILED: metadata_buffer_test && exit /b
echo(

echo Building test murmur_hash_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\support\murmur_hash_test.c 
link.exe /DEBUG /OUT:"test_bin\murmur_hash_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\gpr_test_util.lib Debug\gpr.lib test_bin\murmur_hash_test.obj 
echo(
echo Running test murmur_hash_test
test_bin\murmur_hash_test.exe || echo TEST FAILED: murmur_hash_test && exit /b
echo(

echo Building test no_server_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\end2end\no_server_test.c 
link.exe /DEBUG /OUT:"test_bin\no_server_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\no_server_test.obj 
echo(
echo Running test no_server_test
test_bin\no_server_test.exe || echo TEST FAILED: no_server_test && exit /b
echo(

echo Building test poll_kick_posix_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\iomgr\poll_kick_posix_test.c 
link.exe /DEBUG /OUT:"test_bin\poll_kick_posix_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\poll_kick_posix_test.obj 
echo(
echo Running test poll_kick_posix_test
test_bin\poll_kick_posix_test.exe || echo TEST FAILED: poll_kick_posix_test && exit /b
echo(

echo Building test resolve_address_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\iomgr\resolve_address_test.c 
link.exe /DEBUG /OUT:"test_bin\resolve_address_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\resolve_address_test.obj 
echo(
echo Running test resolve_address_test
test_bin\resolve_address_test.exe || echo TEST FAILED: resolve_address_test && exit /b
echo(

echo Building test secure_endpoint_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\security\secure_endpoint_test.c 
link.exe /DEBUG /OUT:"test_bin\secure_endpoint_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\secure_endpoint_test.obj 
echo(
echo Running test secure_endpoint_test
test_bin\secure_endpoint_test.exe || echo TEST FAILED: secure_endpoint_test && exit /b
echo(

echo Building test sockaddr_utils_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\iomgr\sockaddr_utils_test.c 
link.exe /DEBUG /OUT:"test_bin\sockaddr_utils_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\sockaddr_utils_test.obj 
echo(
echo Running test sockaddr_utils_test
test_bin\sockaddr_utils_test.exe || echo TEST FAILED: sockaddr_utils_test && exit /b
echo(

echo Building test tcp_client_posix_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\iomgr\tcp_client_posix_test.c 
link.exe /DEBUG /OUT:"test_bin\tcp_client_posix_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\tcp_client_posix_test.obj 
echo(
echo Running test tcp_client_posix_test
test_bin\tcp_client_posix_test.exe || echo TEST FAILED: tcp_client_posix_test && exit /b
echo(

echo Building test tcp_posix_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\iomgr\tcp_posix_test.c 
link.exe /DEBUG /OUT:"test_bin\tcp_posix_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\tcp_posix_test.obj 
echo(
echo Running test tcp_posix_test
test_bin\tcp_posix_test.exe || echo TEST FAILED: tcp_posix_test && exit /b
echo(

echo Building test tcp_server_posix_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\iomgr\tcp_server_posix_test.c 
link.exe /DEBUG /OUT:"test_bin\tcp_server_posix_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\tcp_server_posix_test.obj 
echo(
echo Running test tcp_server_posix_test
test_bin\tcp_server_posix_test.exe || echo TEST FAILED: tcp_server_posix_test && exit /b
echo(

echo Building test time_averaged_stats_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\iomgr\time_averaged_stats_test.c 
link.exe /DEBUG /OUT:"test_bin\time_averaged_stats_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\time_averaged_stats_test.obj 
echo(
echo Running test time_averaged_stats_test
test_bin\time_averaged_stats_test.exe || echo TEST FAILED: time_averaged_stats_test && exit /b
echo(

echo Building test time_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\support\time_test.c 
link.exe /DEBUG /OUT:"test_bin\time_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\time_test.obj 
echo(
echo Running test time_test
test_bin\time_test.exe || echo TEST FAILED: time_test && exit /b
echo(

echo Building test timeout_encoding_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\transport\chttp2\timeout_encoding_test.c 
link.exe /DEBUG /OUT:"test_bin\timeout_encoding_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\timeout_encoding_test.obj 
echo(
echo Running test timeout_encoding_test
test_bin\timeout_encoding_test.exe || echo TEST FAILED: timeout_encoding_test && exit /b
echo(

echo Building test transport_metadata_test
cl.exe /c /I..\.. /I..\..\include /nologo /Z7 /W3 /WX- /sdl /D WIN32 /D _LIB /D _USE_32BIT_TIME_T /D _UNICODE /D UNICODE /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TC /analyze- /Fo:test_bin\ ..\..\test\core\transport\metadata_test.c 
link.exe /DEBUG /OUT:"test_bin\transport_metadata_test.exe" /INCREMENTAL /NOLOGO /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /MACHINE:X86 Debug\grpc_test_util.lib Debug\grpc.lib Debug\gpr_test_util.lib Debug\gpr.lib test_bin\metadata_test.obj 
echo(
echo Running test transport_metadata_test
test_bin\transport_metadata_test.exe || echo TEST FAILED: transport_metadata_test && exit /b
echo(

