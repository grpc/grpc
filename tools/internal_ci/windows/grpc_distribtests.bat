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

@rem Move packages generated by the previous step in the build chain.
powershell -Command "mv %KOKORO_GFILE_DIR%\github\grpc\artifacts input_artifacts"
dir input_artifacts

python tools/run_tests/task_runner.py -f distribtest windows -j 16
set RUNTESTS_EXITCODE=%errorlevel%

bash tools/internal_ci/helper_scripts/delete_nonartifacts.sh

exit /b %RUNTESTS_EXITCODE%
