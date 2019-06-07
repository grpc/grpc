#!/bin/bash

# Copyright 2019 The gRPC Authors
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

VERSION_REGEX="v3.8.*"
REPO="python/cpython"

LATEST=$(curl -s https://api.github.com/repos/$REPO/tags | \
          jq -r '.[] | select(.name|test("'$VERSION_REGEX'")) | .name' \
          | sort | tail -n1)

wget https://github.com/$REPO/archive/$LATEST.tar.gz
tar xzvf *.tar.gz
( cd cpython*
  ./configure
  make install
)
