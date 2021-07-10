#!/usr/bin/env bash
# Copyright 2021 The gRPC Authors
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

#install ubuntu pre-requisites
sudo apt update
sudo apt install -y build-essential autoconf libtool pkg-config cmake python python-pip clang
sudo pip install six

# java pre-requisites
# TODO: with openjdk11, benchmarks fail with: "Error: Could not find or load main class com.google.caliper.worker.WorkerMain"
# see https://github.com/protocolbuffers/protobuf/issues/8680
#sudo apt-get install -y maven openjdk-11-jdk-headless
sudo apt-get install -y maven openjdk-8-jdk-headless

# TODO(get rid of hack to uninstall openjdk 11)
sudo apt-get remove -y openjdk-11-jdk-headless || true
sudo apt-get remove -y openjdk-11-jre-headless || true
java -version

# go pre-requisites
sudo snap install go --classic
go version

# node pre-requisites
curl -o- https://raw.githubusercontent.com/creationix/nvm/v0.33.4/install.sh | bash
set +ex
. ~/.nvm/nvm.sh
# TODO: install newer version of node
nvm install 12
nvm use 12
set -ex

git clone https://github.com/jtattermusch/protobuf.git
cd protobuf
git checkout benchmarks_no_tcmalloc
git submodule update --init

kokoro/linux/benchmark/run.sh
