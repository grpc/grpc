# Copyright 2016 gRPC authors.
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