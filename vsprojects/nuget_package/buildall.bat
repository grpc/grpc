@echo off

REM setlocal
REM call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" amd64
REM call :build x64 Release v140 || goto :eof
REM call :build x64 Debug v140 || goto :eof
REM endlocal

setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x86
call :build Win32 Release v140 || goto :eof
call :build Win32 Debug v140 || goto :eof
endlocal

REM setlocal
REM call "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" amd64
REM call :build x64 Release v120 || goto :eof
REM call :build x64 Debug v120 || goto :eof
REM endlocal

setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" x86
call :build Win32 Release v120 || goto :eof
call :build Win32 Debug v120 || goto :eof
endlocal

REM setlocal
REM call "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\vcvarsall.bat" amd64
REM call :build x64 Release v110 || goto :eof
REM call :build x64 Debug v110 || goto :eof
REM endlocal

REM setlocal
REM call "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\vcvarsall.bat" x86
REM call :build Win32 Release v110 || goto :eof
REM call :build Win32 Debug v110 || goto :eof
REM endlocal

REM setlocal
REM call "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\vcvarsall.bat" amd64
REM call :build x64 Release v100 || goto :eof
REM call :build x64 Debug v100 || goto :eof
REM endlocal

setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\vcvarsall.bat" x86
call :build Win32 Release v100 || goto :eof
call :build Win32 Debug v100 || goto :eof
endlocal

goto :eof

:build
msbuild /P:Platform=%1 /P:Configuration=%2 /P:PlatformToolset=%3 /P:OutDir=..\..\nuget_package\output\%3\%1\%2\ /P:IntDir=..\..\nuget_package\tmp\%3\%1\%2\ ..\grpc_csharp_ext.sln || goto :eof
goto :eof


