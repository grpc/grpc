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

FROM php:7.2-stretch

RUN apt-get -qq update && apt-get -qq install -y \
  autoconf automake curl git libtool \
  pkg-config unzip zlib1g-dev


WORKDIR /tmp

RUN curl -sSL https://github.com/protocolbuffers/protobuf/releases/download/v3.8.0/\
protoc-3.8.0-linux-x86_64.zip -o /tmp/protoc.zip && \
  unzip -qq protoc.zip && \
  cp /tmp/bin/protoc /usr/local/bin/protoc


WORKDIR /github/grpc

RUN git clone https://github.com/grpc/grpc . && \
  git submodule update --init && \
  cd third_party/protobuf && git submodule update --init

RUN make grpc_php_plugin

RUN pecl install grpc
