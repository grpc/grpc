#!/bin/bash

# Copyright 2016 gRPC authors.
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

cd $(dirname $0)/../../..
bazel=`pwd`/tools/bazel

if [ $# -eq 0 ]; then
  UPB_OUTPUT_DIR=$PWD/src/core/ext/upb-generated
  UPBDEFS_OUTPUT_DIR=$PWD/src/core/ext/upbdefs-generated
  rm -rf $UPB_OUTPUT_DIR
  rm -rf $UPBDEFS_OUTPUT_DIR
  mkdir -p $UPB_OUTPUT_DIR
  mkdir -p $UPBDEFS_OUTPUT_DIR
else
  UPB_OUTPUT_DIR=$1/upb-generated
  UPBDEFS_OUTPUT_DIR=$1/upbdefs-generated
  mkdir $UPB_OUTPUT_DIR
  mkdir $UPBDEFS_OUTPUT_DIR
fi

# generate upb files from bazel rules
python3 tools/codegen/core/gen_upb_api_from_bazel_xml.py \
  --upb_out=$UPB_OUTPUT_DIR \
  --upbdefs_out=$UPBDEFS_OUTPUT_DIR \
  --verbose
