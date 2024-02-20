#!/usr/bin/env bash
# Copyright 2024 The gRPC Authors
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

# Checks if any of the Python artifacts exceeds a certern size limit since
# Pypi has a per-file size limit.

set -ex

find . -path "*/artifacts/*" -size +80M | egrep '.*' && echo "Found Python artifacts larger than 80 MB." && FAILED="true"


if [ "$FAILED" != "" ]
then
  exit 1
fi
