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

set -x

cd ${IWYU_ROOT}

export PATH=${PATH}:${IWYU_ROOT}/iwyu_build/bin

# number of CPUs available
CPU_COUNT=`nproc`

rm -rf iwyu || true
git clone https://github.com/include-what-you-use/include-what-you-use.git iwyu

###############################################################################
#
#   BEWARE!  BEWARE!  BEWARE!  BEWARE!  BEWARE!  BEWARE!  BEWARE!  BEWARE!
#
#   Changing the version of iwyu can bring along subtle changes.
#   You *must* test the new version of iwyu:
#   1. run it on the entire codebase before submitting
#   2. UPLOAD A CHANGE THAT SHOULD BE BROKEN AFTER SUBMISSION OF THIS CHANGE
#   ensure that the broken change is caught by the new version of iwyu
#
#   BEWARE!  BEWARE!  BEWARE!  BEWARE!  BEWARE!  BEWARE!  BEWARE!  BEWARE!
#
###############################################################################

# latest commit on the clang 15 branch
cd ${IWYU_ROOT}/iwyu
git checkout 7f0b6c304acf69c42bb7f6e03c63f836924cb7e0
if [ $? -ne 0 ]; then
  echo "Failed to checkout iwyu commit"
  exit 1
fi
mkdir -p ${IWYU_ROOT}/iwyu_build
cd ${IWYU_ROOT}/iwyu_build
cmake -G "Unix Makefiles" -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DLLVM_ROOT_DIR=/usr/lib/llvm-15 ${IWYU_ROOT}/iwyu 
if [ $? -ne 0 ]; then
  echo "Failed to cmake iwyu"
  exit 1
fi
make -j $CPU_COUNT
if [ $? -ne 0 ]; then
  echo "Failed to make iwyu"
  exit 1
fi
cd ${IWYU_ROOT}

# patch python shebang for our environment (we need python3, not python)
sed -i 's,^#!/usr/bin/env python,#!/usr/bin/env python3,g' ${IWYU_ROOT}/iwyu/iwyu_tool.py
sed -i 's,^#!/usr/bin/env python,#!/usr/bin/env python3,g' ${IWYU_ROOT}/iwyu/fix_includes.py

cat compile_commands.json                            \
  | sed "s/ -DNDEBUG//g"                             \
  | sed "s/ -std=c\\+\\+14/ -std=c++17/g"            \
  | sed "s,\"file\": \",\"file\": \"${IWYU_ROOT}/,g" \
  > compile_commands_for_iwyu.json

export ENABLED_MODULES='
  src/core/ext
  src/core/lib
  src/cpp
  test/core
  fuzztest
'

export DISABLED_MODULES='
  src/core/lib/gpr
  src/core/lib/iomgr
  src/core/ext/transport/binder
  test/core/alts
  test/core/iomgr
  test/core/security
  test/core/tsi
  test/core/transport/binder
'

export INCLUSION_REGEX=`echo $ENABLED_MODULES | sed 's/ /|/g' | sed 's,\\(.*\\),^(\\1)/,g'`
export EXCLUSION_REGEX=`echo $DISABLED_MODULES | sed 's/ /|/g' | sed 's,\\(.*\\),^(\\1)/,g'`

# figure out which files to include
cat compile_commands.json | jq -r '.[].file'                                     \
  | grep -E $INCLUSION_REGEX                                                     \
  | grep -v -E "/upb-generated/|/upbdefs-generated/"                             \
  | grep -v -E $EXCLUSION_REGEX                                                  \
  | grep -v src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h \
  | grep -v test/core/end2end/end2end_tests.cc                                   \
  | sort                                                                         \
  > iwyu_files0.txt

cat iwyu_files0.txt                    \
  | xargs -d '\n' ls -1df 2> /dev/null \
  > iwyu_files.txt                     \
  || true

echo '#!/bin/sh
${IWYU_ROOT}/iwyu/iwyu_tool.py -p compile_commands_for_iwyu.json $1       \
    -- -Xiwyu --no_fwd_decls                                              \
       -Xiwyu --update_comments                                           \
       -Xiwyu --mapping_file=${IWYU_ROOT}/tools/distrib/iwyu_mappings.imp \
  | grep -v -E "port_platform.h"                                          \
  | grep -v -E "repeated_ptr_field.h"                                     \
  | grep -v -E "repeated_field.h"                                         \
  | grep -v -E "^(- )?namespace "                                         \
  > iwyu/iwyu.`echo $1 | sha1sum`.out
' > iwyu/run_iwyu_on.sh
chmod +x iwyu/run_iwyu_on.sh

# run iwyu, filtering out changes to port_platform.h
xargs -n 1 -P $CPU_COUNT -a iwyu_files.txt ${IWYU_ROOT}/iwyu/run_iwyu_on.sh

cat iwyu/iwyu.*.out > iwyu.out

# apply the suggested changes
${IWYU_ROOT}/iwyu/fix_includes.py \
  --nocomments                    \
  --nosafe_headers                \
  --ignore_re='^(include/.*|src/core/lib/security/credentials/tls/grpc_tls_credentials_options\.h)' \
  < iwyu.out                      \
  | grep 'IWYU edited 0 files on your behalf'

if [ $? -ne 0 ] 
then
    echo "Iwyu edited some files. Here is the diff of files edited by iwyu:"
    git --no-pager diff
    # Exit with a non zero error code to ensure sanity checks fail accordingly.
    exit 1
fi
