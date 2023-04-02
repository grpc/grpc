#!/bin/bash
# Copyright 2021 The gRPC Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Source this rc script at the beginning of your CI script to move
# the entire source tree to a different directory.
# That avoids slow finalization (and a number of other problems)
# caused by kokoro rsyncing the entire /tmpfs/src directory
# back to the agent.
# See b/74837748 for more context.

set -ex

CURRENT_DIR="$(pwd)"
CI_SCRIPT_RELATIVE_TO_SRC="$0"

if [ "${CURRENT_DIR}" == "/tmpfs/altsrc" ]
then
  # we already respawned under /tmpfs/altsrc, no need to do anything
  echo "Successfully respawned the CI script ${CI_SCRIPT_RELATIVE_TO_SRC} under ${CURRENT_DIR}."
  # scripts in tools/run_tests generate test reports and we need to make sure these reports
  # land in a directory that is going to be rsynced back to the kokoro agent.
  export GRPC_TEST_REPORT_BASE_DIR="/tmpfs/src/github/grpc"
elif [ "${CURRENT_DIR}" == "/tmpfs/src" ]
then
  # we need to respawn now
  # - step out of current directory
  # - rename src/github to altsrc/github (/tmpfs/src is the directory that gets rsynced back to kokoro agent once the CI script finishes).
  #   Note that we don't want to move the entire /tmpfs/src tree (which contains e.g. the input artifacts as well,
  #   and their path is referenced by kokoro env variables), just /tmpfs/src/github
  #   which contains all the cloned github repositories (=our workspace in which the build happens)
  echo "Moving workspace from /tmpfs/src to /tmpfs/altsrc and respawning the CI script."
  # once "exec bash" starts, the CI script in the original location will no longer be in use and it will be safe to move
  # the entire "/tmpfs/src/github" tree (moving the bash script file while running is probably safe anyway on unix, but it doesn't
  # hurt to be extra careful)
  exec bash -c "set -ex; cd /tmpfs; mkdir -p altsrc; mv src/github altsrc; cd altsrc; exec '${CI_SCRIPT_RELATIVE_TO_SRC}'"
else
  # avoid messing with the workspace if we don't see the directory layout
  # that's standard on kokoro (in case this script gets accidentally invoked outside of kokoro)
  echo "Looks the script is currently not running on kokoro, skipping respawn and continuing the original CI script."
fi
