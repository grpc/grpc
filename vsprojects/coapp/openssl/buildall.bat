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

rem Restore using NuGet dependencies (Download NuGet from nuget.org and put it in this directory first)
nuget restore  || goto eof:


setlocal
rem First do a bit of hacking to make sure we have headers ready in openssl's inc32 directory
cd ..\..\..\third_party\openssl
call "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" x86
perl Configure no-asm VC-WIN32 || goto :eof
perl util\mkfiles.pl >MINFO || goto :eof
perl util\mk1mf.pl no-asm VC-WIN32 >ms\nt.mak || goto :eof
mkdir inc32\openssl
mkdir tmp32
nmake -f ms\nt.mak headers || goto :eof
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
call :build Win32 Release v100 || goto :eof
call :build Win32 Debug v100 || goto :eof
endlocal

:build
msbuild /m:4 /P:Platform=%1 /P:Configuration=%2 /P:PlatformToolset=%3 /P:UsesConfigurationType=dynamic /P:ConfigurationType=DynamicLibrary .\openssl.sln || goto :eof
msbuild /m:4 /P:Platform=%1 /P:Configuration=%2 /P:PlatformToolset=%3 /P:UsesConfigurationType=static /P:ConfigurationType=StaticLibrary .\openssl.sln || goto :eof
goto :eof


