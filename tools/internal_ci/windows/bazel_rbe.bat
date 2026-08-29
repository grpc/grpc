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

echo "reparsepoint in orig src"
fsutil reparsepoint query %~dp0\..\..\..\test/core/http/python_wrapper.sh

echo "cat orig src"
cat %~dp0\..\..\..\test/core/http/python_wrapper.sh

git config core.symlinks

setlocal EnableDelayedExpansion
@rem IF "%cd%"=="T:\src" (
@rem  call %~dp0\..\..\..\tools\internal_ci\helper_scripts\move_src_tree_and_respawn_itself.bat %0
@rem   echo respawn script has finished with exitcode !errorlevel!
@rem  exit /b !errorlevel!
@rem )
@rem endlocal

git config core.symlinks

cd github/grpc

git config core.symlinks

ls -l test/core/http/python_wrapper.sh

echo "reparsepoint in altsrc:"
pwd
fsutil reparsepoint query test/core/http/python_wrapper.sh

echo "cat alt src"
cat test/core/http/python_wrapper.sh

call tools/internal_ci/helper_scripts/prepare_build_windows.bat || exit /b 1

@rem Install bazel
@rem Side effect of the tools/bazel script is that it downloads the correct version of bazel binary.
mkdir C:\bazel
bash -c "tools/bazel --version && cp tools/bazel-*.exe /c/bazel/bazel.exe"
set PATH=C:\bazel;%PATH%
bazel --version

python3 tools/run_tests/python_utils/bazel_report_helper.py --report_path bazel_rbe

call bazel_rbe/bazel_wrapper.bat ^
  --bazelrc=tools/remote_build/windows.bazelrc ^
  --output_user_root=T:\_bazel_output ^
  test ^
  %BAZEL_FLAGS% ^
  --define=protobuf_allow_msvc=true ^
  -- ^
  //test/... ^
  || exit /b 1
