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

FROM ubuntu:16.04

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

#====================
# Python dependencies

# Install dependencies

RUN apt-get update && apt-get install -y \
    python-all-dev \
    python3-all-dev \
    python-setuptools

# Install Python packages from PyPI
RUN curl https://bootstrap.pypa.io/pip/2.7/get-pip.py | python2.7
RUN pip install --upgrade pip==19.3.1
RUN pip install virtualenv==16.7.9
RUN pip install futures==2.2.0 enum34==1.0.4 protobuf==3.5.2.post1 six==1.15.0 twisted==17.5.0

# Google Cloud platform API libraries
RUN pip install --upgrade google-auth==1.24.0 google-api-python-client==1.12.8 oauth2client==4.1.0

#=================
# C++ dependencies
RUN apt-get update && apt-get -y install libgflags-dev libgtest-dev libc++-dev clang && apt-get clean

#=================
# Install cmake
# Note that this step should be only used for distributions that have new enough cmake to satisfy gRPC's cmake version requirement.

RUN apt-get update && apt-get install -y cmake && apt-get clean


RUN mkdir /var/local/jenkins


# Installs clang 4.0 and 5.0, the clang++ is included in clang packages
RUN apt-get update && apt-get -y install clang-4.0 clang-5.0 && apt-get clean

# Define the default command.
CMD ["bash"]
