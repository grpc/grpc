
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
msbuild /P:Platform=%1 /P:Configuration=%2 /P:PlatformToolset=%3 /P:UsesConfigurationType=dynamic /P:ConfigurationType=DynamicLibrary .\openssl.sln || goto :eof
msbuild /P:Platform=%1 /P:Configuration=%2 /P:PlatformToolset=%3 /P:UsesConfigurationType=static /P:ConfigurationType=StaticLibrary .\openssl.sln || goto :eof
goto :eof


