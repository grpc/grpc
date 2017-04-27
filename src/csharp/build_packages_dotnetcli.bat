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

@rem Current package versions
set VERSION=1.3.0

@rem Adjust the location of nuget.exe
set NUGET=C:\nuget\nuget.exe
set DOTNET=dotnet

set -ex

mkdir -p ..\..\artifacts\

@rem Collect the artifacts built by the previous build step if running on Jenkins
@rem TODO(jtattermusch): is there a better way to do this?
xcopy /Y /I ..\..\architecture=x86,language=csharp,platform=windows\artifacts\* nativelibs\windows_x86\
xcopy /Y /I ..\..\architecture=x64,language=csharp,platform=windows\artifacts\* nativelibs\windows_x64\
xcopy /Y /I ..\..\architecture=x86,language=csharp,platform=linux\artifacts\* nativelibs\linux_x86\
xcopy /Y /I ..\..\architecture=x64,language=csharp,platform=linux\artifacts\* nativelibs\linux_x64\
xcopy /Y /I ..\..\architecture=x86,language=csharp,platform=macos\artifacts\* nativelibs\macosx_x86\
xcopy /Y /I ..\..\architecture=x64,language=csharp,platform=macos\artifacts\* nativelibs\macosx_x64\

@rem Collect protoc artifacts built by the previous build step
xcopy /Y /I ..\..\architecture=x86,language=protoc,platform=windows\artifacts\* protoc_plugins\windows_x86\
xcopy /Y /I ..\..\architecture=x64,language=protoc,platform=windows\artifacts\* protoc_plugins\windows_x64\
xcopy /Y /I ..\..\architecture=x86,language=protoc,platform=linux\artifacts\* protoc_plugins\linux_x86\
xcopy /Y /I ..\..\architecture=x64,language=protoc,platform=linux\artifacts\* protoc_plugins\linux_x64\
xcopy /Y /I ..\..\architecture=x86,language=protoc,platform=macos\artifacts\* protoc_plugins\macosx_x86\
xcopy /Y /I ..\..\architecture=x64,language=protoc,platform=macos\artifacts\* protoc_plugins\macosx_x64\

%DOTNET% restore Grpc.sln || goto :error

@rem To be able to build, we also need to put grpc_csharp_ext to its normal location
xcopy /Y /I nativelibs\windows_x64\grpc_csharp_ext.dll ..\..\cmake\build\x64\Release\

%DOTNET% pack --configuration Release Grpc.Core --output ..\..\..\artifacts || goto :error
%DOTNET% pack --configuration Release Grpc.Core.Testing --output ..\..\..\artifacts || goto :error
%DOTNET% pack --configuration Release Grpc.Auth --output ..\..\..\artifacts || goto :error
%DOTNET% pack --configuration Release Grpc.HealthCheck --output ..\..\..\artifacts || goto :error
%DOTNET% pack --configuration Release Grpc.Reflection --output ..\..\..\artifacts || goto :error

%NUGET% pack Grpc.nuspec -Version %VERSION% -OutputDirectory ..\..\artifacts || goto :error
%NUGET% pack Grpc.Tools.nuspec -Version %VERSION% -OutputDirectory ..\..\artifacts

@rem copy resulting nuget packages to artifacts directory
xcopy /Y /I *.nupkg ..\..\artifacts\ || goto :error

@rem create a zipfile with the artifacts as well
powershell -Command "Add-Type -Assembly 'System.IO.Compression.FileSystem'; [System.IO.Compression.ZipFile]::CreateFromDirectory('..\..\artifacts', 'csharp_nugets_windows_dotnetcli.zip');"
xcopy /Y /I csharp_nugets_windows_dotnetcli.zip ..\..\artifacts\ || goto :error

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
