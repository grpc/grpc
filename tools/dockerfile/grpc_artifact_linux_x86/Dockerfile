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

# Docker file for building gRPC artifacts.

FROM i386/debian:jessie
RUN sed -i '/deb http:\/\/http.debian.net\/debian jessie-updates main/d' /etc/apt/sources.list

# Install Git and basic packages.
RUN apt-get update && apt-get install -y \
  autoconf \
  autotools-dev \
  build-essential \
  bzip2 \
  clang \
  curl \
  gcc \
  gcc-multilib \
  git \
  golang \
  libc6 \
  libc6-dbg \
  libc6-dev \
  libgtest-dev \
  libtool \
  make \
  perl \
  strace \
  python-dev \
  python-setuptools \
  python-yaml \
  telnet \
  unzip \
  wget \
  zip && apt-get clean


##################
# Ruby dependencies

# Install rvm
RUN apt-get update && apt-get install -y gnupg2
RUN gpg2 --keyserver hkp://pool.sks-keyservers.net --recv-keys 409B6B1796C275462A1703113804BB82D39DC0E3 7D2BAF1CF37B13E2069D6956105BD0E739499BDB
RUN \curl -sSL https://get.rvm.io | bash -s stable

# Install Ruby 2.1
RUN /bin/bash -l -c "rvm install ruby-2.1"
RUN /bin/bash -l -c "rvm use --default ruby-2.1"
RUN /bin/bash -l -c "echo 'gem: --no-document' > ~/.gemrc"
RUN /bin/bash -l -c "echo 'export PATH=/usr/local/rvm/bin:$PATH' >> ~/.bashrc"
RUN /bin/bash -l -c "echo 'rvm --default use ruby-2.1' >> ~/.bashrc"
RUN /bin/bash -l -c "gem install bundler -v 1.17.3 --no-document"


##################
# C# dependencies (needed to build grpc_csharp_ext)

# Use cmake 3.6 from jessie-backports
RUN echo "deb http://archive.debian.org/debian jessie-backports main" | tee /etc/apt/sources.list.d/jessie-backports.list
RUN echo 'Acquire::Check-Valid-Until "false";' > /etc/apt/apt.conf
RUN sed -i '/deb http:\/\/deb.debian.org\/debian jessie-updates main/d' /etc/apt/sources.list
RUN apt-get update && apt-get install -t jessie-backports -y cmake && apt-get clean


##################
# Python AuditWheel dependencies (needed to check manylinux1 compatibility)

RUN apt-get install -y python3 python3-pip
RUN pip3 install auditwheel==2.1.1


RUN mkdir /var/local/jenkins

# Define the default command.
CMD ["bash"]
