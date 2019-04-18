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

FROM ubuntu:latest

RUN apt-get update && apt-get install -y \
  cmake \
  g++ \
  gcc \
  git \
  make \
  python \
  && apt-get clean

RUN git clone -b release_36 http://llvm.org/git/llvm.git
RUN git clone -b release_36 http://llvm.org/git/clang.git
RUN git clone -b release_36 http://llvm.org/git/compiler-rt.git
RUN git clone -b release_36 http://llvm.org/git/clang-tools-extra.git
RUN git clone -b release_36 http://llvm.org/git/libcxx.git
RUN git clone -b release_36 http://llvm.org/git/libcxxabi.git

RUN mv clang llvm/tools
RUN mv compiler-rt llvm/projects
RUN mv clang-tools-extra llvm/tools/clang/tools
RUN mv libcxx llvm/projects
RUN mv libcxxabi llvm/projects

RUN mkdir llvm-build
RUN cd llvm-build && cmake \
  -DCMAKE_BUILD_TYPE:STRING=Release \
  -DLLVM_TARGETS_TO_BUILD:STRING=X86 \
  ../llvm
RUN make -C llvm-build && make -C llvm-build install && rm -rf llvm-build

CMD ["bash"]
