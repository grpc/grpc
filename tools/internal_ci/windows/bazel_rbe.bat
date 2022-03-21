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

@rem Avoid slow finalization after the script has exited.
@rem See the script's prologue for info on the correct invocation pattern.
setlocal EnableDelayedExpansion
IF "%cd%"=="T:\src" (
  call %~dp0\..\..\..\tools\internal_ci\helper_scripts\move_src_tree_and_respawn_itself.bat %0
  echo respawn script has finished with exitcode !errorlevel!
  exit /b !errorlevel!
)
endlocal

cd github/grpc

call tools/internal_ci/helper_scripts/prepare_build_windows.bat || exit /b 1

@rem Install bazel
@rem Side effect of the tools/bazel script is that it downloads the correct version of bazel binary.
mkdir C:\bazel
bash -c "tools/bazel --version && cp tools/bazel-*.exe /c/bazel/bazel.exe"
set PATH=C:\bazel;%PATH%
bazel --version

@rem Generate a random UUID and store in "bazel_invocation_ids" artifact file
powershell -Command "[guid]::NewGuid().ToString()" >%KOKORO_ARTIFACTS_DIR%/bazel_invocation_ids
set /p BAZEL_INVOCATION_ID=<%KOKORO_ARTIFACTS_DIR%/bazel_invocation_ids

bazel --bazelrc=tools/remote_build/windows.bazelrc --output_user_root=T:\_bazel_output test --invocation_id="%BAZEL_INVOCATION_ID%" %BAZEL_FLAGS% --workspace_status_command=tools/remote_build/workspace_status_kokoro.bat //test/...
set BAZEL_EXITCODE=%errorlevel%

if not "%UPLOAD_TEST_RESULTS%"=="" (
  @rem Sleep to let ResultStore finish writing results before querying
  sleep 60
  python3 tools/run_tests/python_utils/upload_rbe_results.py || exit /b 1
)

exit /b %BAZEL_EXITCODE%
