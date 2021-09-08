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

@rem enter repo root
cd /d %~dp0\..\..\..

call tools/internal_ci/helper_scripts/prepare_build_windows.bat || exit /b 1

rem NOTHING TO DO HERE
rem This script used to run "task_runner.py -f package windows", but
rem currently there are no build_packages tasks that need to be run on windows.
rem The only build_packages task that ever needed to run on windows was C#, but we switched to
rem building C# nugets on linux (as dotnet SDK on linux does a good job)
rem TODO(jtattermusch): remove the infrastructure for running "build_packages" kokoro job on windows.

bash tools/internal_ci/helper_scripts/delete_nonartifacts.sh

exit /b %RUNTESTS_EXITCODE%
