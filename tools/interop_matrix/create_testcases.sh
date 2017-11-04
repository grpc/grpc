#!/bin/bash
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

function createtests {
# Invoke run_interop_test in manual mode.
# TODO(adelez): Add cloud_gateways when we figure out how to skip them if not 
# running in GCE.
if [ $1 == "cxx" ]; then
client_lang="c++"
else
client_lang=$1
fi
echo $client_lang

${GRPC_ROOT}/tools/run_tests/run_interop_tests.py -l $client_lang --use_docker \
  --cloud_to_prod --prod_servers default gateway_v4 --manual_run

trap cleanup EXIT
# TODO(adelez): add test auth tests but do not run if not testing on GCE.
# Running the testcases as sanity unless we are asked to skip.
[ -z "$SKIP_TEST" ] && (echo "Running test cases: $CMDS_SH"; sh $CMDS_SH)

mkdir -p $TESTCASES_DIR
testcase=$TESTCASES_DIR/$1__$RELEASE
if [ -e $testcase ]; then
  echo "Updating: $testcase"
  diff $testcase $CMDS_SH || true
fi
mv $CMDS_SH $testcase
chmod a+x $testcase
echo "Test cases created: $testcase"
}

if [ $LANG == "csharp" ]; then
createtests "csharp"
createtests "csharpcoreclr"
else
createtests $LANG
fi 
