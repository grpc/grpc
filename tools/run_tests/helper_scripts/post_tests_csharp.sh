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

if [ "$CONFIG" != "gcov" ] ; then exit ; fi

# change to gRPC repo root
cd "$(dirname "$0")/../../.."

# Generate the csharp extension coverage report
gcov objs/gcov/src/csharp/ext/*.o
lcov --base-directory . --directory . -c -o coverage.info
lcov -e coverage.info '**/src/csharp/ext/*' -o coverage.info
genhtml -o reports/csharp_ext_coverage --num-spaces 2 \
  -t 'gRPC C# native extension test coverage' coverage.info \
  --rc genhtml_hi_limit=95 --rc genhtml_med_limit=80 --no-prefix
