#!/usr/bin/env bash
# Copyright 2018 The gRPC Authors
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

set -ex

# change to grpc repo root
cd "$(dirname "$0")/../../.."

# After kokoro build finishes, the workspace gets rsync'ed to another machine,
# from where the artifacts and reports are processed.
# Especially on Windows, the rsync can take long time, so we cleanup the workspace
# after finishing each build. We only leave files we want to keep:
# - reports and artifacts
# - directory containing the kokoro scripts to prevent deleting a script while being executed.
time find . -type f -not -iname "*sponge_log.*" -not -path "./reports/*" -not -path "./artifacts/*" -not -path "./tools/internal_ci/*" -exec rm -f {} +
