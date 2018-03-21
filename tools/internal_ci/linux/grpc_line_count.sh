#!/usr/bin/env bash
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
#
# This script counts the numbers of line in gRPC's repo and uploads to BQ
set -ex

# Enter the gRPC repo root
cd $(dirname $0)/../../..

git submodule update --init

# Install cloc
git clone -b v1.72 https://github.com/AlDanial/cloc/ ~/cloc
PERL_MM_USE_DEFAULT=1 sudo perl -MCPAN -e 'install Regexp::Common; install Algorithm::Diff'
sudo make install -C ~/cloc/Unix

./tools/line_count/collect-now.sh
