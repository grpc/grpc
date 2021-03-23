@rem Copyright 2019 gRPC authors.
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

@rem TODO(jtattermusch): make this generate less output
@rem TODO(jtattermusch): use tools/bazel script to keep the versions in sync
choco install bazel -y --version 3.7.1 --limit-output

cd github/grpc
set PATH=C:\tools\msys64\usr\bin;C:\Python27;%PATH%

@rem Generate a random UUID and store in "bazel_invocation_ids" artifact file
powershell -Command "[guid]::NewGuid().ToString()" >%KOKORO_ARTIFACTS_DIR%/bazel_invocation_ids
set /p BAZEL_INVOCATION_ID=<%KOKORO_ARTIFACTS_DIR%/bazel_invocation_ids

@rem TODO(jtattermusch): windows RBE should be able to use the same credentials as Linux RBE.
bazel --bazelrc=tools/remote_build/windows.bazelrc test --invocation_id="%BAZEL_INVOCATION_ID%" %BAZEL_FLAGS% --workspace_status_command=tools/remote_build/workspace_status_kokoro.bat //test/...
set BAZEL_EXITCODE=%errorlevel%

if not "%UPLOAD_TEST_RESULTS%"=="" (
  @rem Sleep to let ResultStore finish writing results before querying
  timeout /t 60 /nobreak
  python ./tools/run_tests/python_utils/upload_rbe_results.py
)

exit /b %BAZEL_EXITCODE%
