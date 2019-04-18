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

outdir=`pwd`

tmpdir=`mktemp -d`
mkdir -p $tmpdir/logs
repos="grpc grpc-common grpc-go grpc-java grpc.github.io"
for repo in $repos
do
  cd $tmpdir
  git clone https://github.com/grpc/$repo.git
  cd $repo
  gource --output-custom-log $tmpdir/logs/$repo
  sed -i "s,|/,|/$repo/,g" $tmpdir/logs/$repo
done
cat $tmpdir/logs/* | sort -n > $outdir/all-logs.txt
