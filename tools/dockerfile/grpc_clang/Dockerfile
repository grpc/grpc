# Copyright 2015, Google Inc.
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
