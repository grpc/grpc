#!/bin/bash
# Copyright 2015, gRPC authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Initializes a fresh GCE VM to become a jenkins linux performance worker.
# You shouldn't run this script on your own,
# use create_linux_performance_worker.sh instead.

set -ex

sudo apt-get update

# Install Java 8 JDK (to build gRPC Java)
sudo apt-get install -y openjdk-8-jdk
sudo apt-get install -y unzip lsof

# Add pubkey of jenkins@grpc-jenkins-master to authorized keys of jenkins@
# This needs to happen as the last step to prevent Jenkins master from connecting
# to a machine that hasn't been properly setup yet.
cat jenkins_master.pub | sudo tee --append ~jenkins/.ssh/authorized_keys

sudo apt-get install -y \
  autoconf \
  autotools-dev \
  build-essential \
  bzip2 \
  ccache \
  curl \
  gcc \
  gcc-multilib \
  git \
  gyp \
  lcov \
  libc6 \
  libc6-dbg \
  libc6-dev \
  libgtest-dev \
  libtool \
  make \
  strace \
  pypy \
  python-dev \
  python-pip \
  python-setuptools \
  python-yaml \
  python3-dev \
  python3-pip \
  python3-setuptools \
  python3-yaml \
  telnet \
  unzip \
  wget \
  zip

# perftools
sudo apt-get install -y google-perftools libgoogle-perftools-dev

# netperf
sudo apt-get install -y netperf

# C++ dependencies
sudo apt-get install -y libgflags-dev libgtest-dev libc++-dev clang

# Python dependencies
sudo pip install tabulate
sudo pip install google-api-python-client
sudo pip install virtualenv

# TODO(jtattermusch): For some reason, building gRPC Python depends on python3.4
# being installed, but python3.4 is not available on Ubuntu 16.04.
# Temporarily fixing this by adding a PPA with python3.4, but we should
# really remove this hack once possible.
sudo add-apt-repository -y ppa:fkrull/deadsnakes
sudo apt-get update
sudo apt-get install -y python3.4 python3.4-dev
python3.4 -m pip install virtualenv

curl -O https://bootstrap.pypa.io/get-pip.py
sudo pypy get-pip.py
sudo pypy -m pip install tabulate
sudo pip install google-api-python-client

# Node dependencies (nvm has to be installed under user jenkins)
touch .profile
curl -o- https://raw.githubusercontent.com/creationix/nvm/v0.25.4/install.sh | bash
source ~/.nvm/nvm.sh
nvm install 0.12 && npm config set cache /tmp/npm-cache
nvm install 4 && npm config set cache /tmp/npm-cache
nvm install 5 && npm config set cache /tmp/npm-cache
nvm alias default 4

# C# mono dependencies (http://www.mono-project.com/docs/getting-started/install/linux/#debian-ubuntu-and-derivatives)
sudo apt-key adv --keyserver hkp://keyserver.ubuntu.com:80 --recv-keys 3FA7E0328081BFF6A14DA29AA6A19B38D3D831EF
echo "deb http://download.mono-project.com/repo/debian wheezy main" | sudo tee /etc/apt/sources.list.d/mono-xamarin.list
sudo apt-get update
sudo apt-get install -y mono-devel nuget

# C# .NET Core dependencies (https://www.microsoft.com/net/core#ubuntu)
sudo sh -c 'echo "deb [arch=amd64] https://apt-mo.trafficmanager.net/repos/dotnet-release/ xenial main" > /etc/apt/sources.list.d/dotnetdev.list'
sudo apt-key adv --keyserver apt-mo.trafficmanager.net --recv-keys 417A0893
sudo apt-get update
sudo apt-get install -y dotnet-dev-1.0.0-preview2-003131

# Ruby dependencies
gpg --keyserver hkp://keys.gnupg.net --recv-keys 409B6B1796C275462A1703113804BB82D39DC0E3
curl -sSL https://get.rvm.io | bash -s stable --ruby

# Install bundler (prerequisite for gRPC Ruby)
source ~/.rvm/scripts/rvm
gem install bundler

# Java dependencies - nothing as we already have Java JDK 8

# Go dependencies
# Currently, the golang package available via apt-get doesn't have the latest go.
# Significant performance improvements with grpc-go have been observed after
# upgrading from go 1.5 to a later version, so a later go version is preferred.
# Following go install instructions from https://golang.org/doc/install
GO_VERSION=1.7.1
OS=linux
ARCH=amd64
curl -O https://storage.googleapis.com/golang/go${GO_VERSION}.${OS}-${ARCH}.tar.gz
sudo tar -C /usr/local -xzf go$GO_VERSION.$OS-$ARCH.tar.gz
# Put go on the PATH, keep the usual installation dir
sudo ln -s /usr/local/go/bin/go /usr/bin/go
rm go$GO_VERSION.$OS-$ARCH.tar.gz

# Install perf, to profile benchmarks. (need to get the right linux-tools-<> for kernel version)
sudo apt-get install -y linux-tools-common linux-tools-generic linux-tools-`uname -r`
# see http://unix.stackexchange.com/questions/14227/do-i-need-root-admin-permissions-to-run-userspace-perf-tool-perf-events-ar
echo 0 | sudo tee /proc/sys/kernel/perf_event_paranoid
# see http://stackoverflow.com/questions/21284906/perf-couldnt-record-kernel-reference-relocation-symbol
echo 0 | sudo tee /proc/sys/kernel/kptr_restrict

# qps workers under perf appear to need a lot of mmap pages under certain scenarios and perf args in
# order to not lose perf events or time out
echo 4096 | sudo tee /proc/sys/kernel/perf_event_mlock_kb

# Fetch scripts to generate flame graphs from perf data collected
# on benchmarks
git clone -v https://github.com/brendangregg/FlameGraph ~/FlameGraph

