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

@rem set path to python
set PATH=C:\%1;C:\%1\scripts;%PATH%

@rem set path to the existed mingw compiler
set PATH=C:\msys64\mingw%2\bin;C:\tools\msys64\mingw%2\bin;%PATH%
:end_mingw64_installation

python -m pip install --upgrade pip==25.2 six
@rem Ping to a single version to make sure we're building the same artifacts
python -m pip install setuptools==77.0.1 wheel==0.43.0
python -m pip install --upgrade "cython==3.1.1"
python -m pip install -r requirements.txt --user
python -m pip install --upgrade importlib-metadata build

@rem set GRPC_PYTHON_OVERRIDE_CYGWIN_DETECTION_FOR_27=1
set GRPC_PYTHON_BUILD_WITH_CYTHON=1

@rem Allow build_ext to build C/C++ files in parallel
@rem by enabling a monkeypatch. It speeds up the build a lot.
@rem Use externally provided GRPC_PYTHON_BUILD_EXT_COMPILER_JOBS value if set.
if "%GRPC_PYTHON_BUILD_EXT_COMPILER_JOBS%"=="" (
  set GRPC_PYTHON_BUILD_EXT_COMPILER_JOBS=4
)

mkdir -p %ARTIFACTS_OUT%
set ARTIFACT_DIR=%cd%\%ARTIFACTS_OUT%


@rem use short temp directory to avoid linker command file errors caused by
@rem exceeding 131071 characters.
set "GRPC_PYTHON_BUILD_USE_SHORT_TEMP_DIR_NAME=1"

@rem Build the static libraries for gRPC Core
if exist "libs\opt\lib\grpc.lib" goto :after_build_core

@rem Build gRPC with CMake
if not exist "cmake_build" mkdir cmake_build
cd cmake_build

if "%2" == "32" (
  set ARCH=Win32
) else (
  set ARCH=x64
)

cmake -G "Visual Studio 16 2019" -A %ARCH% ^
    -DgRPC_BUILD_TESTS=OFF ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DgRPC_INSTALL=ON ^
    -DCMAKE_INSTALL_PREFIX=..\libs\opt ^
    -DABSL_ENABLE_INSTALL=ON ^
    -DCARES_INSTALL=ON ^
    -Dprotobuf_INSTALL=ON ^
    -DRE2_INSTALL=ON ^
    -DZLIB_INSTALL=ON ^
    -DgRPC_MSVC_STATIC_RUNTIME=ON ^
    -Dprotobuf_MSVC_STATIC_RUNTIME=ON ^
    -DABSL_MSVC_STATIC_RUNTIME=ON ^
    ..

cmake --build . --config Release --target install
cd ..

:after_build_core
set GRPC_PYTHON_PREBUILT_CORE_PATH=%cd%\libs\opt\lib

@rem Build gRPC Python distribution
python -m build || goto :error

@rem Set up gRPC Python tools
python tools\distrib\python\make_grpcio_tools.py

@rem Build grpcio-tools Python distribution
pushd tools\distrib\python\grpcio_tools
python -m build || goto :error
popd

@rem Ensure the generate artifacts are valid.
python -m pip install packaging==21.3 twine==5.0.0
python -m twine check dist\* tools\distrib\python\grpcio_tools\dist\* || goto :error

xcopy /Y /I /S dist\* %ARTIFACT_DIR% || goto :error
xcopy /Y /I /S tools\distrib\python\grpcio_tools\dist\* %ARTIFACT_DIR% || goto :error

goto :EOF

:error
popd
exit /b 1
