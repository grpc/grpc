@rem Copyright 2022 The gRPC Authors
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

setlocal

cd /d %~dp0\..\..\..

mkdir cmake
cd cmake
mkdir build
cd build

@rem Workaround a bug where VS150COMNTOOLS is not set due to a bug in VS 2017 installer
@rem see https://developercommunity.visualstudio.com/t/installing-visualstudio-build-tools-doesnt-add-env/17435
set "VS150COMNTOOLS=C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\Common7\Tools\"

If "%GRPC_BUILD_ACTIVATE_VS_TOOLS%" == "2017" (
  @rem set cl.exe build environment to build with VS2017 tooling
  @rem this is required for Ninja build to work
  call "%VS150COMNTOOLS%..\..\VC\Auxiliary\Build\vcvarsall.bat" %GRPC_BUILD_VS_TOOLS_ARCHITECTURE%
  @rem restore command echo
  echo on
)

If "%GRPC_CMAKE_GENERATOR%" == "Ninja" (
  @rem Use ninja

  @rem Select MSVC compiler (cl.exe) explicitly to make sure we don't end up gcc from mingw or cygwin
  @rem (both are on path in kokoro win workers)
  cmake -G "%GRPC_CMAKE_GENERATOR%" -DCMAKE_C_COMPILER="cl.exe" -DCMAKE_CXX_COMPILER="cl.exe" -DgRPC_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE="%MSBUILD_CONFIG%" %* ../.. || goto :error

  ninja -j%GRPC_RUN_TESTS_JOBS% buildtests_%GRPC_RUN_TESTS_CXX_LANGUAGE_SUFFIX% || goto :error

) else (
  @rem Use one of the Visual Studio generators.

  cmake -G "%GRPC_CMAKE_GENERATOR%" -A "%GRPC_CMAKE_ARCHITECTURE%" -DCMAKE_VS_PLATFORM_TOOLSET_HOST_ARCHITECTURE=x64 -DgRPC_BUILD_TESTS=ON -DgRPC_BUILD_MSVC_MP_COUNT=%GRPC_RUN_TESTS_JOBS% %* ../.. || goto :error

  @rem GRPC_RUN_TESTS_CXX_LANGUAGE_SUFFIX will be set to either "c" or "cxx"
  cmake --build . --target buildtests_%GRPC_RUN_TESTS_CXX_LANGUAGE_SUFFIX% --config %MSBUILD_CONFIG% || goto :error
)

endlocal

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
