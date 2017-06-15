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

setlocal
cd third_party/protobuf/cmake

mkdir build & cd build
mkdir solution & cd solution
cmake -G "%generator%" -Dprotobuf_BUILD_TESTS=OFF ../.. || goto :error
endlocal

call vsprojects/build_plugins.bat || goto :error

xcopy /Y third_party\protobuf\cmake\build\solution\Release\protoc.exe %ARTIFACTS_OUT%\ || goto :error
xcopy /Y vsprojects\Release\*_plugin.exe %ARTIFACTS_OUT%\ || xcopy /Y vsprojects\x64\Release\*_plugin.exe %ARTIFACTS_OUT%\ || goto :error

goto :EOF

:error
exit /b 1