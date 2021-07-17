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

FROM grpc/clang:latest

RUN apt-get update && apt-get install -y \
  autoconf \
  libtool \
  libgtest-dev \
  && apt-get clean

RUN git clone --recursive https://github.com/grpc/grpc.git

EXPOSE 8181

CMD \
  (cd grpc ; git pull) && \
  (cd grpc ; git submodule update --init --recursive) && \
  llvm/tools/clang/tools/scan-build/scan-build -o /tmp/grpc --use-analyzer=/usr/local/bin/clang make -C grpc buildtests && \
  llvm/tools/clang/tools/scan-view/scan-view /tmp/grpc/`ls /tmp/grpc` --host 0.0.0.0 --no-browser --allow-all-hosts
