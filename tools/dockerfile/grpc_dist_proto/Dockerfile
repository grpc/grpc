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

#=================
# Install apt-get-install-retry wrapper script
#=================
#  Create wrapper for apt-get

RUN /bin/bash -c 'echo -e "#!/usr/bin/env bash\n\
# Copyright 2026 The gRPC Authors\n\
\n\
PS4=\"+ \$(date +[%H:%M:%S_%Z]) \"\n\
set -xeuo pipefail\n\
\n\
MAX_RETRIES=4\n\
delay=3\n\
\n\
for i in \$(seq 1 \$MAX_RETRIES); do\n\
  if apt-get update && apt-get install -y \"\$@\"; then\n\
    break\n\
  fi\n\
  sleep \$((delay**i))\n\
done\n\
" > /usr/local/bin/apt-get-install-retry'
RUN chmod +x /usr/local/bin/apt-get-install-retry# Add the file containing the gRPC version
ADD version.txt version.txt

# Install tools needed for building protoc.
RUN apt-get-install-retry libgtest-dev

# Get the protobuf source from GitHub.
RUN mkdir -p /var/local/git
RUN git clone https://github.com/protocolbuffers/protobuf.git /var/local/git/protobuf

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





