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

FROM debian:jessie

# Install Git and basic packages.
RUN apt-get update && apt-get install -y \
  autoconf \
  autotools-dev \
  build-essential \
  bzip2 \
  ccache \
  curl \
  dnsutils \
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

# Google Cloud platform API libraries
RUN apt-get update && apt-get install -y python-pip && apt-get clean
RUN pip install --upgrade google-api-python-client

#====================
# Python dependencies

# Install dependencies

RUN apt-get update && apt-get install -y \
    python-all-dev \
    python3-all-dev \
    python-pip

# Install Python packages from PyPI
RUN pip install --upgrade pip==10.0.1
RUN pip install virtualenv
RUN pip install futures==2.2.0 enum34==1.0.4 protobuf==3.5.2.post1 six==1.10.0 twisted==17.5.0

#=================
# C++ dependencies
RUN apt-get update && apt-get -y install libgflags-dev libgtest-dev libc++-dev clang && apt-get clean

#========================
# Sanity test dependencies
RUN apt-get update && apt-get install -y \
      python-pip \
      autoconf \
      automake \
      libtool \
      curl \
      python-virtualenv \
      python-lxml \
      shellcheck
RUN pip install simplejson mako

#======================================
# More sanity test dependencies (bazel)
RUN echo "deb http://http.debian.net/debian jessie-backports main" >> /etc/apt/sources.list
RUN apt-get update
RUN apt-get install -y -t jessie-backports openjdk-8-jdk

#========================
# Bazel installation
RUN echo "deb [arch=amd64] http://storage.googleapis.com/bazel-apt stable jdk1.8" > /etc/apt/sources.list.d/bazel.list
RUN curl https://bazel.build/bazel-release.pub.gpg | apt-key add -
RUN apt-get -y update
RUN apt-get -y install bazel

# Pin Bazel to 0.9.0
# Installing Bazel via apt-get first is required before installing 0.9.0 to
# allow gRPC to build without errors. See https://github.com/grpc/grpc/issues/10553
RUN curl -fSsL -O https://github.com/bazelbuild/bazel/releases/download/0.9.0/bazel-0.9.0-installer-linux-x86_64.sh
RUN chmod +x ./bazel-0.9.0-installer-linux-x86_64.sh
RUN ./bazel-0.9.0-installer-linux-x86_64.sh

RUN apt-get update && apt-get -y install wget xz-utils
RUN wget http://releases.llvm.org/5.0.0/clang+llvm-5.0.0-linux-x86_64-ubuntu14.04.tar.xz
RUN tar xf clang+llvm-5.0.0-linux-x86_64-ubuntu14.04.tar.xz
RUN ln -s /clang+llvm-5.0.0-linux-x86_64-ubuntu14.04/bin/clang-format /usr/local/bin/clang-format
ENV CLANG_FORMAT=clang-format
RUN ln -s /clang+llvm-5.0.0-linux-x86_64-ubuntu14.04/bin/clang-tidy /usr/local/bin/clang-tidy
ENV CLANG_TIDY=clang-tidy

# Prepare ccache
RUN ln -s /usr/bin/ccache /usr/local/bin/gcc
RUN ln -s /usr/bin/ccache /usr/local/bin/g++
RUN ln -s /usr/bin/ccache /usr/local/bin/cc
RUN ln -s /usr/bin/ccache /usr/local/bin/c++
RUN ln -s /usr/bin/ccache /usr/local/bin/clang
RUN ln -s /usr/bin/ccache /usr/local/bin/clang++


RUN mkdir /var/local/jenkins


# Define the default command.
CMD ["bash"]
