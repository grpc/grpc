@rem Copyright 2018 The gRPC Authors
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
set VERSION=1.16.0-dev

@rem Adjust the location of nuget.exe
set NUGET=C:\nuget\nuget.exe
set DOTNET=dotnet

mkdir ..\..\artifacts

@rem Collect the artifacts built by the previous build step
mkdir nativelibs
powershell -Command "cp -r ..\..\input_artifacts\csharp_ext_* nativelibs"

@rem Collect protoc artifacts built by the previous build step
mkdir protoc_plugins
powershell -Command "cp -r ..\..\input_artifacts\protoc_* protoc_plugins"

%DOTNET% restore Grpc.sln || goto :error

@rem To be able to build, we also need to put grpc_csharp_ext to its normal location
xcopy /Y /I nativelibs\csharp_ext_windows_x64\grpc_csharp_ext.dll ..\..\cmake\build\x64\Release\

%DOTNET% build --configuration Release Grpc.Core || goto :error
@rem build HealthCheck to get hold of Google.Protobuf.dll assembly
%DOTNET% build --configuration Release Grpc.HealthCheck || goto :error

@rem copy Grpc assemblies to the unity package skeleton
@rem TODO(jtattermusch): Add Grpc.Auth assembly and its dependencies
copy /Y Grpc.Core\bin\Release\net45\Grpc.Core.dll unitypackage\unitypackage_skeleton\Plugins\Grpc.Core\lib\net45\Grpc.Core.dll || goto :error
copy /Y Grpc.Core\bin\Release\net45\Grpc.Core.pdb unitypackage\unitypackage_skeleton\Plugins\Grpc.Core\lib\net45\Grpc.Core.pdb || goto :error
copy /Y Grpc.Core\bin\Release\net45\Grpc.Core.xml unitypackage\unitypackage_skeleton\Plugins\Grpc.Core\lib\net45\Grpc.Core.xml || goto :error

@rem copy desktop native libraries to the unity package skeleton
copy /Y nativelibs\csharp_ext_linux_x86\libgrpc_csharp_ext.so unitypackage\unitypackage_skeleton\Plugins\Grpc.Core\runtimes\linux\x86\libgrpc_csharp_ext.so || goto :error
copy /Y nativelibs\csharp_ext_linux_x64\libgrpc_csharp_ext.so unitypackage\unitypackage_skeleton\Plugins\Grpc.Core\runtimes\linux\x64\libgrpc_csharp_ext.so || goto :error
copy /Y nativelibs\csharp_ext_macos_x86\libgrpc_csharp_ext.dylib unitypackage\unitypackage_skeleton\Plugins\Grpc.Core\runtimes\osx\x86\grpc_csharp_ext.bundle || goto :error
copy /Y nativelibs\csharp_ext_macos_x64\libgrpc_csharp_ext.dylib unitypackage\unitypackage_skeleton\Plugins\Grpc.Core\runtimes\osx\x64\grpc_csharp_ext.bundle || goto :error
copy /Y nativelibs\csharp_ext_windows_x86\grpc_csharp_ext.dll unitypackage\unitypackage_skeleton\Plugins\Grpc.Core\runtimes\win\x86\grpc_csharp_ext.dll || goto :error
copy /Y nativelibs\csharp_ext_windows_x64\grpc_csharp_ext.dll unitypackage\unitypackage_skeleton\Plugins\Grpc.Core\runtimes\win\x64\grpc_csharp_ext.dll || goto :error

@rem add Android and iOS native libraries
copy /Y nativelibs\csharp_ext_linux_android_armeabi-v7a\libgrpc_csharp_ext.so unitypackage\unitypackage_skeleton\Plugins\Grpc.Core\runtimes\android\armeabi-v7a\libgrpc_csharp_ext.so || goto :error
copy /Y nativelibs\csharp_ext_linux_android_arm64-v8a\libgrpc_csharp_ext.so unitypackage\unitypackage_skeleton\Plugins\Grpc.Core\runtimes\android\arm64-v8a\libgrpc_csharp_ext.so || goto :error
copy /Y nativelibs\csharp_ext_linux_android_x86\libgrpc_csharp_ext.so unitypackage\unitypackage_skeleton\Plugins\Grpc.Core\runtimes\android\x86\libgrpc_csharp_ext.so || goto :error
copy /Y nativelibs\csharp_ext_macos_ios\libgrpc_csharp_ext.a unitypackage\unitypackage_skeleton\Plugins\Grpc.Core\runtimes\ios\libgrpc_csharp_ext.a || goto :error
copy /Y nativelibs\csharp_ext_macos_ios\libgrpc.a unitypackage\unitypackage_skeleton\Plugins\Grpc.Core\runtimes\ios\libgrpc.a || goto :error

@rem add gRPC dependencies
@rem TODO(jtattermusch): also include XMLdoc
copy /Y Grpc.Core\bin\Release\net45\System.Interactive.Async.dll unitypackage\unitypackage_skeleton\Plugins\System.Interactive.Async\lib\net45\System.Interactive.Async.dll || goto :error

@rem add Google.Protobuf
@rem TODO(jtattermusch): also include XMLdoc
copy /Y Grpc.HealthCheck\bin\Release\net45\Google.Protobuf.dll unitypackage\unitypackage_skeleton\Plugins\Google.Protobuf\lib\net45\Google.Protobuf.dll || goto :error

@rem create a zipfile that will act as a Unity package
cd unitypackage\unitypackage_skeleton
zip -r ..\..\grpc_unity_package.zip Plugins
cd ..\..
copy /Y grpc_unity_package.zip ..\..\artifacts\grpc_unity_package.%VERSION%.zip || goto :error

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
