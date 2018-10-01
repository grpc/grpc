# Copyright 2018 The gRPC Authors
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

FROM debian:stretch

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

# Install Node dependencies
RUN touch .profile
RUN curl -o- https://raw.githubusercontent.com/creationix/nvm/v0.25.4/install.sh | bash
RUN /bin/bash -l -c "nvm install 8 && npm install -g node-pre-gyp"

##################
# Install cross compiler for ARM

RUN echo 'deb http://emdebian.org/tools/debian/ jessie main' | tee -a /etc/apt/sources.list.d/crosstools.list && \
  curl http://emdebian.org/tools/debian/emdebian-toolchain-archive.key | apt-key add -

RUN dpkg --add-architecture armhf && apt-get update && apt-get install -y crossbuild-essential-armhf && apt-get clean

RUN mkdir /var/local/jenkins

# Define the default command.
CMD ["bash"]
