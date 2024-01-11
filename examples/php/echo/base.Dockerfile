# Copyright 2019 gRPC authors.
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

FROM php:7.4-buster

RUN apt-get -qq update && apt-get -qq install -y \
  autoconf automake cmake curl git libtool \
  pkg-config unzip zlib1g-dev

ARG MAKEFLAGS=-j8


WORKDIR /github/grpc

RUN git clone https://github.com/grpc/grpc . && \
  git submodule update --init --recursive

WORKDIR /github/grpc/cmake/build

RUN cmake ../.. && \
  make protoc grpc_php_plugin

RUN pecl install grpc
