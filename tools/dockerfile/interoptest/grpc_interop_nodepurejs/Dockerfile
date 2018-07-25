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

FROM debian:jessie

# Install Git and basic packages.
RUN apt-get update && apt-get install -y \
  autoconf \
  autotools-dev \
  build-essential \
  bzip2 \
  ccache \
  curl \
  dnsutils \
  gcc \
  gcc-multilib \
  git \
  golang \
  gyp \
  lcov \
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

#================
# Build profiling
RUN apt-get update && apt-get install -y time && apt-get clean

#==================
# Node dependencies

# Install nvm
RUN touch .profile
RUN curl -o- https://raw.githubusercontent.com/creationix/nvm/v0.25.4/install.sh | bash
# Install all versions of node that we want to test
RUN /bin/bash -l -c "nvm install 4 && npm config set cache /tmp/npm-cache"
RUN /bin/bash -l -c "nvm install 5 && npm config set cache /tmp/npm-cache"
RUN /bin/bash -l -c "nvm install 6 && npm config set cache /tmp/npm-cache"
RUN /bin/bash -l -c "nvm install 8 && npm config set cache /tmp/npm-cache"
RUN /bin/bash -l -c "nvm install 9 && npm config set cache /tmp/npm-cache"
RUN /bin/bash -l -c "nvm install 10 && npm config set cache /tmp/npm-cache"
RUN /bin/bash -l -c "nvm alias default 10"

RUN mkdir /var/local/jenkins

# Define the default command.
CMD ["bash"]
