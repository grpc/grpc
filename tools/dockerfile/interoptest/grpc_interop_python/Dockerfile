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

FROM debian:bullseye
  
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
  python3-dev \
  python3-setuptools \
  python3-yaml \
  telnet \
  unzip \
  wget \
  zip && \
  apt-get clean

#================
# Build profiling
RUN apt-get update && apt-get install -y time && apt-get clean

# Install Python 2.7
RUN apt-get update && apt-get install -y python2.7 python-all-dev
RUN curl https://bootstrap.pypa.io/pip/2.7/get-pip.py | python2.7

# Google Cloud platform API libraries
RUN pip install --upgrade google-auth==1.24.0 google-api-python-client==1.12.8 oauth2client==4.1.0


RUN mkdir /var/local/jenkins



RUN apt-get update && apt-get install -y python3 python3-all-dev python3-pip
RUN ln -s $(which python2) /usr/bin/python
