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

cmake -G "%generator%" -DgRPC_BUILD_TESTS=OFF -DgRPC_MSVC_STATIC_RUNTIME=ON ../.. || goto :error
cmake --build . --target protoc --config Release || goto :error
cmake --build . --target plugins --config Release || goto :error
cd ..\..

xcopy /Y cmake\build\third_party\protobuf\Release\protoc.exe %ARTIFACTS_OUT%\ || goto :error
xcopy /Y cmake\build\Release\*_plugin.exe %ARTIFACTS_OUT%\ || goto :error

goto :EOF

:error
exit /b 1