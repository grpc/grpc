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

FROM debian:stretch
  
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

# Install Python 2.7
RUN apt-get update && apt-get install -y python2.7 python-all-dev
RUN curl https://bootstrap.pypa.io/pip/2.7/get-pip.py | python2.7

# Google Cloud platform API libraries
RUN pip install --upgrade google-auth==1.24.0 google-api-python-client==1.12.8 oauth2client==4.1.0

# Add Debian 'buster' repository, we will need it for installing newer versions of python
RUN echo 'deb http://ftp.de.debian.org/debian buster main' >> /etc/apt/sources.list
RUN echo 'APT::Default-Release "stretch";' | tee -a /etc/apt/apt.conf.d/00local

RUN mkdir /var/local/jenkins


#=================
# Compile CPython 3.6.9 from source

RUN apt-get update && apt-get install -y zlib1g-dev libssl-dev
RUN apt-get update && apt-get install -y jq build-essential libffi-dev

RUN cd /tmp && \
    wget -q https://www.python.org/ftp/python/3.6.9/Python-3.6.9.tgz && \
    tar xzvf Python-3.6.9.tgz && \
    cd Python-3.6.9 && \
    ./configure && \
    make install

RUN cd /tmp && \
    echo "ff7cdaef4846c89c1ec0d7b709bbd54d Python-3.6.9.tgz" > checksum.md5 && \
    md5sum -c checksum.md5

RUN python3.6 -m ensurepip && \
    python3.6 -m pip install coverage

