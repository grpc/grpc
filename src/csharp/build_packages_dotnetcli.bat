@rem Copyright 2016 gRPC authors.
@rem
@rem Licensed under the Apache License, Version 2.0 (the "License");
@rem you may not use this file except in compliance with the License.
@rem You may obtain a copy of the License at
@rem
@rem     http://www.apache.org/licenses/LICENSE-2.0
@rem
@rem Unless required by applicable law or agreed to in writing, software
@rem distributed under the License is distributed on an "AS IS" BASIS,
@rem WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
@rem See the License for the specific language governing permissions and
@rem limitations under the License.

@rem Current package versions
set VERSION=1.12.0

@rem Adjust the location of nuget.exe
set NUGET=C:\nuget\nuget.exe
set DOTNET=dotnet

mkdir ..\..\artifacts

@rem Collect the artifacts built by the previous build step if running on Jenkins
mkdir nativelibs
@rem Jenkins flow (deprecated)
powershell -Command "cp -r ..\..\platform=*\artifacts\csharp_ext_* nativelibs"
@rem Kokoro flow
powershell -Command "cp -r ..\..\input_artifacts\csharp_ext_* nativelibs"

@rem Collect protoc artifacts built by the previous build step
mkdir protoc_plugins
@rem Jenkins flow (deprecated)
powershell -Command "cp -r ..\..\platform=*\artifacts\protoc_* protoc_plugins"
@rem Kokoro flow
powershell -Command "cp -r ..\..\input_artifacts\protoc_* protoc_plugins"

%DOTNET% restore Grpc.sln || goto :error

@rem To be able to build, we also need to put grpc_csharp_ext to its normal location
xcopy /Y /I nativelibs\csharp_ext_windows_x64\grpc_csharp_ext.dll ..\..\cmake\build\x64\Release\

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
