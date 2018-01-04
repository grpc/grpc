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

out=$(readlink -f "${1:-coverage}")

root=$(readlink -f "$(dirname "$0")/../../..")
shift || true
tmp=$(mktemp)
cd "$root"
tools/run_tests/run_tests.py -c gcov -l c c++ "$@" || true
lcov --capture --directory . --output-file "$tmp"
genhtml "$tmp" --output-directory "$out"
rm "$tmp"
if which xdg-open > /dev/null
then
  xdg-open "file://$out/index.html"
fi
