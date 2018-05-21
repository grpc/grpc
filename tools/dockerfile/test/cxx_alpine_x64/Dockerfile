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

FROM alpine:3.5

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
  linux-headers \
  make \
  perl \
  strace \
  python-dev \
  py-pip \
  py-yaml \
  unzip \
  wget \
  zip

# Install Python packages from PyPI
RUN pip install --upgrade pip==10.0.1
RUN pip install virtualenv
RUN pip install futures==2.2.0 enum34==1.0.4 protobuf==3.5.0.post1 six==1.10.0 twisted==17.5.0

# Google Cloud platform API libraries
RUN pip install --upgrade google-api-python-client

# Install gflags
RUN git clone https://github.com/gflags/gflags.git && cd gflags && git checkout v2.2.0
RUN cd gflags && cmake . && make && make install
RUN ln -s /usr/local/include/gflags /usr/include/gflags

# Prepare ccache
RUN ln -s /usr/bin/ccache /usr/local/bin/gcc
RUN ln -s /usr/bin/ccache /usr/local/bin/g++
RUN ln -s /usr/bin/ccache /usr/local/bin/cc
RUN ln -s /usr/bin/ccache /usr/local/bin/c++
RUN ln -s /usr/bin/ccache /usr/local/bin/clang
RUN ln -s /usr/bin/ccache /usr/local/bin/clang++


RUN mkdir /var/local/jenkins


# Define the default command.
CMD ["bash"]
