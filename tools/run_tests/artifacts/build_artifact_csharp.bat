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

@rem Builds C# artifacts on Windows

set ARCHITECTURE=%1

@call tools\run_tests\helper_scripts\pre_build_csharp.bat %ARCHITECTURE% || goto :error

cd cmake\build\%ARCHITECTURE%
cmake --build . --target grpc_csharp_ext --config Release
cd ..\..\..

mkdir -p %ARTIFACTS_OUT%
copy /Y cmake\build\Win32\Release\grpc_csharp_ext.dll %ARTIFACTS_OUT% || copy /Y cmake\build\x64\Release\grpc_csharp_ext.dll %ARTIFACTS_OUT% || goto :error

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
