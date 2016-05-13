@rem Copyright 2016, Google Inc.
@rem All rights reserved.
@rem
@rem Redistribution and use in source and binary forms, with or without
@rem modification, are permitted provided that the following conditions are
@rem met:
@rem
@rem     * Redistributions of source code must retain the above copyright
@rem notice, this list of conditions and the following disclaimer.
@rem     * Redistributions in binary form must reproduce the above
@rem copyright notice, this list of conditions and the following disclaimer
@rem in the documentation and/or other materials provided with the
@rem distribution.
@rem     * Neither the name of Google Inc. nor the names of its
@rem contributors may be used to endorse or promote products derived from
@rem this software without specific prior written permission.
@rem
@rem THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
@rem "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
@rem LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
@rem A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
@rem OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
@rem SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
@rem LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
@rem DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
@rem THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
@rem (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
@rem OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

@echo off
setlocal

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

setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" amd64
call :build x64 Release v140 || goto :eof
call :build x64 Debug v140 || goto :eof
endlocal

setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x86
call :build Win32 Release v140 || goto :eof
call :build Win32 Debug v140 || goto :eof
endlocal

setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" amd64
call :build x64 Release v120 || goto :eof
call :build x64 Debug v120 || goto :eof
endlocal

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

goto :eof

:build
msbuild /m:4 /P:Platform=%1 /P:Configuration=%2 /P:PlatformToolset=%3 /P:UsesConfigurationType=Dynamic /P:CallingConvention=cdecl .\zlib.sln || goto :eof
msbuild /m:4 /P:Platform=%1 /P:Configuration=%2 /P:PlatformToolset=%3 /P:UsesConfigurationType=Dynamic /P:CallingConvention=stdcall .\zlib.sln || goto :eof
msbuild /m:4 /P:Platform=%1 /P:Configuration=%2 /P:PlatformToolset=%3 /P:UsesConfigurationType=Static /P:CallingConvention=cdecl .\zlib.sln || goto :eof
msbuild /m:4 /P:Platform=%1 /P:Configuration=%2 /P:PlatformToolset=%3 /P:UsesConfigurationType=Static /P:CallingConvention=stdcall .\zlib.sln || goto :eof
msbuild /m:4 /P:Platform=%1 /P:Configuration=%2 /P:PlatformToolset=%3 /P:UsesConfigurationType=ltcg /P:CallingConvention=cdecl .\zlib.sln || goto :eof
msbuild /m:4 /P:Platform=%1 /P:Configuration=%2 /P:PlatformToolset=%3 /P:UsesConfigurationType=ltcg /P:CallingConvention=stdcall .\zlib.sln || goto :eof
goto :eof


