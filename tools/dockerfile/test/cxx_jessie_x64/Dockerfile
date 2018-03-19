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

#=================
# Update clang to a version with improved tsan and fuzzing capabilities

RUN apt-get update && apt-get -y install python && apt-get clean

RUN git clone -n -b release_50 http://llvm.org/git/llvm.git && \
  cd llvm && git checkout 5ae73c3 && cd ..
RUN git clone -n -b release_50 http://llvm.org/git/clang.git && \
  cd clang && git checkout 334f080 && cd ..
RUN git clone -n -b release_50 http://llvm.org/git/compiler-rt.git && \
  cd compiler-rt && git checkout 4b38c40 && cd ..
RUN git clone -n -b release_50 \
  http://llvm.org/git/clang-tools-extra.git && cd clang-tools-extra && \
  git checkout bf75b14 && cd ..
RUN git clone -n -b release_50 http://llvm.org/git/libcxx.git && \
  cd libcxx && git checkout 598ae4f  && cd ..
RUN git clone -n -b release_50 http://llvm.org/git/libcxxabi.git && \
  cd libcxxabi && git checkout 5df6b5d && cd ..

RUN mv clang llvm/tools
RUN mv compiler-rt llvm/projects
RUN mv clang-tools-extra llvm/tools/clang/tools
RUN mv libcxx llvm/projects
RUN mv libcxxabi llvm/projects

# Install newer version of cmake to support building Clang.
RUN curl -o ./cmake-3.9.6-Linux-x86_64.sh https://cmake.org/files/v3.9/cmake-3.9.6-Linux-x86_64.sh
RUN chmod +x ./cmake-3.9.6-Linux-x86_64.sh
RUN ./cmake-3.9.6-Linux-x86_64.sh --skip-license --prefix=/usr/local
RUN cmake

RUN mkdir llvm-build
RUN cd llvm-build && cmake \
  -DCMAKE_BUILD_TYPE:STRING=Release \
  -DCMAKE_INSTALL_PREFIX:STRING=/usr \
  -DLLVM_TARGETS_TO_BUILD:STRING=X86 \
  ../llvm
RUN make -C llvm-build -j 12 && make -C llvm-build install && rm -rf llvm-build

# Prepare ccache
RUN ln -s /usr/bin/ccache /usr/local/bin/gcc
RUN ln -s /usr/bin/ccache /usr/local/bin/g++
RUN ln -s /usr/bin/ccache /usr/local/bin/cc
RUN ln -s /usr/bin/ccache /usr/local/bin/c++
RUN ln -s /usr/bin/ccache /usr/local/bin/clang
RUN ln -s /usr/bin/ccache /usr/local/bin/clang++


RUN mkdir /var/local/jenkins

#================
# libuv
RUN cd /tmp     && wget http://dist.libuv.org/dist/v1.9.1/libuv-v1.9.1.tar.gz     && tar -xf libuv-v1.9.1.tar.gz     && cd libuv-v1.9.1     && sh autogen.sh && ./configure --prefix=/usr && make && make install

# Install gcc-4.8 and other relevant items
RUN apt-get update && apt-get -y install gcc-4.8 gcc-4.8-multilib g++-4.8 g++-4.8-multilib && apt-get clean

# Define the default command.
CMD ["bash"]
