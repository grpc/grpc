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

set PREPARE_BUILD_INSTALL_DEPS_CSHARP=true
call tools/internal_ci/helper_scripts/prepare_build_windows.bat || exit /b 1

call tools/internal_ci/helper_scripts/prepare_ccache.bat || exit /b 1

@rem Install Msys2 zip to avoid crash when using cygwin's zip on grpc-win2016 kokoro workers.
@rem Downloading from GCS should be very reliables when on a GCP VM.
@rem TODO(jtattermusch): find a better way of making the build_packages step work on windows workers.
mkdir C:\zip
curl -sSL --fail -o C:\zip\zip.exe https://storage.googleapis.com/grpc-build-helper/zip-3.0-1-x86_64/zip.exe || goto :error
set PATH=C:\zip;%PATH%
zip --version

@rem Build all protoc windows artifacts
python tools/run_tests/task_runner.py -f artifact windows protoc %TASK_RUNNER_EXTRA_FILTERS% -j 4 --inner_jobs 4 -x build_artifacts_protoc/sponge_log.xml || set FAILED=true

@rem the next step expects to find the artifacts from the previous step in the "input_artifacts" folder.
bash -c "rm -rf input_artifacts; mkdir -p input_artifacts; cp -r artifacts/* input_artifacts/ || true"

@rem This step builds the nuget packages from input_artifacts
@rem Set env variable option to build single platform version of the nugets.
@rem (this is required as we only have the windows artifacts at hand)
set GRPC_CSHARP_BUILD_SINGLE_PLATFORM_NUGET=1 
python tools/run_tests/task_runner.py -f package windows csharp nuget -j 2 -x build_packages/sponge_log.xml || set FAILED=true

@rem the next step expects to find the artifacts from the previous step in the "input_artifacts" folder.
@rem in addition to that, preserve the contents of "artifacts" directory since we want kokoro
@rem to upload its contents as job output artifacts
bash -c "rm -rf input_artifacts; mkdir -p input_artifacts; cp -r artifacts/* input_artifacts/ || true"

@rem Run all C# windows distribtests
@rem We run the distribtests even if some of the artifacts have failed to build, since that gives
@rem a better signal about which distribtest are affected by the currently broken artifact builds.
python tools/run_tests/task_runner.py -f distribtest windows csharp %TASK_RUNNER_EXTRA_FILTERS% -j 4 -x distribtests/sponge_log.xml || set FAILED=true

@rem show ccache stats
ccache --show-stats

bash tools/internal_ci/helper_scripts/store_artifacts_from_moved_src_tree.sh

if not "%FAILED%" == "" (
  exit /b 1
)
