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

@rem enter repo root
cd /d %~dp0\..\..\..

mkdir cmake
cd cmake
mkdir build
cd build
mkdir %ARCHITECTURE%
cd %ARCHITECTURE%

@rem TODO(jtattermusch): is there a better way to force using MSVC?
@rem select the MSVC compiler explicitly to avoid using gcc from mingw or cygwin
@rem (both are on path)
set "MSVC_COMPILER=C:/Program Files (x86)/Microsoft Visual Studio 14.0/VC/bin/cl.exe"
if "%ARCHITECTURE%" == "x64" (
  set "MSVC_COMPILER=C:/Program Files (x86)/Microsoft Visual Studio 14.0/VC/bin/amd64/cl.exe"
)

call "%VS140COMNTOOLS%..\..\VC\vcvarsall.bat" %ARCHITECTURE%
cmake -G Ninja -DCMAKE_C_COMPILER="%MSVC_COMPILER%" -DCMAKE_CXX_COMPILER="%MSVC_COMPILER%" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DgRPC_BUILD_TESTS=OFF -DgRPC_MSVC_STATIC_RUNTIME=ON ../../.. || goto :error
cmake --build . --target grpc_csharp_ext
cd ..\..\..

mkdir -p %ARTIFACTS_OUT%
copy /Y cmake\build\%ARCHITECTURE%\grpc_csharp_ext.dll %ARTIFACTS_OUT% || goto :error
copy /Y cmake\build\%ARCHITECTURE%\grpc_csharp_ext.pdb %ARTIFACTS_OUT% || goto :error

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
