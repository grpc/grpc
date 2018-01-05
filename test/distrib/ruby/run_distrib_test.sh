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

cd $(dirname $0)

ARCH=$1
PLATFORM=$2
# Create an indexed local gem source with gRPC gems to test
GEM_SOURCE=../../../gem_source
mkdir -p ${GEM_SOURCE}/gems
cp $EXTERNAL_GIT_ROOT/input_artifacts/grpc-*$ARCH-$PLATFORM.gem ${GEM_SOURCE}/gems
if [[ "$(ls ${GEM_SOURCE}/gems | grep grpc | wc -l)" != 1 ]]; then
  echo "Sanity check failed. Copied over more than one grpc gem into the gem source directory."
  exit 1
fi;
gem install builder
gem generate_index --directory ${GEM_SOURCE}

bundle install

bundle exec ./distribtest.rb
