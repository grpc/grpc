@rem Copyright 2017, gRPC authors
@rem
@rem Licensed under the Apache License, Version 2.0 (the "License");
@rem you may not use this file except in compliance with the License.
@rem You may obtain a copy of the License at
@rem
@rem   http://www.apache.org/licenses/LICENSE-2.0
@rem
@rem Unless required by applicable law or agreed to in writing, software
@rem distributed under the License is distributed on an "AS IS" BASIS,
@rem WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
@rem See the License for the specific language governing permissions and
@rem limitations under the License.

setlocal

@rem enter repo root
cd /d %~dp0\..\..\..

git submodule update --init

sh tools\run_tests\helper_scripts\run_tests_in_workspace.sh -t -j 4 -x c_windows_dbg_sponge_log.xml --report_suite_name c_windows_dbg -l c -c dbg
sh tools\run_tests\helper_scripts\run_tests_in_workspace.sh -t -j 4 -x c_windows_opt_sponge_log.xml --report_suite_name c_windows_opt -l c -c opt
sh tools\run_tests\helper_scripts\run_tests_in_workspace.sh -t -j 4 -x csharp_windows_dbg_sponge_log.xml --report_suite_name csharp_windows_dbg -l csharp -c dbg
sh tools\run_tests\helper_scripts\run_tests_in_workspace.sh -t -j 4 -x csharp_windows_opt_sponge_log.xml --report_suite_name csharp_windows_opt -l csharp -c opt
sh tools\run_tests\helper_scripts\run_tests_in_workspace.sh -t -j 4 -x node_windows_dbg_sponge_log.xml --report_suite_name node_windows_dbg -l node -c dbg
sh tools\run_tests\helper_scripts\run_tests_in_workspace.sh -t -j 4 -x node_windows_opt_sponge_log.xml --report_suite_name node_windows_opt -l node -c opt
sh tools\run_tests\helper_scripts\run_tests_in_workspace.sh -t -j 4 -x python_windows_dbg_sponge_log.xml --report_suite_name python_windows_dbg -l python -c dbg
sh tools\run_tests\helper_scripts\run_tests_in_workspace.sh -t -j 4 -x python_windows_opt_sponge_log.xml --report_suite_name python_windows_opt -l python -c opt
