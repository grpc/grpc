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

python -m pip install --upgrade six
@rem some artifacts are broken for setuptools 38.5.0. See https://github.com/grpc/grpc/issues/14317
python -m pip install --upgrade setuptools==44.1.1
python -m pip install --upgrade "cython<3.0.0rc1"
python -m pip install -rrequirements.txt --user

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

@rem Build gRPC Python extensions
python setup.py build_ext -c %EXT_COMPILER% || goto :error

pushd tools\distrib\python\grpcio_tools
python setup.py build_ext -c %EXT_COMPILER% || goto :error
popd

@rem Build gRPC Python distributions
python setup.py bdist_wheel || goto :error

pushd tools\distrib\python\grpcio_tools
python setup.py bdist_wheel || goto :error
popd

@rem Ensure the generate artifacts are valid.
python -m pip install packaging==21.3 twine==3.8.0
python -m twine check dist\* tools\distrib\python\grpcio_tools\dist\* || goto :error

xcopy /Y /I /S dist\* %ARTIFACT_DIR% || goto :error
xcopy /Y /I /S tools\distrib\python\grpcio_tools\dist\* %ARTIFACT_DIR% || goto :error

goto :EOF

:error
popd
exit /b 1
