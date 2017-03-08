# Copyright 2015-2016, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

FROM debian:jessie

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

#====================
# Python dependencies

# Install dependencies

RUN apt-get update && apt-get install -y \
    python-all-dev \
    python3-all-dev \
    python-pip

# Install Python packages from PyPI
RUN pip install pip --upgrade
RUN pip install virtualenv
RUN pip install futures==2.2.0 enum34==1.0.4 protobuf==3.2.0 six==1.10.0

# Prepare ccache
RUN ln -s /usr/bin/ccache /usr/local/bin/gcc
RUN ln -s /usr/bin/ccache /usr/local/bin/g++
RUN ln -s /usr/bin/ccache /usr/local/bin/cc
RUN ln -s /usr/bin/ccache /usr/local/bin/c++
RUN ln -s /usr/bin/ccache /usr/local/bin/clang
RUN ln -s /usr/bin/ccache /usr/local/bin/clang++

#=================
# C++ dependencies
RUN apt-get update && apt-get -y install libgflags-dev libgtest-dev libc++-dev clang && apt-get clean

# Google Cloud platform API libraries
RUN apt-get update && apt-get install -y python-pip && apt-get clean
RUN pip install --upgrade google-api-python-client


#=================
# Update clang to a version with improved tsan and fuzzing capabilities

RUN apt-get update && apt-get -y install python cmake && apt-get clean

RUN git clone -n -b release_38 http://llvm.org/git/llvm.git && \
  cd llvm && git checkout ad57503 && cd ..
RUN git clone -n -b release_38 http://llvm.org/git/clang.git && \
  cd clang && git checkout ad2c56e && cd ..
RUN git clone -n -b release_38 http://llvm.org/git/compiler-rt.git && \
  cd compiler-rt && git checkout 3176922 && cd ..
RUN git clone -n -b release_38 \
  http://llvm.org/git/clang-tools-extra.git && cd clang-tools-extra && \
  git checkout c288525 && cd ..
RUN git clone -n -b release_38 http://llvm.org/git/libcxx.git && \
  cd libcxx && git checkout fda3549  && cd ..
RUN git clone -n -b release_38 http://llvm.org/git/libcxxabi.git && \
  cd libcxxabi && git checkout 8d4e51d && cd ..

RUN mv clang llvm/tools
RUN mv compiler-rt llvm/projects
RUN mv clang-tools-extra llvm/tools/clang/tools
RUN mv libcxx llvm/projects
RUN mv libcxxabi llvm/projects

RUN mkdir llvm-build
RUN cd llvm-build && cmake \
  -DCMAKE_BUILD_TYPE:STRING=Release \
  -DCMAKE_INSTALL_PREFIX:STRING=/usr \
  -DLLVM_TARGETS_TO_BUILD:STRING=X86 \
  ../llvm
RUN make -C llvm-build -j 12 && make -C llvm-build install && rm -rf llvm-build

# Define the default command.
CMD ["bash"]
