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

FROM ubuntu:15.10

# Install Git and basic packages.
RUN apt-get update && apt-get install -y \
  autoconf \
  autotools-dev \
  build-essential \
  bzip2 \
  ccache \
  curl \
  gcc \
  gcc-multilib \
  git \
  golang \
  gyp \
  lcov \
  libc6 \
  libc6-dbg \
  libc6-dev \
  libgtest-dev \
  libtool \
  make \
  perl \
  strace \
  python-dev \
  python-setuptools \
  python-yaml \
  telnet \
  unzip \
  wget \
  zip && apt-get clean

#================
# Build profiling
RUN apt-get update && apt-get install -y time && apt-get clean


#========================
# Bazel installation
RUN apt-get install -y software-properties-common g++
RUN echo "deb [arch=amd64] http://storage.googleapis.com/bazel-apt stable jdk1.8" > /etc/apt/sources.list.d/bazel.list
RUN curl https://bazel.build/bazel-release.pub.gpg | apt-key add -
RUN apt-get -y update
RUN apt-get -y install bazel

RUN mkdir -p /var/local/jenkins

# Define the default command.
CMD ["bash"]
