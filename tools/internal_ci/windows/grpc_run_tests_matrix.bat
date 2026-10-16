@rem Copyright 2017 gRPC authors.
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

@rem Info on disk usage
dir t:\

@rem enter repo root
cd /d %~dp0\..\..\..

@rem if RUN_TESTS_FLAGS contains the string "csharp", make sure C# deps are installed.
If Not "%RUN_TESTS_FLAGS%"=="%RUN_TESTS_FLAGS:csharp=%" (
    set PREPARE_BUILD_INSTALL_DEPS_CSHARP=true
)
@rem if RUN_TESTS_FLAGS contains the string "python", make sure python deps are installed.
If Not "%RUN_TESTS_FLAGS%"=="%RUN_TESTS_FLAGS:python=%" (
    set PREPARE_BUILD_INSTALL_DEPS_PYTHON=true
)
call tools/internal_ci/helper_scripts/prepare_build_windows.bat || exit /b 1

call tools/internal_ci/helper_scripts/prepare_ccache.bat || exit /b 1

@rem TODO(https://github.com/grpc/grpc/issues/28011): Remove once Windows Kokoro workers'
@rem   Python installs have been upgraded. Currently, removing this line will cause
@rem   run_tests.py to fail to spawn test subprocesses.
python3 tools/run_tests/start_port_server.py || exit /b 1

python3 tools/run_tests/run_tests_matrix.py %RUN_TESTS_FLAGS%
set RUNTESTS_EXITCODE=%errorlevel%

@rem show ccache stats
ccache --show-stats

@rem Info on disk usage after test
dir t:\

exit /b %RUNTESTS_EXITCODE%
