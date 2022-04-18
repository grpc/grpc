#!/bin/bash
# Copyright 2022 gRPC authors.
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

# Prepares the build dependencies
apt update
apt install -y zlib1g-dev libssl-dev jq build-essential libffi-dev wget

# Compiles Python 3.9.12
cd /tmp
wget -q https://www.python.org/ftp/python/3.9.12/Python-3.9.12.tgz
tar xzvf Python-3.9.12.tgz

pushd ./Python-3.9.12
./configure --enable-optimizations
make -j8
make install
popd

echo "abc7f7f83ea8614800b73c45cf3262d3 Python-3.9.12.tgz" > checksum.md5
md5sum -c checksum.md5

python3.10 -m ensurepip
python3.10 -m pip install coverage
