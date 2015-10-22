rem setlocal
rem call "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" amd64
rem call :build x64 Release v120 || goto :eof
rem call :build x64 Debug v120 || goto :eof
rem endlocal

setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" x86
call :build v120 Win32 Release || goto :eof
call :build v120 Win32 Debug || goto :eof
endlocal

rem setlocal
rem call "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\vcvarsall.bat" amd64
rem call :build x64 Release v110 || goto :eof
rem call :build x64 Debug v110 || goto :eof
rem endlocal

rem setlocal
rem call "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\vcvarsall.bat" x86
rem call :build Win32 Release v110 || goto :eof
rem call :build Win32 Debug v110 || goto :eof
rem endlocal

rem setlocal
rem call "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\vcvarsall.bat" amd64
rem call :build x64 Release v100 || goto :eof
rem call :build x64 Debug v100 || goto :eof
rem endlocal

setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\vcvarsall.bat" x86
call :build v100 Win32 Release || goto :eof
call :build v100 Win32 Debug || goto :eof
endlocal

goto :eof

:build
rem %1=[v100,v120] %2=[Win32] %3=[Release,Debug]
setlocal
@rem TODO(jtattermusch): Debug configuration produces the same results as release config currently.
set CONF_PATH=%1\%2\%3
set ZLIB_INCLUDE="--with-zlib-include=.\COPKG\packages\zlib.1.2.8.7\build\native\include"
set ZLIB_LIB="--with-zlib-lib=.\COPKG\packages\zlib.1.2.8.7\build\native\lib\%CONF_PATH%\dynamic\cdecl\zlib.lib"

set TEMP_DIR=.\COPKG\dest\tmp\%CONF_PATH%
set OUT_DIR=.\COPKG\dest\out\%CONF_PATH%
set INSTALL_DIR=.\COPKG\dest\install\%CONF_PATH%

perl Configure no-asm zlib-dynamic %ZLIB_INCLUDE% %ZLIB_LIB% VC-WIN32 || goto :error
call ".\ms\do_ms.bat" || goto :error

rem Building the static library
nmake -f ms\nt.mak "INSTALLTOP=%INSTALL_DIR%\static" "OPENSSLDIR=%INSTALL_DIR%\static" "TMP_D=%TEMP_DIR%\static" "OUT_D=%OUT_DIR%\static" install || goto :error
rem Building the dynamic library
nmake -f ms\ntdll.mak "INSTALLTOP=%INSTALL_DIR%\dynamic" "OPENSSLDIR=%INSTALL_DIR%\dynamic" "TMP_D=%TEMP_DIR%\dynamic" "OUT_D=%OUT_DIR%\dynamic" install || goto :error
endlocal

goto :eof

:error
echo Failed with error #%errorlevel%.
exit /b %errorlevel%