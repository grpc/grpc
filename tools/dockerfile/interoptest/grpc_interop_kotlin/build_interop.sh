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
#
# Builds Kotlin interop server and client in a base image.
set -e

cp -r /var/local/jenkins/grpc-kotlin /tmp/grpc-kotlin

# copy service account keys if available
cp -r /var/local/jenkins/service_account $HOME || true

pushd /tmp/grpc-kotlin
./gradlew --no-daemon build -PskipAndroid=true -x test
./gradlew --no-daemon :interop_testing:installDist -PskipCodegen=true -PskipAndroid=true

mkdir -p /var/local/git/grpc-kotlin/
cp -r --parents -t /var/local/git/grpc-kotlin/ \
    interop_testing/build/install/ \
    run-test-client.sh \
    run-test-server.sh

popd
rm -r /tmp/grpc-kotlin
rm -r "$HOME/.gradle"

# enable extra java logging
mkdir -p /var/local/grpc_kotlin_logging
echo "handlers = java.util.logging.ConsoleHandler
java.util.logging.ConsoleHandler.level = ALL
.level = FINE
io.grpc.netty.NettyClientHandler = ALL
io.grpc.netty.NettyServerHandler = ALL" > /var/local/grpc_kotlin_logging/logconf.txt
  
