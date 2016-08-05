# Copyright 2016, Google Inc.
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

FROM ubuntu:14.04

RUN apt-get update && \
  apt-get install -y \
  git build-essential \
  pkg-config flex \
  bison \
  libkrb5-dev \
  libsasl2-dev \
  libnuma-dev \
  pkg-config \
  libssl-dev \
  autoconf libtool \
  cmake \
  libiberty-dev \
  g++ unzip \
  curl make automake libtool libboost-dev

# Configure git
RUN git config --global user.name "Jenkins" && \
  git config --global user.email "jenkins@grpc"

# Clone gRPC
RUN git clone https://github.com/grpc/grpc

# Update Submodules
RUN cd grpc && git submodule update --init

# Install protobuf
RUN cd grpc/third_party/protobuf && ./autogen.sh && ./configure && \
  make -j && make check -j && make install && ldconfig

# Install gRPC
RUN cd grpc && make -j && make install

# Install thrift
RUN cd grpc/third_party/thrift && git am --signoff < ../../tools/grift/grpc_plugins_generator.patch && \
  ./bootstrap.sh && ./configure && make -j && make install