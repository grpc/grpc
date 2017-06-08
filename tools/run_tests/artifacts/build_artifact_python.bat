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


set PATH=C:\%1;C:\%1\scripts;C:\msys64\mingw%2\bin;%PATH%

pip install --upgrade six
pip install --upgrade setuptools
pip install -rrequirements.txt

set GRPC_PYTHON_BUILD_WITH_CYTHON=1

@rem Multiple builds are running simultaneously, so to avoid distutils
@rem file collisions, we build everything in a tmp directory
@rem TODO(jtattermusch): it doesn't look like builds are actually running in parallel in the same dir
mkdir -p %ARTIFACTS_OUT%
set ARTIFACT_DIR=%cd%\%ARTIFACTS_OUT%
set BUILD_DIR=C:\Windows\Temp\pygrpc-%3\
mkdir %BUILD_DIR%
xcopy /s/e/q %cd%\* %BUILD_DIR%
pushd %BUILD_DIR%


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


xcopy /Y /I /S dist\* %ARTIFACT_DIR% || goto :error
xcopy /Y /I /S tools\distrib\python\grpcio_tools\dist\* %ARTIFACT_DIR% || goto :error

popd
rmdir /s /q %BUILD_DIR%

goto :EOF

:error
popd
rmdir /s /q %BUILD_DIR%
exit /b 1
