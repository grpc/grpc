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

FROM alpine:3.3

# Install Git and basic packages.
RUN apk update && apk add \
  autoconf \
  automake \
  bzip2 \
  build-base \
  cmake \
  ccache \
  curl \
  gcc \
  git \
  libtool \
  make \
  perl \
  strace \
  python-dev \
  py-pip \
  unzip \
  wget \
  zip

# Install Python packages from PyPI
RUN pip install --upgrade pip==9.0.1
RUN pip install virtualenv
RUN pip install futures==2.2.0 enum34==1.0.4 protobuf==3.2.0 six==1.10.0

# Google Cloud platform API libraries
RUN pip install --upgrade google-api-python-client

# Prepare ccache
RUN ln -s /usr/bin/ccache /usr/local/bin/gcc
RUN ln -s /usr/bin/ccache /usr/local/bin/g++
RUN ln -s /usr/bin/ccache /usr/local/bin/cc
RUN ln -s /usr/bin/ccache /usr/local/bin/c++

RUN mkdir -p /var/local/jenkins

# Define the default command.
CMD ["bash"]
