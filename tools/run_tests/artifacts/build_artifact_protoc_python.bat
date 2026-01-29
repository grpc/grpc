@rem Copyright 2024 gRPC authors.
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

@rem Build script for standalone gRPC Python protoc plugin on Windows

setlocal

cd /d %~dp0\..\..\..

md cmake\build
pushd cmake\build

cmake -DgRPC_BUILD_TESTS=OFF ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_CXX_STANDARD=17 ^
      -DgRPC_BUILD_GRPC_PYTHON_PLUGIN=ON ^
      ..\..

@rem Use externally provided env to determine build parallelism, otherwise use default.
if "%GRPC_PROTOC_BUILD_COMPILER_JOBS%"=="" set GRPC_PROTOC_BUILD_COMPILER_JOBS=4

msbuild ALL_BUILD.vcxproj /p:Configuration=Release /p:Platform=%ARCHITECTURE% /maxcpucount:%GRPC_PROTOC_BUILD_COMPILER_JOBS%

popd

md "%ARTIFACTS_OUT%"
copy cmake\build\Release\grpc_python_plugin.exe "%ARTIFACTS_OUT%\grpc_python_plugin.exe"
copy cmake\build\Release\grpc_python_plugin.exe "%ARTIFACTS_OUT%\protoc-gen-grpc_python.exe"

echo Built gRPC Python plugin successfully:
dir "%ARTIFACTS_OUT%"