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





