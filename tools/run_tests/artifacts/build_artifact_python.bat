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

python -m pip install --upgrade pip six
@rem Ping to a single version to make sure we're building the same artifacts
python -m pip install setuptools==77.0.1 wheel==0.43.0
python -m pip install --upgrade "cython==3.1.1"
python -m pip install -r requirements.txt --user

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

@rem Set up gRPC Python tools
python tools\distrib\python\make_grpcio_tools.py

@rem Creates a unique, short, and concurrency-safe directory like T:\b12345
call :CreateAndGetUniqueTempDir
set "GRPC_BUILD_EXT_TEMP=%SHORT_TMP_DIR%"

@rem Build gRPC Python distribution
python -m build --wheel || goto :error

@rem Build grpcio-tools Python distribution
@REM call :CreateAndGetUniqueTempDir
@REM set "GRPC_TOOLS_BUILD_EXT_TEMP=%SHORT_TMP_DIR%"
@REM pushd tools\distrib\python\grpcio_tools
@REM python -m build --wheel || goto :error
@REM popd

@REM @rem Ensure the generate artifacts are valid.
@REM python -m pip install packaging==21.3 twine==5.0.0
@REM python -m twine check dist\* tools\distrib\python\grpcio_tools\dist\* || goto :error

@REM xcopy /Y /I /S dist\* %ARTIFACT_DIR% || goto :error
@REM xcopy /Y /I /S tools\distrib\python\grpcio_tools\dist\* %ARTIFACT_DIR% || goto :error

goto :EOF

:error
popd
exit /b 1

:CreateAndGetUniqueTempDir
  @rem Use time for randomness
  set "CURRENT_TIME=%TIME%"
  echo %CURRENT_TIME%

  @rem remove : and . from the time
  set "CLEAN_TIME=%CURRENT_TIME::=%"
  set "CLEAN_TIME=%CLEAN_TIME:.=%"

  @rem create a unique id using last 7 digits
  set "UNIQUE_ID=%CLEAN_TIME:~-7%"

  set "SHORT_TMP_DIR=T:\t%UNIQUE_ID%"
  mkdir "%SHORT_TMP_DIR%"
