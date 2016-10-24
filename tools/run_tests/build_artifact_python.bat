@rem Copyright 2016, Google Inc.
@rem All rights reserved.
@rem
@rem Redistribution and use in source and binary forms, with or without
@rem modification, are permitted provided that the following conditions are
@rem met:
@rem
@rem     * Redistributions of source code must retain the above copyright
@rem notice, this list of conditions and the following disclaimer.
@rem     * Redistributions in binary form must reproduce the above
@rem copyright notice, this list of conditions and the following disclaimer
@rem in the documentation and/or other materials provided with the
@rem distribution.
@rem     * Neither the name of Google Inc. nor the names of its
@rem contributors may be used to endorse or promote products derived from
@rem this software without specific prior written permission.
@rem
@rem THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
@rem "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
@rem LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
@rem A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
@rem OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
@rem SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
@rem LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
@rem DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
@rem THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
@rem (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
@rem OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


set PATH=C:\%1;C:\%1\scripts;C:\msys64\mingw%2\bin;%PATH%

pip install --upgrade six
pip install --upgrade setuptools
pip install -rrequirements.txt

set GRPC_PYTHON_BUILD_WITH_CYTHON=1

@rem Multiple builds are running simultaneously, so to avoid distutils
@rem file collisions, we build everything in a tmp directory
if not exist "artifacts" mkdir "artifacts"
set ARTIFACT_DIR=%cd%\artifacts
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
