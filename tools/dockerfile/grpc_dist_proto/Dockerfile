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

# Dockerfile to build protoc and plugins for inclusion in a release.
FROM grpc/base

# Add the file containing the gRPC version
ADD version.txt version.txt

# Install tools needed for building protoc.
RUN apt-get update && apt-get -y install libgflags-dev libgtest-dev

# Get the protobuf source from GitHub.
RUN mkdir -p /var/local/git
RUN git clone https://github.com/google/protobuf.git /var/local/git/protobuf

# Build the protobuf library statically and install to /tmp/protoc_static.
WORKDIR /var/local/git/protobuf
RUN ./autogen.sh && \
    ./configure --disable-shared --prefix=/tmp/protoc_static \
    LDFLAGS="-lgcc_eh -static-libgcc -static-libstdc++" && \
    make -j12 && make check && make install

# Build the protobuf library dynamically and install to /usr/local.
WORKDIR /var/local/git/protobuf
RUN ./autogen.sh && \
    ./configure --prefix=/usr/local && \
    make -j12 && make check && make install

# Build the grpc plugins.
RUN git clone https://github.com/google/grpc.git /var/local/git/grpc
WORKDIR /var/local/git/grpc
RUN LDFLAGS=-static make plugins

# Create an archive containing all the generated binaries.
RUN mkdir /tmp/proto-bins_$(cat /version.txt)_linux-$(uname -m)
RUN cp -v bins/opt/* /tmp/proto-bins_$(cat /version.txt)_linux-$(uname -m)
RUN cp -v /tmp/protoc_static/bin/protoc /tmp/proto-bins_$(cat /version.txt)_linux-$(uname -m)
RUN cd /tmp && \
    tar -czf proto-bins_$(cat /version.txt)_linux-$(uname -m).tar.gz proto-bins_$(cat /version.txt)_linux-$(uname -m)

# List the tar contents: provides a way to visually confirm that the contents
# are correct.
RUN echo 'proto-bins_$(cat /version.txt)_linux-tar-$(uname -m) contents:' && \
    tar -ztf /tmp/proto-bins_$(cat /version.txt)_linux-$(uname -m).tar.gz





