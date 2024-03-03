#!/bin/bash
# Copyright 2018 gRPC authors.
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

set -e
cd $(dirname $0)/../../..

ALL_IMAGES=( grpc-ext grpc-src alpine centos7 php-src php-zts
             fork-support i386 php8.2 )

if [[ "$1" == "--cmds" ]]; then
  for arg in "${ALL_IMAGES[@]}"
  do
    echo "docker run -it --rm grpc-php/$arg"
  done
  exit 0
fi

if [[ $# -eq 0 ]]; then
  lst=("${ALL_IMAGES[@]}")
else
  lst=("$@")
fi

set -x
for arg in "${lst[@]}"
do
  echo "Running $arg..."
  docker run -it --rm grpc-php/"$arg"
done
