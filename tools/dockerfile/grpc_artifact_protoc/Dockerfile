# Copyright 2016, Google Inc.
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

# Docker file for building protoc and gRPC protoc plugin artifacts.
# forked from https://github.com/google/protobuf/blob/master/protoc-artifacts/Dockerfile

FROM centos:6.6

RUN yum install -y git \
                   tar \
                   wget \
                   make \
                   autoconf \
                   curl-devel \
                   unzip \
                   automake \
                   libtool \
                   glibc-static.i686 \
                   glibc-devel \
                   glibc-devel.i686

# Install GCC 4.7 to support -static-libstdc++
RUN wget http://people.centos.org/tru/devtools-1.1/devtools-1.1.repo -P /etc/yum.repos.d
RUN bash -c 'echo "enabled=1" >> /etc/yum.repos.d/devtools-1.1.repo'
RUN bash -c "sed -e 's/\$basearch/i386/g' /etc/yum.repos.d/devtools-1.1.repo > /etc/yum.repos.d/devtools-i386-1.1.repo"
RUN sed -e 's/testing-/testing-i386-/g' -i /etc/yum.repos.d/devtools-i386-1.1.repo

# We'll get and "Rpmdb checksum is invalid: dCDPT(pkg checksums)" error caused by
# docker issue when using overlay storage driver, but all the stuff we need
# will be installed, so for now we just ignore the error.
# https://github.com/docker/docker/issues/10180
RUN yum install -y devtoolset-1.1 \
                   devtoolset-1.1-libstdc++-devel \
                   devtoolset-1.1-libstdc++-devel.i686 || true

# Start in devtoolset environment that uses GCC 4.7
CMD ["scl", "enable", "devtoolset-1.1", "bash"]
