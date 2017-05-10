%#!/bin/bash
#% Copyright 2017, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Creates test cases for a language by running run_interop_test in manual mode
# and save the generated output under ./testcases/<lang>__<release>.
#
# Params:
#   LANG - The language.
#   SKIP_TEST - If set, skip running the test cases for sanity.
#   RELEASE - Create testcase for specific release, defautl to 'master'.
#   KEEP_IMAGE - Do not clean local docker image created for the test cases.

set -e

cd $(dirname $0)/../..
GRPC_ROOT=$(pwd)
CMDS_SH="${GRPC_ROOT}/interop_client_cmds.sh"
TESTCASES_DIR=${GRPC_ROOT}/tools/interop_matrix/testcases

echo "Create '$LANG' test cases for gRPC release '${RELEASE:=master}'"

# Invoke run_interop_test in manual mode.
${GRPC_ROOT}/tools/run_tests/run_interop_tests.py -l $LANG --use_docker \
  --cloud_to_prod --manual_run

# Clean up
function cleanup {
  [ -z "$testcase" ] && testcase=$CMDS_SH
  echo "testcase: $testcase"
  if [ -e $testcase ]; then
    # The script should generate a line with "${docker_image:=...}".
    eval docker_image=$(grep -oe '${docker_image:=.*}' $testcase)
    if [ -z "$KEEP_IMAGE" ]; then
      echo "Clean up docker_image $docker_image"
      docker rmi -f $docker_image
    else
      echo "Kept docker_image $docker_image"
    fi
  fi
  [ -e "$CMDS_SH" ] && rm $CMDS_SH
}
trap cleanup EXIT
# Running the testcases as sanity unless we are asked to skip.
[ -z "$SKIP_TEST" ] && (echo "Running test cases: $CMDS_SH"; sh $CMDS_SH)

mkdir -p $TESTCASES_DIR
testcase=$TESTCASES_DIR/${LANG}__$RELEASE
if [ -e $testcase ]; then
  echo "Updating: $testcase"
  diff $testcase $CMDS_SH || true
fi
mv $CMDS_SH $testcase
chmod a+x $testcase
echo "Test cases created: $testcase"
