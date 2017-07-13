# Copyright 2016 gRPC authors.
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

# Update Git to version >1.7 to allow cloning submodules with --reference arg.
RUN yum remove -y git && yum clean all
RUN yum install -y https://centos6.iuscommunity.org/ius-release.rpm && yum clean all
RUN yum install -y git2u && yum clean all

# Start in devtoolset environment that uses GCC 4.7
CMD ["scl", "enable", "devtoolset-1.1", "bash"]
