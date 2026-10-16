#!/bin/bash

# Copyright 2015 gRPC authors.
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

SYSTEM=$(uname | cut -f 1 -d_)

if [ "$SYSTEM" == "Darwin" ] ; then
  # Workaround for crash during bundle install
  # See suggestion in https://github.com/bundler/bundler/issues/3692
  BUNDLE_SPECIFIC_PLATFORM=true bundle install
else
  # TODO(jtattermusch): remove the retry hack
  # on linux artifact build, multiple instances of "bundle install" run in parallel
  # in different workspaces. That should work fine since the workspaces
  # are isolated, but causes occasional
  # failures (builder/gem bug?). Retrying fixes the issue.
  # Note that using bundle install --retry is not enough because
  # that only retries downloading, not installation.
  bundle install || (sleep 10; bundle install)
fi

