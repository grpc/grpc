@rem Copyright 2021 The gRPC Authors
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

@rem Call this script at the beginning of your CI script to move
@rem the entire source tree to a different directory.
@rem That avoids slow finalization (and a number of other problems)
@rem caused by kokoro rsyncing the entire /tmpfs/src directory
@rem back to the agent.
@rem Since batch scripts don't support the "source" unix command,
@rem invoking this script is a bit trickier than on unix.
@rem This script should be invoked from the CI script like this:
@rem Note that honoring the pattern (including e.g. EnableDelayedExpansion)
@rem is critical for correctly propagating the exitcode.
@rem TODO(jtattermusch): find a way to simplify the invocation of the respawn script.
@rem
@rem setlocal EnableDelayedExpansion
@rem IF "%cd%"=="T:\src" (
@rem   call %~dp0\..\..\..\tools\internal_ci\helper_scripts\move_src_tree_and_respawn_itself.bat %0
@rem   echo respawn script has finished with exitcode !errorlevel!
@rem   exit /b !errorlevel!
@rem )
@rem endlocal


@echo off

@rem CI script path needs to be passed as arg1.
set CI_SCRIPT_RELATIVE_TO_SRC=%1

@rem Check that this script was invoked under correct circumstances.
IF NOT "%cd%"=="T:\src" (
  @echo "Error: Current directory must be T:\src when invoking move_src_tree_and_respawn_itself.bat"
  exit /b 1
)

@rem T:\ is equivalent to /tmpfs on kokoro linux.
echo "Moving workspace from T:\src to T:\altsrc and respawning the CI script."
cd /d T:\
@rem We cannot simply rename "src" to "altsrc" as on linux since the currently running batch file is in it
@rem and windows holds a lock that prevents moving the dir.
bash -c "set -ex; mkdir -p altsrc; time cp -r src/github altsrc;"
@rem Delete files we can under the original "src/github" directory, skipping the directory that contains CI scripts
@rem (as on of the scripts is currently running and cannot be deleted)
@rem We don't want to delete files in "src" outside of "src/github" since they contain e.g kokoro input artifacts.
bash -c "set -ex; cd src/github; time find . -type f -not -path './grpc/tools/internal_ci/*' -exec rm -f {} +; ls grpc"
cd altsrc

@rem scripts in tools/run_tests generate test reports and we need to make sure these reports
@rem land in a directory that is going to be rsynced back to the kokoro agent.
set GRPC_TEST_REPORT_BASE_DIR=T:\src\github\grpc

echo "Invoking original CI script now."
@echo on
call "%CI_SCRIPT_RELATIVE_TO_SRC%"
exit /b %errorlevel%
