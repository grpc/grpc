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

@rem Avoid slow finalization after the script has exited.
@rem See the script's prologue for info on the correct invocation pattern.
setlocal EnableDelayedExpansion
IF "%cd%"=="T:\src" (
  call %~dp0\..\..\..\tools\internal_ci\helper_scripts\move_src_tree_and_respawn_itself.bat %0
  echo respawn script has finished with exitcode !errorlevel!
  exit /b !errorlevel!
)
endlocal

@rem enter repo root
cd /d %~dp0\..\..\..

set PREPARE_BUILD_INSTALL_DEPS_PYTHON=true
call tools/internal_ci/helper_scripts/prepare_build_windows.bat || exit /b 1

@rem Build all python windows artifacts
python tools/run_tests/task_runner.py -f artifact windows python %TASK_RUNNER_EXTRA_FILTERS% -j 4 --inner_jobs 4 -x build_artifacts_python/sponge_log.xml || set FAILED=true

@rem the next step expects to find the artifacts from the previous step in the "input_artifacts" folder.
bash -c "rm -rf input_artifacts; mkdir -p input_artifacts; cp -r artifacts/* input_artifacts/ || true"

@rem Collect the python artifact from subdirectories of input_artifacts/ to artifacts/
@rem TODO(jtattermusch): when collecting the artifacts that will later be uploaded as kokoro job artifacts,
@rem potentially skip some file names that would clash with linux-created artifacts.
bash -c "cp -r input_artifacts/python_*/* artifacts/ || true"

@rem TODO(jtattermusch): Here we would normally run python windows distribtests, but currently no such tests are defined
@rem in distribtest_targets.py

bash tools/internal_ci/helper_scripts/store_artifacts_from_moved_src_tree.sh

if not "%FAILED%" == "" (
  exit /b 1
)
