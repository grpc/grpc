@rem Build and runs unit all unit tests

@rem Set VS variables
@call "%VS120COMNTOOLS%\..\..\vc\vcvarsall.bat" x86

@rem Build the library dependencies first
MSBuild.exe gpr.vcxproj /p:Configuration=Debug
MSBuild.exe gpr_test_util.vcxproj /p:Configuration=Debug

mkdir test_bin

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

