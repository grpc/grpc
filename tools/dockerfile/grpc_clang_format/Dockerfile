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

FROM ubuntu:15.10

RUN apt-get update && apt-get -y install wget
RUN echo deb http://llvm.org/apt/wily/ llvm-toolchain-wily-3.8 main >> /etc/apt/sources.list
RUN echo deb-src http://llvm.org/apt/wily/ llvm-toolchain-wily-3.8 main >> /etc/apt/sources.list
RUN wget -O - http://llvm.org/apt/llvm-snapshot.gpg.key| apt-key add -
RUN apt-get update && apt-get -y install clang-format-3.8

ADD clang_format_all_the_things.sh /
CMD ["echo 'Run with tools/distrib/clang_format_code.sh'"]
