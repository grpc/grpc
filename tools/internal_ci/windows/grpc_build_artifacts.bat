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

@rem Move python installation from _32bit to _32bits where they are expected by python artifact builder
@rem TODO(jtattermusch): get rid of this hack
rename C:\Python27_32bit Python27_32bits
rename C:\Python34_32bit Python34_32bits
rename C:\Python35_32bit Python35_32bits
rename C:\Python36_32bit Python36_32bits
rename C:\Python37_32bit Python37_32bits

@rem enter repo root
cd /d %~dp0\..\..\..

call tools/internal_ci/helper_scripts/prepare_build_windows.bat

python tools/run_tests/task_runner.py -f artifact windows -j 4
set RUNTESTS_EXITCODE=%errorlevel%

bash tools/internal_ci/helper_scripts/delete_nonartifacts.sh

exit /b %RUNTESTS_EXITCODE%
