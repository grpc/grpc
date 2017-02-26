@rem Copyright 2016, gRPC authors
@rem
@rem Licensed under the Apache License, Version 2.0 (the "License");
@rem you may not use this file except in compliance with the License.
@rem You may obtain a copy of the License at
@rem
@rem   http://www.apache.org/licenses/LICENSE-2.0
@rem
@rem Unless required by applicable law or agreed to in writing, software
@rem distributed under the License is distributed on an "AS IS" BASIS,
@rem WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
@rem See the License for the specific language governing permissions and
@rem limitations under the License.

@rem Current package versions
set VERSION=1.2.0-dev
set PROTOBUF_VERSION=3.0.0

@rem Adjust the location of nuget.exe
set NUGET=C:\nuget\nuget.exe
set DOTNET=C:\dotnet\dotnet.exe

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

%DOTNET% restore . || goto :error

%DOTNET% pack --configuration Release Grpc.Core\project.json --output ..\..\artifacts || goto :error
%DOTNET% pack --configuration Release Grpc.Core.Testing\project.json --output ..\..\artifacts || goto :error
%DOTNET% pack --configuration Release Grpc.Auth\project.json --output ..\..\artifacts || goto :error
%DOTNET% pack --configuration Release Grpc.HealthCheck\project.json --output ..\..\artifacts || goto :error
%DOTNET% pack --configuration Release Grpc.Reflection\project.json --output ..\..\artifacts || goto :error

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
