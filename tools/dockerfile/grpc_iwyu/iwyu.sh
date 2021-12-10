#!/bin/sh
# Copyright 2017 gRPC authors.
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

cd ${IWYU_ROOT}

export PATH=${PATH}:${IWYU_ROOT}/iwyu_build/bin

rm -rf iwyu || true
git clone https://github.com/include-what-you-use/include-what-you-use.git iwyu
# latest commit on the clang 11 branch
cd ${IWYU_ROOT}/iwyu && git checkout 5db414ac448004fe019871c977905cb7c2cff23f
mkdir -p ${IWYU_ROOT}/iwyu_build && cd ${IWYU_ROOT}/iwyu_build && cmake -G "Unix Makefiles" -DCMAKE_PREFIX_PATH=/usr/lib/llvm-11 /iwyu && make
cd ${IWYU_ROOT}

cat compile_commands.json | sed "s,\"file\": \",\"file\": \"${IWYU_ROOT}/,g" > compile_commands_for_iwyu.json

# figure out which files to include
cat compile_commands.json | jq -r '.[].file' \
  | grep -E "^src/core/lib/promise/" \
  | grep -v -E "/upb-generated/|/upbdefs-generated/" \
  | sort \
  | tee iwyu_files.txt

# run iwyu, filtering out changes to port_platform.h
xargs -a iwyu_files.txt ${IWYU_ROOT}/iwyu/iwyu_tool.py -p compile_commands_for_iwyu.json -j 16 \
  | grep -v -E "port_platform.h" \
  | tee iwyu.out

cat iwyu.out | grep -Ev "^namespace " > iwyu.out.filtered

# apply the suggested changes
${IWYU_ROOT}/iwyu/fix_includes.py --nocomments < iwyu.out.filtered || true

# reformat sources, since iwyu gets this wrong
xargs -a iwyu_files.txt $CLANG_FORMAT -i

# TODO(ctiller): expand this to match the clang-tidy directories:
#  | grep -E "(^include/|^src/core/|^src/cpp/|^test/core/|^test/cpp/)"

git diff --exit-code > /dev/null
