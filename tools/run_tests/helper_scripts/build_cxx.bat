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
mkdir install
cd ..
set "INSTALL_PATH=%~dp0\cmake\install"

If "%GRPC_BUILD_ACTIVATE_VS_TOOLS%" == "2019" (
  @rem set cl.exe build environment to build with VS2019 tooling
  @rem this is required for Ninja build to work
  call "%VS160COMNTOOLS%..\..\VC\Auxiliary\Build\vcvarsall.bat" %GRPC_BUILD_VS_TOOLS_ARCHITECTURE%
  @rem restore command echo
  echo on
)

If "%GRPC_BUILD_ACTIVATE_VS_TOOLS%" == "2022" (
  @rem set cl.exe build environment to build with VS2022 tooling
  @rem this is required for Ninja build to work
  call "%VS170COMNTOOLS%..\..\VC\Auxiliary\Build\vcvarsall.bat" %GRPC_BUILD_VS_TOOLS_ARCHITECTURE%
  @rem restore command echo
  echo on
)

@rem Setting the env variable to a single space translates to passing no argument
@rem when evaluated on the command line.
set "CMAKE_SYSTEM_VERSION_ARG= "

If "%GRPC_CMAKE_GENERATOR%" == "Visual Studio 16 2019" (
  @rem Always use the newest Windows 10 SDK available.
  @rem A new-enough Windows 10 SDK that supports C++11's stdalign.h is required
  @rem for a successful build.
  @rem By default cmake together with Visual Studio generator
  @rem pick a version of Win SDK that matches the Windows version,
  @rem even when a newer version of the SDK available.
  @rem Setting CMAKE_SYSTEM_VERSION=10.0 changes this behavior
  @rem to pick the newest Windows SDK available.
  @rem When using Ninja generator, this problem doesn't happen.
  @rem See b/275694647 and https://gitlab.kitware.com/cmake/cmake/-/issues/16202#note_140259
  set "CMAKE_SYSTEM_VERSION_ARG=-DCMAKE_SYSTEM_VERSION=10.0"
)

If "%GRPC_CMAKE_GENERATOR%" == "Visual Studio 17 2022" (
  @rem The same as above.
  set "CMAKE_SYSTEM_VERSION_ARG=-DCMAKE_SYSTEM_VERSION=10.0"
)

If "%GRPC_CMAKE_GENERATOR%" == "Ninja" (
  @rem Use ninja
  @rem Select MSVC compiler (cl.exe) explicitly to make sure we don't end up gcc from mingw or cygwin
  @rem (both are on path in kokoro win workers)

  @rem Install abseil-cpp since opentelemetry CMake uses find_package to find it.
  cd third_party/abseil-cpp
  mkdir build
  cd build
  cmake -G "%GRPC_CMAKE_GENERATOR%" -DCMAKE_C_COMPILER="cl.exe" -DCMAKE_CXX_COMPILER="cl.exe" -DABSL_BUILD_TESTING=OFF -DCMAKE_BUILD_TYPE="%MSBUILD_CONFIG%" -DCMAKE_INSTALL_PREFIX="%INSTALL_PATH%" %* ..  || goto :error
  ninja -j%GRPC_RUN_TESTS_JOBS% install || goto :error

  @rem Install opentelemetry-cpp since we only support "package" mode for opentelemetry at present.
  cd ../../..
  cd third_party/opentelemetry-cpp
  mkdir build
  cd build
  cmake -G "%GRPC_CMAKE_GENERATOR%" -DCMAKE_C_COMPILER="cl.exe" -DCMAKE_CXX_COMPILER="cl.exe" -DWITH_ABSEIL=ON -DBUILD_TESTING=OFF -DWITH_BENCHMARK=OFF -DWITH_ETW=OFF -DCMAKE_BUILD_TYPE="%MSBUILD_CONFIG%" -DCMAKE_INSTALL_PREFIX="%INSTALL_PATH%" %* ..  || goto :error
  ninja -j%GRPC_RUN_TESTS_JOBS% install || goto :error

  cd ../../..

  mkdir cmake
  cd cmake
  mkdir build
  cd build

  cmake -G "%GRPC_CMAKE_GENERATOR%" -DCMAKE_C_COMPILER="cl.exe" -DCMAKE_CXX_COMPILER="cl.exe" -DgRPC_BUILD_GRPCPP_OTEL_PLUGIN=ON -DgRPC_ABSL_PROVIDER=package -DgRPC_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE="%MSBUILD_CONFIG%" -DCMAKE_INSTALL_PREFIX="%INSTALL_PATH%" %* ../.. || goto :error

  ninja -j%GRPC_RUN_TESTS_JOBS% buildtests_%GRPC_RUN_TESTS_CXX_LANGUAGE_SUFFIX% || goto :error

) else (
  @rem Use one of the Visual Studio generators.

  @rem Install abseil-cpp since opentelemetry CMake uses find_package to find it.
  cd third_party/abseil-cpp
  mkdir build
  cd build
  cmake -G "%GRPC_CMAKE_GENERATOR%" -A "%GRPC_CMAKE_ARCHITECTURE%" %CMAKE_SYSTEM_VERSION_ARG% -DCMAKE_VS_PLATFORM_TOOLSET_HOST_ARCHITECTURE=x64 -DABSL_BUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX="%INSTALL_PATH%" %* .. || goto :error
  cmake --build . --target install || goto :error

  @rem Install opentelemetry-cpp since we only support "package" mode for opentelemetry at present.
  cd ../../..
  cd third_party/opentelemetry-cpp
  mkdir build
  cd build
  cmake -G "%GRPC_CMAKE_GENERATOR%" -A "%GRPC_CMAKE_ARCHITECTURE%" %CMAKE_SYSTEM_VERSION_ARG% -DCMAKE_VS_PLATFORM_TOOLSET_HOST_ARCHITECTURE=x64 -DWITH_ABSEIL=ON -DBUILD_TESTING=OFF -DWITH_BENCHMARK=OFF -DWITH_ETW=OFF -DCMAKE_INSTALL_PREFIX="%INSTALL_PATH%" %* .. || goto :error
  cmake --build . --target install -j%GRPC_RUN_TESTS_JOBS% || goto :error

  cd ../../..
  mkdir cmake
  cd cmake
  mkdir build
  cd build

  cmake -G "%GRPC_CMAKE_GENERATOR%" -A "%GRPC_CMAKE_ARCHITECTURE%" %CMAKE_SYSTEM_VERSION_ARG% -DCMAKE_VS_PLATFORM_TOOLSET_HOST_ARCHITECTURE=x64 -DgRPC_BUILD_GRPCPP_OTEL_PLUGIN=ON -DgRPC_ABSL_PROVIDER=package -DgRPC_BUILD_TESTS=ON -DgRPC_BUILD_MSVC_MP_COUNT=%GRPC_RUN_TESTS_JOBS% -DCMAKE_INSTALL_PREFIX="%INSTALL_PATH%" %* ../.. || goto :error

  @rem GRPC_RUN_TESTS_CXX_LANGUAGE_SUFFIX will be set to either "c" or "cxx"
  @rem Don't add -j option here because it will disable gRPC_BUILD_MSVC_MP_COUNT option
  cmake --build . --target buildtests_%GRPC_RUN_TESTS_CXX_LANGUAGE_SUFFIX% --config %MSBUILD_CONFIG% || goto :error
)

endlocal

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
