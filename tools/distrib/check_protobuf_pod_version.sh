#!/bin/bash
# Copyright 2019 gRPC authors.
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

cd `dirname $0`/../..

# get the version of protobuf in /third_party/protobuf
pushd third_party/protobuf

version1=$(git describe --tags | cut -f 1 -d'-')
v1=${version1:1}
# Protobuf has recently changed the versioning of the release branches/tags
# and the same release commit can be tagged with mutliple tag names
# (e.g. v3.21.12 is also tagged as v21.12), which ultimately confuses
# the output of "git describe --tags" and makes it non-deterministic.
# The hack below converts the version number to always become 3.x.y
# regardless of what tag name we get back from "git describe --tags".
# Hack: In case we got a 2-part "x.y" version number from the tag,
# convert it to version number in "3.x.y" format.
# TODO(jtattermusch): find a better workaround for this.
v1=$(echo "$v1" | sed 's/^\([0-9]*\)\.\([0-9]*\)$/3.\1.\2/')

popd

# get the version of protobuf in /src/objective-c/!ProtoCompiler.podspec
v2=$(cat src/objective-c/\!ProtoCompiler.podspec | egrep "v = " | cut -f 2 -d"'")

# get the version of protobuf in /src/objective-c/!ProtoCompiler-gRPCPlugin.podspec
v3=$(cat src/objective-c/\!ProtoCompiler-gRPCPlugin.podspec | egrep 'dependency.*!ProtoCompiler' | cut -f 4 -d"'")

# compare and emit error
ret=0
if [ "$v1" != "$v2" ]; then
  echo 'Protobuf version in src/objective-c/!ProtoCompiler.podspec does not match protobuf version in third_party/protobuf.'
  ret=1
fi

if [ "$v1" != "$v3" ]; then
  echo 'Protobuf version in src/objective-c/!ProtoCompiler-gRPCPlugin.podspec does not match protobuf version in third_party/protobuf.'
  ret=1
fi
  
exit $ret
