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

# number of CPUs available
CPU_COUNT=`nproc`

rm -rf iwyu || true
git clone https://github.com/include-what-you-use/include-what-you-use.git iwyu
# latest commit on the clang 13 branch
cd ${IWYU_ROOT}/iwyu
git checkout fbd921d6640bf1b18fe5a8a895636215367eb6b9
mkdir -p ${IWYU_ROOT}/iwyu_build
cd ${IWYU_ROOT}/iwyu_build
cmake -G "Unix Makefiles" -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DLLVM_ROOT_DIR=/usr/lib/llvm-13 ${IWYU_ROOT}/iwyu 
make -j $CPU_COUNT
cd ${IWYU_ROOT}

# patch python shebang for our environment (we need python3, not python)
sed -i 's,^#!/usr/bin/env python,#!/usr/bin/env python3,g' ${IWYU_ROOT}/iwyu/iwyu_tool.py
sed -i 's,^#!/usr/bin/env python,#!/usr/bin/env python3,g' ${IWYU_ROOT}/iwyu/fix_includes.py

cat compile_commands.json | sed "s,\"file\": \",\"file\": \"${IWYU_ROOT}/,g" > compile_commands_for_iwyu.json

export ENABLED_MODULES='
  src/core/ext/transport/chttp2
  src/core/lib/avl
  src/core/lib/channel
  src/core/lib/config
  src/core/lib/gprpp
  src/core/lib/json
  src/core/lib/slice
  src/core/lib/resource_quota
  src/core/lib/promise
  src/core/lib/transport
  src/core/lib/uri
'

export INCLUSION_REGEX=`echo $ENABLED_MODULES | sed 's/ /|/g' | sed 's,\\(.*\\),^(\\1)/,g'`

# figure out which files to include
cat compile_commands.json | jq -r '.[].file' \
  | grep -E $INCLUSION_REGEX \
  | grep -v -E "/upb-generated/|/upbdefs-generated/" \
  | sort \
  > iwyu_files.txt

echo '#!/bin/sh
${IWYU_ROOT}/iwyu/iwyu_tool.py -p compile_commands_for_iwyu.json $1 -- -Xiwyu --no_fwd_decls -Xiwyu --update_comments \
  | grep -v -E "port_platform.h" \
  | grep -v -E "^(- )?namespace " \
  > iwyu/iwyu.`echo $1 | sha1sum`.out
' > iwyu/run_iwyu_on.sh
chmod +x iwyu/run_iwyu_on.sh

# run iwyu, filtering out changes to port_platform.h
xargs -n 1 -P $CPU_COUNT -a iwyu_files.txt ${IWYU_ROOT}/iwyu/run_iwyu_on.sh

cat iwyu/iwyu.*.out > iwyu.out

# apply the suggested changes
${IWYU_ROOT}/iwyu/fix_includes.py --nocomments --nosafe_headers < iwyu.out || true

# reformat sources, since iwyu gets this wrong
xargs -a iwyu_files.txt ${CLANG_FORMAT:-clang-format} -i

# TODO(ctiller): expand this to match the clang-tidy directories:
#  | grep -E "(^include/|^src/core/|^src/cpp/|^test/core/|^test/cpp/)"

git diff --exit-code > /dev/null
