@rem Copyright 2026 gRPC authors.
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
setlocal EnableDelayedExpansion
IF "%cd%"=="T:\src" (
  call %~dp0\..\..\..\tools\internal_ci\helper_scripts\move_src_tree_and_respawn_itself.bat %0
  echo respawn script has finished with exitcode !errorlevel!
  exit /b !errorlevel!
)
endlocal

@rem enter repo root
cd /d %~dp0\..\..\..

call tools/internal_ci/helper_scripts/prepare_build_windows.bat || exit /b 1

python3 tools/run_tests/start_port_server.py || exit /b 1

@rem Run CMake build with default compiler (linking grpc_cli)
python3 tools/run_tests/run_tests.py -l c++ -c dbg --compiler default --build_only --cmake_configure_extra_args="-DgRPC_BUILD_TESTS=OFF"
set RUNTESTS_EXITCODE=%errorlevel%

exit /b %RUNTESTS_EXITCODE%