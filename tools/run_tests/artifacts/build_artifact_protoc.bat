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

mkdir -p %ARTIFACTS_OUT%

@rem enter repo root
cd /d %~dp0\..\..\..

mkdir cmake
cd cmake
mkdir build
cd build

@rem Use externally provided env to determine build parallelism, otherwise use default.
if "%GRPC_PROTOC_BUILD_COMPILER_JOBS%"=="" (
  set GRPC_PROTOC_BUILD_COMPILER_JOBS=2
)

@rem set cl.exe build environment to build with VS2019 tooling
@rem this is required for Ninja build to work
call "%VS160COMNTOOLS%..\..\VC\Auxiliary\Build\vcvarsall.bat" %ARCHITECTURE%
@rem restore command echo
echo on

@rem Select MSVC compiler (cl.exe) explicitly to make sure we don't end up gcc from mingw or cygwin
@rem (both are on path in kokoro win workers)
cmake -G Ninja -DCMAKE_C_COMPILER="cl.exe" -DCMAKE_CXX_COMPILER="cl.exe" -DCMAKE_BUILD_TYPE=Release -DgRPC_BUILD_TESTS=OFF -DgRPC_MSVC_STATIC_RUNTIME=ON ../../.. || goto :error

ninja -j%GRPC_PROTOC_BUILD_COMPILER_JOBS% protoc plugins || goto :error
cd ..\..

xcopy /Y cmake\build\third_party\protobuf\protoc.exe %ARTIFACTS_OUT%\ || goto :error
xcopy /Y cmake\build\*_plugin.exe %ARTIFACTS_OUT%\ || goto :error

goto :EOF

:error
exit /b 1